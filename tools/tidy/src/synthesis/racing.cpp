// SPDX-FileCopyrightText: Michael Popoloski
// SPDX-License-Identifier: MIT
//
// tools/tidy/src/style/ForkSharedWriteRace.cpp

#include "ASTHelperVisitors.h"
#include "TidyDiags.h"
#include "fmt/color.h"
#include <algorithm>
#include <optional>
#include <unordered_set>
#include <vector>

#include "slang/analysis/AnalysisManager.h"
#include "slang/ast/SemanticFacts.h"
#include "slang/ast/symbols/SubroutineSymbols.h"
#include "slang/ast/symbols/VariableSymbols.h"
#include "slang/ast/types/AllTypes.h"

using namespace slang;
using namespace slang::ast;
using namespace slang::analysis;

namespace fork_shared_write_race {

// Access paths

/// One step below a root symbol.
///  - Member:  a struct field or class property.
///  - Bits:    a constant index or range, as an inclusive interval.
///  - Dynamic: an index or range whose bounds aren't constant. Kept in the path (rather than
///             truncating it) so later selectors can still prove disjointness, e.g.
///             data[i].a vs data[j].b.
struct PathElem {
    enum class Kind { Member, Bits, Dynamic } kind;
    const Symbol* member = nullptr;
    int64_t lo = 0;
    int64_t hi = 0;
};

/// An lvalue reduced to a root symbol plus its selection path. No handle
/// alias analysis is done: `a.x` and `b.x` have different roots even if a and b refer to the
/// same object (documented limitation).
struct AccessPath {
    const Symbol* root = nullptr;
    std::vector<PathElem> elems;
    SourceRange range;
};

static std::optional<int64_t> constantInt(const Expression& expr) {
    auto cv = expr.getConstant();
    if (!cv || !cv->isInteger())
        return std::nullopt;
    return cv->integer().as<int64_t>();
}

/// `this` is a reserved keyword, so a symbol with that name can only be the implicit handle
/// slang creates for a method (SubroutineSymbol::thisVar).
static bool isImplicitThis(const Symbol& sym) {
    return sym.name == "this";
}

/// Returns false when the root can't be resolved lexically (e.g. `get_obj().x = 1`).
static bool buildPath(const Expression& expr, AccessPath& path) {
    switch (expr.kind) {
        case ExpressionKind::NamedValue:
        case ExpressionKind::HierarchicalValue:
            path.root = &expr.as<ValueExpressionBase>().symbol;
            return true;

        case ExpressionKind::ElementSelect: {
            auto& sel = expr.as<ElementSelectExpression>();
            if (!buildPath(sel.value(), path))
                return false;

            if (auto idx = constantInt(sel.selector()))
                path.elems.push_back({PathElem::Kind::Bits, nullptr, *idx, *idx});
            else
                path.elems.push_back({PathElem::Kind::Dynamic});
            return true;
        }

        case ExpressionKind::RangeSelect: {
            auto& sel = expr.as<RangeSelectExpression>();
            if (!buildPath(sel.value(), path))
                return false;

            auto left = constantInt(sel.left());
            auto right = constantInt(sel.right());
            if (!left || !right) {
                path.elems.push_back({PathElem::Kind::Dynamic});
                return true;
            }

            int64_t lo = std::min(*left, *right);
            int64_t hi = std::max(*left, *right);
            switch (sel.getSelectionKind()) {
                case RangeSelectionKind::IndexedUp:
                    lo = *left;
                    hi = *left + *right - 1;
                    break;
                case RangeSelectionKind::IndexedDown:
                    lo = *left - *right + 1;
                    hi = *left;
                    break;
                default:
                    break;
            }
            path.elems.push_back({PathElem::Kind::Bits, nullptr, lo, hi});
            return true;
        }

        case ExpressionKind::MemberAccess: {
            auto& access = expr.as<MemberAccessExpression>();
            if (!buildPath(access.value(), path))
                return false;

            // `this.x` and a bare `x` inside a method name the same property.
            if (path.elems.empty() && isImplicitThis(*path.root)) {
                path.root = &access.member;
                return true;
            }

            path.elems.push_back({PathElem::Kind::Member, &access.member});
            return true;
        }

        default:
            return false;
    }
}

/// May-overlap: same root and no step proves the two paths disjoint. A dynamic step is
/// "unknown", so comparison continues past it: a provably different member or a disjoint
/// constant range further down still separates the paths.
static bool mayOverlap(const AccessPath& a, const AccessPath& b) {
    if (a.root != b.root)
        return false;

    auto n = std::min(a.elems.size(), b.elems.size());
    for (size_t k = 0; k < n; k++) {
        auto& x = a.elems[k];
        auto& y = b.elems[k];

        bool xMember = x.kind == PathElem::Kind::Member;
        bool yMember = y.kind == PathElem::Kind::Member;
        if (xMember != yMember)
            return true; // shapes disagree; can't prove anything

        if (xMember) {
            if (x.member != y.member)
                return false;
            continue;
        }

        if (x.kind == PathElem::Kind::Dynamic || y.kind == PathElem::Kind::Dynamic)
            continue;

        if (x.hi < y.lo || y.hi < x.lo)
            return false;
    }
    return true;
}

// A race warning is suppressed only by a proof, never by the mere presence of
// a synchronization call. In particular, get/put on a semaphore does not prove
// exclusivity (it may have multiple keys); mailbox operations can consume old
// messages; and an event wait need not observe the trigger in the other child.
// No synchronization proof is currently implemented. This is intentional:
// reporting a possible race is preferable to declaring an actual race safe.

struct Write {
    AccessPath path;
};

/// Collect writes belonging to one sequential child process. Nested forks
/// create new processes and must not be flattened into this child's timeline.
struct WriteCollector : ASTVisitor<WriteCollector, VisitFlags::AllGood> {
    std::vector<Write> writes;

