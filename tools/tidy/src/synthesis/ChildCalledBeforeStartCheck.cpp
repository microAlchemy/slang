// SPDX-FileCopyrightText: Michael Popoloski
// SPDX-License-Identifier: MIT

#include "ASTHelperVisitors.h"
#include "TidyDiags.h"
#include "TidyFactory.h"
#include "fmt/color.h"

#include <optional>
#include <span>
#include <string_view>
#include <unordered_set>

#include "slang/ast/Compilation.h"
#include "slang/ast/EvalContext.h"

using namespace slang;
using namespace slang::ast;
using namespace slang::analysis;
namespace child_called_before_start_check {

namespace {
bool shouldVisitBranch(const ConditionalStatement& stmt, EvalContext& evalCtx,
                       bool goodBranch) {
    bool knownFalse = false;
    bool knownTrue = true;
    for (const auto& cond : stmt.conditions) {
        if (cond.pattern) {
            knownTrue = false;
            continue;
        }
        auto cv = cond.expr->eval(evalCtx);
        if (!cv) {
            knownTrue = false;
            continue;
        }
        if (!cv.isTrue()) {
            knownFalse = true;
            break;
        }
    }
    return goodBranch ? !knownFalse : (knownFalse || !knownTrue);
}

bool skipsBody(const RepeatLoopStatement& loop, EvalContext& evalCtx) {
    auto cv = loop.count.eval(evalCtx);
    return cv.isInteger() && cv.integer() == 0;
}

bool endsSequence(const Statement& stmt) {
    return stmt.kind == StatementKind::Return ||
           stmt.kind == StatementKind::ForeverLoop;
}

struct CalledSubroutineCollector :
    public ASTVisitor<CalledSubroutineCollector, VisitFlags::AllCanonical> {

    EvalContext& evalCtx;
    std::unordered_set<const SubroutineSymbol*> callees;

    explicit CalledSubroutineCollector(EvalContext& evalCtx) : evalCtx(evalCtx) {}

    void handle(const CallExpression& call) {
        if (call.subroutine.index() == 0) {
            if (const auto* sub = std::get<0>(call.subroutine))
                callees.insert(sub);
        }
        visitDefault(call);
    }

    void handle(const VariableDeclStatement& decl) {
        if (decl.symbol.lifetime == VariableLifetime::Static)
            return;
        if (const auto* init = decl.symbol.getInitializer())
            init->visit(*this);
    }

    void handle(const RepeatLoopStatement& loop) {
        loop.count.visit(*this);
        if (!skipsBody(loop, evalCtx))
            loop.body.visit(*this);
    }

    void handle(const ConditionalStatement& stmt) {
        for (const auto& cond : stmt.conditions) {
            cond.expr->visit(*this);
            if (cond.pattern)
                cond.pattern->visit(*this);
        }
        if (shouldVisitBranch(stmt, evalCtx, true))
            stmt.ifTrue.visit(*this);
        if (stmt.ifFalse && shouldVisitBranch(stmt, evalCtx, false))
            stmt.ifFalse->visit(*this);
    }
};

struct MatchingCallFinder : public ASTVisitor<MatchingCallFinder, VisitFlags::AllCanonical> {
    const std::unordered_set<const SubroutineSymbol*>& targets;
    EvalContext& evalCtx;
    const CallExpression* match = nullptr;

    MatchingCallFinder(const std::unordered_set<const SubroutineSymbol*>& targets,
                       EvalContext& evalCtx) :
        targets(targets), evalCtx(evalCtx) {}

    void handle(const CallExpression& call) {
        if (match)
            return;

        if (call.subroutine.index() == 0) {
            const auto* sub = std::get<0>(call.subroutine);
            if (sub && targets.contains(sub)) {
                match = &call;
                return;
            }
        }

        visitDefault(call);
    }

    void handle(const StatementList& list) {
        for (const auto* stmt : list.list) {
            stmt->visit(*this);
            if (match || endsSequence(*stmt))
                break;
        }
    }

