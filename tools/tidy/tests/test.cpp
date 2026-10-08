/*
#include "ASTHelperVisitors.h"
#include "TidyDiags.h"
#include "TidyFactory.h"
#include "fmt/color.h"

using namespace slang;
using namespace slang::ast;
using namespace slang::analysis;

namespace child_called_before_start_check {

struct CalledSubroutineCollector :
    public ASTVisitor<CalledSubroutineCollector, VisitFlags::AllCanonical> {

    std::unordered_set<const SubroutineSymbol*> callees;

    void handle(const CallExpression& call) {
        if (call.subroutine.index() == 0) {
            if (const auto* sub = std::get<0>(call.subroutine))
                callees.insert(sub);
        }

        visitDefault(call);
    }
};

static bool WaitingChildrenReleased(const Statement& stmt) {
    switch (stmt.kind) {
        case StatementKind::Timed:
        case StatementKind::WaitFork:
        case StatementKind::Wait:
        case StatementKind::DisableFork:
            return true;

        case StatementKind::Block: {
            const auto& block = stmt.as<BlockStatement>();
            return block.blockKind == StatementBlockKind::JoinAll ||
                   block.blockKind == StatementBlockKind::JoinAny;
        }

        case StatementKind::ExpressionStatement: {
            const auto& exprStmt = stmt.as<ExpressionStatement>();
            if (exprStmt.expr.kind != ExpressionKind::Call)
                return false;

            const auto& call = exprStmt.expr.as<CallExpression>();
            if (call.subroutine.index() != 0)
                return false;

            const auto* sub = std::get<0>(call.subroutine);
            if (!sub)
                return false;

            if (sub->name != "get" && sub->name != "put" && sub->name != "peek")
                return false;

            const auto* parentScope = sub->getParentScope();
            if (!parentScope)
                return false;

            const auto& parentSym = parentScope->asSymbol();
            if (parentSym.kind != SymbolKind::ClassType)
                return false;

            return parentSym.name == "semaphore" || parentSym.name == "mailbox"; 
        }

        default:
            return false;
    }
}

struct MatchingCallFinder :
    public ASTVisitor<MatchingCallFinder, VisitFlags::AllCanonical> {

    const std::unordered_set<const SubroutineSymbol*>& targets;
    const CallExpression* match = nullptr;
    const SubroutineSymbol* matchedSubroutine = nullptr;

    explicit MatchingCallFinder(
        const std::unordered_set<const SubroutineSymbol*>& targets) :
        targets(targets) {}

    void handle(const CallExpression& call) {
        if (match)
            return;

        if (call.subroutine.index() == 0) {
            if (const auto* sub = std::get<0>(call.subroutine)) {
                if (targets.contains(sub)) {
                    match = &call;
                    matchedSubroutine = sub;
                    return;
                }
            }
        }

        visitDefault(call);
    }
};

struct MainVisitor :
    public TidyVisitor, ASTVisitor<MainVisitor, VisitFlags::AllCanonical> {

    MainVisitor(Diagnostics& diagnostics) :
        TidyVisitor(diagnostics) {}

    void handle(const StatementList& stmtList) {
        const auto& stmts = stmtList.list;

        for (size_t i = 0; i < stmts.size(); ++i) {
            if (stmts[i]->kind != StatementKind::Block)
                continue;

            const auto& forkJoin = stmts[i]->as<BlockStatement>();
            if (forkJoin.blockKind != StatementBlockKind::JoinNone)
                continue;

            CalledSubroutineCollector forkCallCollector;
            forkJoin.body.visit(forkCallCollector);
            if (forkCallCollector.callees.empty())
                continue;

            for (size_t j = i + 1; j < stmts.size(); ++j) {
                if (WaitingChildrenReleased(*stmts[j]))
                    break;

                MatchingCallFinder finder(forkCallCollector.callees);
                stmts[j]->visit(finder);

                if (finder.match) {
                    diags.add(diag::ChildCalledBeforeStart,
                              finder.match->sourceRange.start())
                        << finder.matchedSubroutine->name;
                    break;
                }
            }
        }

        visitDefault(stmtList);
    }
};

} // namespace child_called_before_start_check

using namespace child_called_before_start_check;

class ChildCalledBeforeStartCheck : public TidyCheck {
public:
    explicit ChildCalledBeforeStartCheck(
        TidyKind kind,
        std::optional<slang::DiagnosticSeverity> severity) :
        TidyCheck(kind, severity) {}

    bool check(const RootSymbol& root, const AnalysisManager&) override {
        MainVisitor visitor(diagnostics);
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

    std::string name() const override {
        return "ChildCalledBeforeStartCheck";
    }

    std::string description() const override {
        return "Checks for subroutine calls in a parent process that also occur within a "
               "fork..join_none child before the parent reaches a scheduling point. "
               "At the point join_none returns, child threads are scheduled but have not yet "
               "begun execution. Calling the same subroutine from the parent before yielding "
               "can therefore violate code that relies on the child call occurring first.\n\n" +
               fmt::format(fmt::emphasis::italic,
                           "// BAD: child not yet started\n"
                           "fork\n"
                           "    child_task();\n"
                           "join_none\n"
                           "child_task(); // warning: child may not have started\n\n") +
               "Insert a scheduling point to suppress:\n\n" +
               fmt::format(fmt::emphasis::italic,
                           "// GOOD: child has had a chance to start\n"
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



*/

















