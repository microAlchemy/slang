// SPDX-FileCopyrightText: Michael Popoloski
// SPDX-License-Identifier: MIT

#include "ASTHelperVisitors.h"
#include "TidyDiags.h"
#include "fmt/color.h"
#include <algorithm>
#include <iterator>
#include <optional>
#include <span>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "slang/analysis/AnalysisManager.h"
#include "slang/ast/EvalContext.h"
#include "slang/ast/SystemSubroutine.h"
#include "slang/ast/ValuePath.h"
#include "slang/ast/expressions/Operator.h"
#include "slang/ast/expressions/SelectExpressions.h"
#include "slang/ast/symbols/SubroutineSymbols.h"
#include "slang/ast/symbols/VariableSymbols.h"

using namespace slang;
using namespace slang::ast;
using namespace slang::analysis;

namespace no_fork_racing {
static bool isThisVar(const Symbol& sym) {
    auto scope = sym.getParentScope();
    auto sub = scope ? scope->asSymbol().as_if<SubroutineSymbol>() : nullptr;
    return sub && sub->thisVar == &sym;
}

// ValuePath roots `this.x` at the compiler-generated `this` variable, key it on property `x`, the same as an implicit `x`.
struct Write {
    ValuePath path;
    const Symbol* key;

    explicit Write(const ValuePath& path) : path(path), key(path.rootSymbol()) {
        if (!key || !isThisVar(*key))
            return;

        for (auto& elem : path) {
            auto access = elem.as_if<MemberAccessExpression>();
            if (access && &access->value() == path.rootExpr) {
                key = &access->member;
                break;
            }
        }
    }

    // Policy: warn when overlap is provable (constant selections) or syntactically identical
    // (same dynamic expression). Distinct dynamic indices are assumed to address different
    // elements, and a constant against a dynamic index is not reported.
    bool mayOverlap(const Write& other, EvalContext& evalCtx) const {
        if (!key || key != other.key)
            return false;

        auto a = selections();
        auto b = other.selections();
        for (size_t i = 0; i < std::min(a.size(), b.size()); ++i) {
            if (!selectionsOverlap(*a[i], *b[i], evalCtx))
                return false;
        }

        // Identical, or one encloses the other (`d` vs `d[i]`).
        return true;
    }

private:
    // Selections below the key, outermost root first.
    std::vector<const Expression*> selections() const {
        std::vector<const Expression*> result;
        for (auto& elem : path) {
            if (elem.kind == ExpressionKind::NamedValue ||
                elem.kind == ExpressionKind::HierarchicalValue)
                break;

            if (auto member = elem.as_if<MemberAccessExpression>(); member && &member->member == key)
                break;

            result.push_back(&elem);
        }

        std::ranges::reverse(result);
        return result;
    }

    static bool selectionsOverlap(const Expression& a, const Expression& b, EvalContext& evalCtx) {
        if (a.kind != b.kind)
            return true;

        if (a.kind == ExpressionKind::MemberAccess)
            return &a.as<MemberAccessExpression>().member == &b.as<MemberAccessExpression>().member;

        if (a.kind != ExpressionKind::ElementSelect && a.kind != ExpressionKind::RangeSelect)
            return true;

        auto left = a.evalSelector(evalCtx, /* enforceBounds */ true);
        auto right = b.evalSelector(evalCtx, /* enforceBounds */ true);
        if (left && right)
            return left->lower() <= right->upper() && right->lower() <= left->upper();

        // Constant against dynamic: not reported.
        if (left || right)
            return false;

        if (a.kind == ExpressionKind::ElementSelect) {
            return a.as<ElementSelectExpression>().selector().isEquivalentTo(
                b.as<ElementSelectExpression>().selector());
        }

        // Indexed ranges from the same dynamic base always share that element,
        // whatever their widths.
        return a.as<RangeSelectExpression>().left().isEquivalentTo(
            b.as<RangeSelectExpression>().left());
    }
};

// Whether a call writes its i-th argument by reference. Output and inout args are already
// bound as lvalue-arg assignments in the AST.
static bool writesArg(const CallExpression& call, size_t i) {
    if (auto sysCall = std::get_if<CallExpression::SystemCallInfo>(&call.subroutine)) {
        // aa.first(k) and friends flag k as by-ref, but mutating methods such as q.push_back(x)
        // or s.putc(i, c) get no flag for their receiver (args[0]).
        using enum parsing::KnownSystemName;
        static constexpr parsing::KnownSystemName mutators[] = {
            Delete,  PushBack, PushFront, PopBack, PopFront, Insert, Sort,   Rsort,
            Shuffle, Reverse,  Putc,      IToA,    HexToA,   OctToA, BinToA, RealToA};

        auto& sub = *sysCall->subroutine;
        return sub.isArgByRef(i) ||
               (i == 0 && std::ranges::find(mutators, sub.knownNameId) != std::end(mutators));
    }

    auto args = std::get<const SubroutineSymbol*>(call.subroutine)->getArguments();
    if (i >= args.size())
        return false;

    auto& formal = *args[i];
    return formal.direction == ArgumentDirection::Ref && !formal.flags.has(VariableFlags::Const);
}

struct MainVisitor : TidyVisitor, ASTVisitor<MainVisitor, VisitFlags::StatementsCanonical> {
    MainVisitor(Diagnostics& diagnostics, const RootSymbol& root) :
        TidyVisitor(diagnostics), evalCtx(ASTContext(root, LookupLocation::max)) {}