    void handle(const BlockStatement& block) {
        if (match)
            return;
        if (block.blockKind == StatementBlockKind::Sequential) {
            visitDefault(block);
            return;
        }
        if (block.body.kind != StatementKind::List)
            return;
        for (const auto* s : block.body.as<StatementList>().list) {
            if (s->kind != StatementKind::VariableDeclaration)
                break;
            s->visit(*this);
        }
    }

    void handle(const VariableDeclStatement& decl) {
        if (match || decl.symbol.lifetime == VariableLifetime::Static)
            return;
        if (const auto* init = decl.symbol.getInitializer())
            init->visit(*this);
    }

    void handle(const RepeatLoopStatement& loop) {
        if (match)
            return;
        loop.count.visit(*this);
        if (!match && !skipsBody(loop, evalCtx))
            loop.body.visit(*this);
    }

    void handle(const ConditionalStatement& stmt) {
        if (match)
            return;
        for (const auto& cond : stmt.conditions) {
            cond.expr->visit(*this);
            if (cond.pattern)
                cond.pattern->visit(*this);
        }
        if (shouldVisitBranch(stmt, evalCtx, true))
            stmt.ifTrue.visit(*this);
        if (stmt.ifFalse && shouldVisitBranch(stmt, evalCtx, false))
            stmt.ifFalse->visit(*this);
    }
};

bool ChildrenReleased(const Statement& stmt, MatchingCallFinder& preSuspension) {
    auto& evalCtx = preSuspension.evalCtx;
    switch (stmt.kind) {
        case StatementKind::WaitFork:
        case StatementKind::DisableFork:
            return true;
        case StatementKind::Block: {
            const auto& block = stmt.as<BlockStatement>();
            if (block.blockKind != StatementBlockKind::JoinAll &&
                block.blockKind != StatementBlockKind::JoinAny) {
                return false;
            }
            if (block.blockKind == StatementBlockKind::JoinAny &&
                block.body.kind == StatementKind::List &&
                block.body.as<StatementList>().list.empty()) {
                return false;
            }
            stmt.visit(preSuspension);
            return true;
        }
        case StatementKind::Wait: {
            const auto& waitStmt = stmt.as<WaitStatement>();
            auto cv = waitStmt.cond.eval(evalCtx);
            waitStmt.cond.visit(preSuspension);
            return cv && cv.isFalse();
        }
        case StatementKind::Timed: {
            const auto& timing = stmt.as<TimedStatement>().timing;
            bool suspends = false;
            switch (timing.kind) {
                case TimingControlKind::Delay:
                case TimingControlKind::SignalEvent:
                case TimingControlKind::EventList:
                case TimingControlKind::ImplicitEvent:
                    suspends = true;
                    break;
                case TimingControlKind::CycleDelay: {
                    auto cv = timing.as<CycleDelayControl>().expr.eval(evalCtx);
                    if (cv.isInteger()) {
                        auto count = cv.integer().as<int64_t>();
                        suspends = count && *count > 0;
                    }
                    break;
                }
                default:
                    break;
            }
            if (!suspends)
                return false;
            timing.visit(preSuspension);
            return true;
        }
        case StatementKind::RepeatLoop: {
            const auto& loop = stmt.as<RepeatLoopStatement>();
            auto cv = loop.count.eval(evalCtx);
            if (!cv.isInteger())
                return false;
            auto count = cv.integer().as<int64_t>();
            if (!count || *count <= 0)
                return false;
            if (!ChildrenReleased(loop.body, preSuspension))
                return false;
            loop.count.visit(preSuspension);
            return true;
        }
        default:
            return false;
    }
}
} // namespace

struct MainVisitor : public TidyVisitor, ASTVisitor<MainVisitor, VisitFlags::AllCanonical> {
    EvalContext& evalCtx;

    MainVisitor(Diagnostics& diagnostics, EvalContext& evalCtx) :
        TidyVisitor(diagnostics), evalCtx(evalCtx) {}

    void reportMatch(const CallExpression& call) {
        const auto* sub = std::get<0>(call.subroutine);
        diags.add(diag::ChildCalledBeforeStart, call.sourceRange.start())
            << sub->name;
    }