/*



TEST_CASE("ChildCalledBeforeStartCheck: call after delay in conditional -negative") {
    auto result = runCheckTest("ChildCalledBeforeStartCheck", R"(
module conditional_delay_no_warn;
    bit condition = 1;
    task child_task(); endtask

    initial begin
        fork
            child_task();
        join_none

        if (condition) begin
            #0;
            child_task();
        end
    end
endmodule
)");
    // Expected: no warning. Current finder likely traverses past #0.
    CHECK(result);
}
    
TEST_CASE("ChildCalledBeforeStartCheck: same method different objects -negative") {
    auto result = runCheckTest("ChildCalledBeforeStartCheck", R"(
module different_objects_no_warn;
    class Worker;
        task run(); endtask
    endclass

    Worker a = new();
    Worker b = new();

    initial begin
        fork
            a.run();
        join_none

        b.run();
    end
endmodule
)");
    // Expected: no warning IF object identity is part of the rule.
    // Current implementation likely warns.
    CHECK(result);
}


TEST_CASE("ChildCalledBeforeStartCheck: wait true does not suspend -positive") {
    auto result = runCheckTest("ChildCalledBeforeStartCheck", R"(
module wait_true_warn;
    bit ready = 1;
    task child_task(); endtask

    initial begin
        fork
            child_task();
        join_none
        wait (ready);
        child_task();
    end
endmodule
)");
    // Expected: warning. Current implementation likely misses it.
    CHECK_FALSE(result);
}


TEST_CASE("ChildCalledBeforeStartCheck: mailbox get can return immediately -positive") {
    auto result = runCheckTest("ChildCalledBeforeStartCheck", R"(
module mailbox_get_warn;
    mailbox mb = new();
    int value;
    task child_task(); endtask

    initial begin
        mb.put(42);
        fork
            child_task();
        join_none
        mb.get(value);
        child_task();
    end
endmodule
)");
    // Expected: warning. The mailbox already contains an item.
    CHECK_FALSE(result);
}

    

TEST_CASE("ChildCalledBeforeStartCheck: call after delay inside nested block -negative") {
    auto result = runCheckTest("ChildCalledBeforeStartCheck", R"(
module nested_delay_no_warn;
    task child_task(); endtask

    initial begin
        fork
            child_task();
        join_none

        begin
            #0;
            child_task();
        end
    end
endmodule
)");
    // Expected: no warning. Current finder likely ignores the inner delay.
    CHECK(result);
}


TEST_CASE("ChildCalledBeforeStartCheck: scheduling point inside block before matching call -negative") {
    auto result = runCheckTest("ChildCalledBeforeStartCheck", R"(
module nested_release_clean;
    task child_task(); endtask

    initial begin
        fork
            child_task();
        join_none
        begin
            #0;
            child_task();
        end
    end
endmodule
)");
    CHECK(result);
}


TEST_CASE("ChildCalledBeforeStartCheck: fork inside nested begin-end -positive") {
    auto result = runCheckTest("ChildCalledBeforeStartCheck", R"(
module nested_fork_warn;
    task child_task(); endtask

    initial begin
        begin
            fork
                child_task();
            join_none
        end
        child_task();
    end
endmodule
)");
    // Expected: warning. Predicted current result: no warning.
    CHECK_FALSE(result);
}


TEST_CASE("ChildCalledBeforeStartCheck: fork inside if branch -positive") {
    auto result = runCheckTest("ChildCalledBeforeStartCheck", R"(
module conditional_fork_warn;
    bit condition = 1;
    task child_task(); endtask

    initial begin
        if (condition)
            fork
                child_task();
            join_none

        child_task();
    end
endmodule
)");
    // Expected: warning on the path where condition is true.
    CHECK_FALSE(result);
}



TEST_CASE("ChildCalledBeforeStartCheck: generate if zero -negative") {
    auto result = runCheckTest("ChildCalledBeforeStartCheck", R"(
module generate_if_zero_no_warn;
    task child_task(); endtask

    if (0) begin : disabled_generate
        initial begin
            fork
                child_task();
            join_none
            child_task();
        end
    end
endmodule
)");
    // Expected: no warning. Tests elaboration-time branch elimination.
    CHECK(result);
}



TEST_CASE("ChildCalledBeforeStartCheck: fork inside else branch -positive") {
    auto result = runCheckTest("ChildCalledBeforeStartCheck", R"(
module else_fork_warn;
    bit condition = 0;
    task child_task(); endtask

    initial begin
        if (condition)
            $display("other branch");
        else
            fork
                child_task();
            join_none

        child_task();
    end
endmodule
)");
    // Expected: warning on the else path.
    CHECK_FALSE(result);
}





TEST_CASE("ChildCalledBeforeStartCheck: semaphore get can return immediately -positive") {
    auto result = runCheckTest("ChildCalledBeforeStartCheck", R"(
module semaphore_get_warn;
    semaphore sem = new(1);
    task child_task(); endtask

    initial begin
        fork
            child_task();
        join_none
        sem.get(1);
        child_task();
    end
endmodule
)");
    // Expected: warning. A key is already available.
    CHECK_FALSE(result);
}


TEST_CASE("ChildCalledBeforeStartCheck: not only look for a JoinNone block directly within a StatementList. -positive") {
    auto result = runCheckTest("ChildCalledBeforeStartCheck", R"(
module inbegin;
    task child_task(); endtask

    initial begin
        begin
            fork
                child_task();
            join_none
        end
        child_task();
    end
endmodule
)");
    CHECK_FALSE(result);
}




*/