    void handle(const BlockStatement& stmt) {
        if (stmt.blockKind != StatementBlockKind::Sequential)
            return;
        visitDefault(stmt);
    }

    void handle(const AssignmentExpression& expr) {
        visitDefault(expr);
        record(expr.left());
    }

    void handle(const UnaryExpression& expr) {
        visitDefault(expr);
        switch (expr.op) {
            case UnaryOperator::Preincrement:
            case UnaryOperator::Predecrement:
            case UnaryOperator::Postincrement:
            case UnaryOperator::Postdecrement:
                record(expr.operand());
                break;
            default:
                break;
        }
    }

    void handle(const CallExpression& call) {
        visitDefault(call);
        recordRefArguments(call);
    }

private:
    void record(const Expression& lhs) {
        if (lhs.kind == ExpressionKind::Concatenation) {
            for (auto operand : lhs.as<ConcatenationExpression>().operands())
                record(*operand);
            return;
        }

        AccessPath path;
        path.range = lhs.sourceRange;
        if (buildPath(lhs, path))
            writes.push_back({std::move(path)});
    }

    void recordRefArguments(const CallExpression& call) {
        if (call.isSystemCall())
            return;

        auto sub = std::get<0>(call.subroutine);
        if (!sub)
            return;

        auto formals = sub->getArguments();
        auto actuals = call.arguments();
        for (size_t i = 0; i < std::min(formals.size(), actuals.size()); ++i) {
            const auto* formal = formals[i];
            if (formal->direction != ArgumentDirection::Ref ||
                formal->flags.has(VariableFlags::Const))
                continue;
            // slang binds output / inout arguments as AssignmentExpression.
            if (actuals[i]->kind != ExpressionKind::Assignment)
                record(*actuals[i]);
        }
    }
};

/// The immediate statements of a fork are its children. The declarations
/// preceding them are initialized in the parent, not spawned as children.
static void collectChildren(const Statement& body, std::vector<const Statement*>& children) {
    auto add = [&](const Statement& stmt) {
        if (stmt.kind != StatementKind::VariableDeclaration &&
            stmt.kind != StatementKind::Empty)
            children.push_back(&stmt);
    };

    if (auto list = body.as_if<StatementList>()) {
        for (auto stmt : list->list)
            add(*stmt);
    }
    else {
        add(body);
    }
}

struct MainVisitor : TidyVisitor, ASTVisitor<MainVisitor, VisitFlags::AllCanonical> {
    explicit MainVisitor(Diagnostics& diagnostics) : TidyVisitor(diagnostics) {}

    void handle(const BlockStatement& stmt) {
        visitDefault(stmt); // Independently check every nested fork.
        if (stmt.blockKind == StatementBlockKind::Sequential)
            return;
        NEEDS_SKIP_STATEMENT(stmt)
        checkFork(stmt);
    }

private:
    void checkFork(const BlockStatement& fork) {
        std::vector<const Statement*> children;
        collectChildren(fork.body, children);
        if (children.size() < 2)
            return;

        std::vector<WriteCollector> processes(children.size());
        for (size_t i = 0; i < children.size(); ++i)
            children[i]->visit(processes[i]);

        // Report once per root per fork, on the later child in source order.
        std::unordered_set<const Symbol*> reported;
        for (size_t later = 1; later < processes.size(); ++later) {
            for (const auto& candidate : processes[later].writes) {
                if (reported.contains(candidate.path.root))
                    continue;

                bool conflicting = false;
                for (size_t earlier = 0; earlier < later && !conflicting; ++earlier) {
                    for (const auto& previous : processes[earlier].writes) {
                        if (mayOverlap(previous.path, candidate.path)) {
                            conflicting = true;
                            break;
                        }
                    }
                }

                if (conflicting) {
                    diags.add(diag::ForkSharedWriteRace, candidate.path.range)
                        << candidate.path.root->name;
                    reported.insert(candidate.path.root);
                }
            }
        }
    }
};
} // namespace fork_shared_write_race

using namespace fork_shared_write_race;

class ForkSharedWriteRace : public TidyCheck {
public:
    [[maybe_unused]] explicit ForkSharedWriteRace(TidyKind kind,
                                                  std::optional<slang::DiagnosticSeverity> severity) :
        TidyCheck(kind, severity) {}

    bool check(const RootSymbol& root, const AnalysisManager&) override {
        MainVisitor visitor(diagnostics);
        root.visit(visitor);
        return diagnostics.empty();
    }

    DiagCode diagCode() const override { return diag::ForkSharedWriteRace; }

    std::string diagString() const override {
        return "'{}' may be written by multiple concurrent fork children";
    }

    DiagnosticSeverity diagDefaultSeverity() const override { return DiagnosticSeverity::Warning; }
    std::string name() const override { return "ForkSharedWriteRace"; }

    std::string shortDescription() const override {
        return "Warns about overlapping writes from different children of a fork.";
    }

    std::string description() const override {
        return shortDescription() +
               " Detects overlapping lvalue paths in fork/join, join_any, and join_none. "
               "Synchronization is not used to suppress diagnostics unless proven safe; "
               "this version does not attempt such proofs. Analysis is intraprocedural "
               "and does not resolve handle aliases, called task bodies, or parent/child "
               "continuations. Nested forks are checked separately.\n\n" +
               fmt::format(fmt::emphasis::italic,
                           "module m;\n"
                           "    int count;\n"
                           "    initial fork\n"
                           "        count++;\n"
                           "        count++; // possible concurrent write\n"
                           "    join\n"
                           "endmodule\n");
    }
};

REGISTER(ForkSharedWriteRace, ForkSharedWriteRace, TidyKind::Style)