    void checkFork(const BlockStatement& fork,
                   std::span<const Statement* const> following) {
        CalledSubroutineCollector forkCallCollector(evalCtx);
        if (fork.body.kind == StatementKind::List) {
            bool inLeadingDecls = true;
            for (const auto* s : fork.body.as<StatementList>().list) {
                if (inLeadingDecls && s->kind == StatementKind::VariableDeclaration)
                    continue;
                inLeadingDecls = false;
                s->visit(forkCallCollector);
            }
        }
        else {
            fork.body.visit(forkCallCollector);
        }
        if (forkCallCollector.callees.empty())
            return;
        for (const auto* stmt : following) {
            MatchingCallFinder finder(forkCallCollector.callees, evalCtx);
            const bool released = ChildrenReleased(*stmt, finder);
            if (!released)
                stmt->visit(finder);
            if (finder.match) {
                reportMatch(*finder.match);
                break;
            }
            if (released || endsSequence(*stmt))
                break;
        }
    }

    void handle(const StatementList& stmtList) {
        const auto& stmts = stmtList.list;
        for (size_t i = 0; i < stmts.size(); ++i) {
            if (stmts[i]->kind != StatementKind::Block)
                continue;
            const auto& forkJoin = stmts[i]->as<BlockStatement>();
            if (forkJoin.blockKind != StatementBlockKind::JoinNone)
                continue;
            checkFork(forkJoin, std::span<const Statement* const>(
                                    stmts.data() + i + 1, stmts.size() - i - 1));
        }
        visitDefault(stmtList);
    }

    void handle(const ConditionalStatement& stmt) {
        if (shouldVisitBranch(stmt, evalCtx, true))
            stmt.ifTrue.visit(*this);
        if (stmt.ifFalse && shouldVisitBranch(stmt, evalCtx, false))
            stmt.ifFalse->visit(*this);
    }
};

} // namespace child_called_before_start_check

using namespace child_called_before_start_check;

class ChildCalledBeforeStartCheck : public TidyCheck {
public:
    explicit ChildCalledBeforeStartCheck(TidyKind kind,
                                         std::optional<slang::DiagnosticSeverity> severity) :
        TidyCheck(kind, severity) {}

    bool check(const RootSymbol& root, const AnalysisManager&) override {
        EvalContext evalCtx{ASTContext(root, LookupLocation::max)};
        evalCtx.pushEmptyFrame();
        MainVisitor visitor(diagnostics, evalCtx);
        root.visit(visitor);
        return diagnostics.empty();
    }

    DiagCode diagCode() const override {
        return diag::ChildCalledBeforeStart;
    }

    std::string diagString() const override {
        return "subroutine '{}' called in the parent before a scheduling point after "
               "fork..join_none; the child thread may not have started execution yet";
    }

    DiagnosticSeverity diagDefaultSeverity() const override {
        return DiagnosticSeverity::Warning;
    }

    std::string name() const override { return "ChildCalledBeforeStartCheck"; }

    std::string description() const override {
        return "Checks for subroutine calls in a parent process that also occur within a "
               "fork..join_none child before the parent reaches a scheduling point. "
               "At the point join_none returns, child threads are scheduled but have not yet "
               "begun execution. Calling the same subroutine from the parent before yielding "
               "can therefore violate code that relies on the child call occurring first.\n\n" +
               fmt::format(fmt::emphasis::italic, "// BAD: child not yet started\n"
                                                  "fork\n"
                                                  "    child_task();\n"
                                                  "join_none\n"
                                                  "child_task(); // warning: child may not have "
                                                  "started\n\n") +
               "Insert a scheduling point to suppress:\n\n" +
               fmt::format(fmt::emphasis::italic, "// GOOD: child has had a chance to start\n"
                                                  "fork\n"
                                                  "    child_task();\n"
                                                  "join_none\n"
                                                  "#0;\n"
                                                  "child_task(); // fine\n");
    }

    std::string shortDescription() const override {
        return "Child task called in parent before scheduling point after fork..join_none";
    }
};

REGISTER(ChildCalledBeforeStartCheck, ChildCalledBeforeStartCheck, TidyKind::Synthesis)