    void handle(const BlockStatement& stmt) {
        visitDefault(stmt);

        if (stmt.blockKind != StatementBlockKind::Sequential) {
            NEEDS_SKIP_STATEMENT(stmt)
            checkFork(stmt);
        }
    }

private:
    EvalContext evalCtx;
    std::vector<SourceLocation> emitted;

    bool markEmitted(SourceLocation location) {
        if (std::ranges::find(emitted, location) != emitted.end())
            return false;

        emitted.push_back(location);
        return true;
    }

    std::vector<Write> collectWrites(const Statement& child) {
        std::vector<Write> writes;
        auto record = [&](const Expression& lhs) {
            // Skip selectors: `a[i++]` writes i, which the traversal finds on its own.
            ValuePath::visitPaths(
                lhs, evalCtx, [&](const ValuePath& path) { writes.emplace_back(path); },
                /* skipSelectors */ true);
        };

        child.visit(makeVisitor(
            [&](auto& v, const AssignmentExpression& expr) {
                v.visitDefault(expr);
                record(expr.left());
            },
            [&](auto& v, const UnaryExpression& expr) {
                v.visitDefault(expr);
                if (OpInfo::isLValue(expr.op))
                    record(expr.operand());
            },
            [&](auto& v, const CallExpression& expr) {
                v.visitDefault(expr);
                auto args = expr.arguments();
                for (size_t i = 0; i < args.size(); ++i) {
                    if (writesArg(expr, i))
                        record(*args[i]);
                }
            },
            // VariableDeclStatement has no visitExprs, no `automatic int t = x++;`.
            [&](auto& v, const VariableDeclStatement& stmt) {
                if (auto init = stmt.symbol.getInitializer())
                    init->visit(v);
            }));
        return writes;
    }

    void checkFork(const BlockStatement& fork) {
        evalCtx.reset();

        const Statement* body = &fork.body;
        auto children = body->kind == StatementKind::List
                            ? body->as<StatementList>().list
                            : std::span<const Statement* const>(&body, 1);

        std::unordered_map<const Symbol*, std::vector<Write>> seen;
        std::unordered_set<const Symbol*> reported;
        for (auto child : children) {
            if (child->kind == StatementKind::VariableDeclaration)
                continue;

            auto writes = collectWrites(*child);
            for (auto& write : writes) {
                if (!write.key || reported.contains(write.key))
                    continue;

                auto it = seen.find(write.key);
                if (it == seen.end() ||
                    !std::ranges::any_of(it->second, [&](const Write& prev) {
                        return prev.mayOverlap(write, evalCtx);
                    }))
                    continue;

                if (markEmitted(write.path.fullExpr->sourceRange.start())) {
                    diags.add(diag::NoForkRacing, write.path.fullExpr->sourceRange)
                        << write.key->name;
                }
                reported.insert(write.key);
            }

            // Only earlier children contribute to comparisons for this child.
            for (auto& write : writes) {
                if (write.key)
                    seen[write.key].push_back(std::move(write));
            }
        }
    }
};

} // namespace no_fork_racing

using namespace no_fork_racing;

class NoForkRacing : public TidyCheck {
public:
    [[maybe_unused]] explicit NoForkRacing(TidyKind kind,
                                           std::optional<slang::DiagnosticSeverity> severity) :
        TidyCheck(kind, severity) {}

    bool check(const RootSymbol& root, const AnalysisManager&) override {
        MainVisitor visitor(diagnostics, root);
        root.visit(visitor);
        return diagnostics.empty();
    }

    DiagCode diagCode() const override { return diag::NoForkRacing; }

    std::string diagString() const override {
        return "'{}' may be written by multiple concurrent fork children";
    }

   DiagnosticSeverity diagDefaultSeverity() const override {
       return DiagnosticSeverity::Warning;
   }

    std::string name() const override { return "NoForkRacing"; }

    std::string shortDescription() const override {
        return "Warns about overlapping writes from different children of a fork.";
    }

    std::string description() const override {
        return shortDescription() +
               " Reports potential conflicts without proving simultaneous execution "
               "or accounting for synchronization. Analysis is intraprocedural and "
              "does not resolve handle aliases or writes inside called subroutines.\n\n" +
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


REGISTER(NoForkRacing, NoForkRacing, TidyKind::Synthesis)
