// SPDX-FileCopyrightText: Michael Popoloski
// SPDX-License-Identifier: MIT

#include "ASTHelperVisitors.h"
#include "TidyDiags.h"
#include "fmt/color.h"

#include <algorithm>
#include <iterator>
#include <optional>
#include <ranges>
#include <string>
#include <unordered_set>
#include <variant>
#include <vector>

#include "slang/analysis/AnalysisManager.h"
#include "slang/ast/EvalContext.h"
#include "slang/ast/SemanticFacts.h"
#include "slang/ast/SystemSubroutine.h"
#include "slang/ast/ValuePath.h"
#include "slang/ast/expressions/Operator.h"
#include "slang/ast/symbols/SubroutineSymbols.h"
#include "slang/ast/symbols/VariableSymbols.h"

using namespace slang;
using namespace slang::ast;
using namespace slang::analysis;

namespace no_fork_racing {

static bool isThisVar(const Symbol& symbol) {
    auto var = symbol.as_if<VariableSymbol>();
    return var && var->flags.has(VariableFlags::CompilerGenerated) && var->name == "this";
}

// ValuePath roots `x` at the property but `this.x` at the `this` variable, since the
// member access goes through a handle. Normalize both to the property.
struct WriteKey {
    const Symbol* symbol = nullptr;
    bool viaThis = false;
};

static WriteKey keyOf(const ValuePath& path) {
    const Symbol* root = path.rootSymbol();
    if (!root || !isThisVar(*root))
        return {root, false};

    // Path iteration runs outermost -> root, so the element just above the root is
    // the `this.<member>` access.
    const Expression* above = nullptr;
    for (auto& elem : path) {
        if (&elem == path.rootExpr)
            break;
        above = &elem;
    }

    if (auto access = above ? above->as_if<MemberAccessExpression>() : nullptr)
        return {&access->member, true};
    return {root, false};
}

struct Write {
    ValuePath path;
    WriteKey key;
};

static bool mayOverlap(const Write& a, const Write& b) {
    if (!a.key.symbol || a.key.symbol != b.key.symbol)
        return false;

    // Same spelling on both sides: ValuePath compares them precisely.
    if (a.key.viaThis == b.key.viaThis)
        return a.path.overlaps(b.path);

    // `x[...]` vs `this.x[...]`: the paths have different roots, so be conservative.
    return true;
}

struct WriteCollector : ASTVisitor<WriteCollector, VisitFlags::AllGood> {
    explicit WriteCollector(EvalContext& evalCtx) : evalCtx(evalCtx) {}

    std::vector<Write> writes;

    // Also covers output/inout arguments, which slang binds as lvalue-arg assignments.
    void handle(const AssignmentExpression& expr) {
        visitDefault(expr);
        record(expr.left());
    }

    void handle(const UnaryExpression& expr) {
        visitDefault(expr);
        if (OpInfo::isLValue(expr.op))
            record(expr.operand());
    }

    void handle(const CallExpression& call) {
        visitDefault(call);
        const auto args = call.arguments();

        if (auto sysCall = std::get_if<CallExpression::SystemCallInfo>(&call.subroutine)) {
            // Built-ins such as aa.first(k) write through by-ref arguments.
            const auto& sub = *sysCall->subroutine;
            for (size_t i = 0; i < args.size(); ++i) {
                if (!sub.isArgUnevaluated(i) && sub.isArgByRef(i))
                    record(*args[i]);
            }
            return;
        }

        const auto* subroutine = std::get<const SubroutineSymbol*>(call.subroutine);
        if (!subroutine)
            return;

        // Non-const ref arguments get no assignment expression but still act as lvalues.
        const auto formals = subroutine->getArguments();
        const auto count = std::min(formals.size(), args.size());
        for (size_t i = 0; i < count; ++i) {
            const auto& formal = *formals[i];
            if (formal.direction == ArgumentDirection::Ref &&
                !formal.flags.has(VariableFlags::Const))
                record(*args[i]);
        }
    }

private:
    EvalContext& evalCtx;

    void record(const Expression& lhs) {
        // Skip selector operands as write targets; AST traversal
        // separately records side effects such as a[i++].
        ValuePath::visitPaths(
            lhs, evalCtx,
            [&](const ValuePath& path) { writes.push_back({path, keyOf(path)}); },
            /* skipSelectors */ true);
    }
};

struct MainVisitor : TidyVisitor, ASTVisitor<MainVisitor, VisitFlags::AllCanonical> {
    MainVisitor(Diagnostics& diagnostics, const RootSymbol& root) :
        TidyVisitor(diagnostics), evalCtx(ASTContext(root, LookupLocation::max)) {}

    void handle(const BlockStatement& stmt) {
        if (stmt.blockKind != StatementBlockKind::Sequential) {
            NEEDS_SKIP_STATEMENT(stmt)
            checkFork(stmt);
        }
        visitDefault(stmt);
    }

private:
    EvalContext evalCtx;

    void checkFork(const BlockStatement& fork) {
        evalCtx.reset();

        std::vector<const Statement*> children;
        if (const auto* list = fork.body.as_if<StatementList>())
            children.assign(list->list.begin(), list->list.end());
        else
            children.push_back(&fork.body);

        std::vector<Write> seen;
        std::unordered_set<const Symbol*> reported;

        for (const auto* child : children) {
            if (child->kind == StatementKind::VariableDeclaration)
                continue;
            
            WriteCollector collector(evalCtx);
            child->visit(collector);

            for (const auto& write : collector.writes) {
                const auto* key = write.key.symbol;
                if (!key || reported.contains(key))
                    continue;

                if (std::ranges::any_of(seen,
                                        [&](const Write& prev) { return mayOverlap(prev, write); })) {
                    diags.add(diag::NoForkRacing, write.path.fullExpr->sourceRange) << key->name;
                    reported.insert(key);
                }
            }

            // Only earlier children contribute to comparisons for this child.
            std::ranges::move(collector.writes, std::back_inserter(seen));
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