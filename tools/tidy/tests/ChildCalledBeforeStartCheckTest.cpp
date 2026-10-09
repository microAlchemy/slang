// SPDX-FileCopyrightText: Michael Popoloski
// SPDX-License-Identifier: MIT

#include "Test.h"
#include "TidyFactory.h"
#include "TidyTest.h"

// POSITIVE CASES 

TEST_CASE("ChildCalledBeforeStartCheck: one fork thread called before scheduling point, sibling gated correctly -positive") {
    auto result = runCheckTest("ChildCalledBeforeStartCheck", R"(
module mixed_gating_warn;
    task task_a(); endtask
    task task_b(); endtask

    initial begin
        fork
            task_a();
            task_b();
        join_none
        task_a();
        #0;
        task_b();
    end
endmodule
)");
    CHECK_FALSE(result);
}


TEST_CASE("ChildCalledBeforeStartCheck: same subroutine called twice after fork, both before scheduling point -positive") {
    auto result = runCheckTest("ChildCalledBeforeStartCheck", R"(
module double_call_warn;
    task child_task(); endtask

    initial begin
        fork
            child_task();
        join_none
        child_task();
        child_task();
    end
endmodule
)");
    CHECK_FALSE(result);
}

TEST_CASE("ChildCalledBeforeStartCheck: fork thread body is multi-statement begin-end block -positive") {
    auto result = runCheckTest("ChildCalledBeforeStartCheck", R"(
module multistmt_body_warn;
    task child_task(); endtask
    function void helper(); endfunction

    initial begin
        fork
            begin
                helper();
                child_task();
            end
        join_none
        child_task();
    end
endmodule
)");
    CHECK_FALSE(result);
}

TEST_CASE("ChildCalledBeforeStartCheck: parameterized mailbox get() is a scheduling point -negative ") {
    auto result = runCheckTest("ChildCalledBeforeStartCheck", R"(
module parameterized_mailbox_clean;
    task child_task(); endtask
    mailbox #(int) mbox;

    initial begin
        fork
            child_task();
        join_none
        mbox.get();
        child_task();
    end
endmodule
)");
    CHECK(result);
}

TEST_CASE("ChildCalledBeforeStartCheck: queue get-like method is not a scheduling point -positive") {
    auto result = runCheckTest("ChildCalledBeforeStartCheck", R"(
module fake_get_warn;
    task child_task(); endtask

    class Fetcher;
        function void get(); endfunction
    endclass

    initial begin
        Fetcher f = new;
        fork
            child_task();
        join_none
        f.get();
        child_task();
    end
endmodule
)");
    CHECK_FALSE(result);
}

TEST_CASE("ChildCalledBeforeStartCheck: scheduling point nested inside if-block before call -positive") {
    auto result = runCheckTest("ChildCalledBeforeStartCheck", R"(
module nested_scheduling_warn;
    task child_task(); endtask
    logic cond;

    initial begin
        fork
            child_task();
        join_none
        if (cond) begin
            #0;
        end
        child_task();
    end
endmodule
)");
    CHECK_FALSE(result);
}


TEST_CASE("ChildCalledBeforeStartCheck: recursive task calling itself before scheduling point -positive") {
    auto result = runCheckTest("ChildCalledBeforeStartCheck", R"(
module recursive_warn;
    task recursive_task();
        recursive_task();
    endtask

    initial begin
        fork
            recursive_task();
        join_none
        recursive_task();
    end
endmodule
)");
    CHECK_FALSE(result);
}

TEST_CASE("ChildCalledBeforeStartCheck: same subroutine in fork and immediately after -positive") {
    auto result = runCheckTest("ChildCalledBeforeStartCheck", R"(
module basic_warn;
    task child_task(); endtask

    initial begin
        fork
            child_task();
        join_none
        child_task();
    end
endmodule
)");
    CHECK_FALSE(result);
}

TEST_CASE("ChildCalledBeforeStartCheck: two distinct subroutines in fork body both called before scheduling point -positive") {
    auto result = runCheckTest("ChildCalledBeforeStartCheck", R"(
module two_subs_warn;
    task task_a(); endtask
    task task_b(); endtask

    initial begin
        fork
            task_a();
            task_b();
        join_none
        task_a();
        task_b();
    end
endmodule
)");
    CHECK_FALSE(result);
}

TEST_CASE("ChildCalledBeforeStartCheck: matching call nested in expression -positive") {
    auto result = runCheckTest("ChildCalledBeforeStartCheck", R"(
module nested_call_warn;
    function int child_func();
        return 1;
    endfunction

    function void consume(int value);
    endfunction

    initial begin
        fork
            child_func();
        join_none
        consume(child_func());
    end
endmodule
)");
    CHECK_FALSE(result);
}

TEST_CASE("ChildCalledBeforeStartCheck: intervening non-scheduling function call does not suppress warning -positive") {
    auto result = runCheckTest("ChildCalledBeforeStartCheck", R"(
module intervening_nonblocking_warn;
    task child_task(); endtask
    function void helper(); endfunction

    initial begin
        fork
            child_task();
        join_none
        helper();
        child_task();
    end
endmodule
)");
    CHECK_FALSE(result);
}

TEST_CASE("ChildCalledBeforeStartCheck: nested fork..join_none outer child called before outer scheduling point -positive") {
    auto result = runCheckTest("ChildCalledBeforeStartCheck", R"(
module nested_fork_warn;
    task outer_task(); endtask

    initial begin
        fork
            outer_task();
        join_none
        fork
        join_none
        outer_task();
    end
endmodule
)");
    CHECK_FALSE(result);
}

// NEGATIVE CASES 

TEST_CASE("ChildCalledBeforeStartCheck: #0 scheduling point before call -negative ") {
    auto result = runCheckTest("ChildCalledBeforeStartCheck", R"(
module zero_delay_clean;
    task child_task(); endtask

    initial begin
        fork
            child_task();
        join_none
        #0;
        child_task();
    end
endmodule
)");
    CHECK(result);
}

TEST_CASE("ChildCalledBeforeStartCheck: nonzero delay scheduling point before call -negative ") {
    auto result = runCheckTest("ChildCalledBeforeStartCheck", R"(
module nonzero_delay_clean;
    task child_task(); endtask

    initial begin
        fork
            child_task();
        join_none
        #10;
        child_task();
    end
endmodule
)");
    CHECK(result);
}

TEST_CASE("ChildCalledBeforeStartCheck: @event scheduling point before call -negative ") {
    auto result = runCheckTest("ChildCalledBeforeStartCheck", R"(
module event_control_clean;
    task child_task(); endtask
    event e;

    initial begin
        fork
            child_task();
        join_none
        @e;
        child_task();
    end
endmodule
)");
    CHECK(result);
}

TEST_CASE("ChildCalledBeforeStartCheck: wait() scheduling point before call -positive ") {
    auto result = runCheckTest("ChildCalledBeforeStartCheck", R"(
module wait_clean;
    task child_task(); endtask
    logic sig;

    initial begin
        fork
            child_task();
        join_none
        wait(sig);
        child_task();
    end
endmodule
)");
    CHECK_FALSE(result);
}

TEST_CASE("ChildCalledBeforeStartCheck: wait fork scheduling point before call -negative ") {
    auto result = runCheckTest("ChildCalledBeforeStartCheck", R"(
module wait_fork_clean;
    task child_task(); endtask

    initial begin
        fork
            child_task();
        join_none
        wait fork;
        child_task();
    end
endmodule
)");
    CHECK(result);
}



TEST_CASE("ChildCalledBeforeStartCheck: matching call inside block before scheduling point -positive") {
    auto result = runCheckTest("ChildCalledBeforeStartCheck", R"(
module nested_call_before_release_warn;
    task child_task(); endtask

    initial begin
        fork
            child_task();
        join_none
        begin
            child_task();
            #0;
        end
    end
endmodule
)");
    CHECK_FALSE(result);
}

TEST_CASE("ChildCalledBeforeStartCheck: fork..join blocking is scheduling point -negative ") {
    auto result = runCheckTest("ChildCalledBeforeStartCheck", R"(
module fork_join_blocking_clean;
    task child_task(); endtask
    task other_task(); endtask

    initial begin
        fork
            child_task();
        join_none
        fork
            other_task();
        join
        child_task();
    end
endmodule
)");
    CHECK(result);
}

TEST_CASE("ChildCalledBeforeStartCheck: fork..join_any is scheduling point -negative ") {
    auto result = runCheckTest("ChildCalledBeforeStartCheck", R"(
module fork_join_any_clean;
    task child_task(); endtask
    task other_task(); endtask

    initial begin
        fork
            child_task();
        join_none
        fork
            other_task();
        join_any
        child_task();
    end
endmodule
)");
    CHECK(result);
}

TEST_CASE("ChildCalledBeforeStartCheck: different subroutine called after fork -negative ") {
    auto result = runCheckTest("ChildCalledBeforeStartCheck", R"(
module different_sub_clean;
    task child_task(); endtask
    task unrelated_task(); endtask

    initial begin
        fork
            child_task();
        join_none
        unrelated_task();
    end
endmodule
)");
    CHECK(result);
}

TEST_CASE("ChildCalledBeforeStartCheck: empty fork body -negative ") {
    auto result = runCheckTest("ChildCalledBeforeStartCheck", R"(
module empty_fork_clean;
    task child_task(); endtask

    initial begin
        fork
        join_none
        child_task();
    end
endmodule
)");
    CHECK(result);
}

TEST_CASE("ChildCalledBeforeStartCheck: fork..join does not trigger lint -negative ") {
    auto result = runCheckTest("ChildCalledBeforeStartCheck", R"(
module join_blocking_no_lint;
    task child_task(); endtask

    initial begin
        fork
            child_task();
        join
        child_task();
    end
endmodule
)");
    CHECK(result);
}

TEST_CASE("ChildCalledBeforeStartCheck: fork..join_any does not trigger lint -negative ") {
    auto result = runCheckTest("ChildCalledBeforeStartCheck", R"(
module join_any_no_lint;
    task child_task(); endtask

    initial begin
        fork
            child_task();
        join_any
        child_task();
    end
endmodule
)");
    CHECK(result);
}


TEST_CASE("ChildCalledBeforeStartCheck: fork with no subsequent call at all -negative ") {
    auto result = runCheckTest("ChildCalledBeforeStartCheck", R"(
module no_recall_clean;
    task child_task(); endtask

    initial begin
        fork
            child_task();
        join_none
    end
endmodule
)");
    CHECK(result);
}

TEST_CASE("ChildCalledBeforeStartCheck: scheduling point properly gates both of two fork threads -negative ") {
    auto result = runCheckTest("ChildCalledBeforeStartCheck", R"(
module both_gated_clean;
    task task_a(); endtask
    task task_b(); endtask

    initial begin
        fork
            task_a();
            task_b();
        join_none
        #0;
        task_a();
        task_b();
    end
endmodule
)");
    CHECK(result);
}

TEST_CASE("ChildCalledBeforeStartCheck: disable fork ends waiting child region -negative") {
    auto result = runCheckTest("ChildCalledBeforeStartCheck", R"(
module disable_fork_clean;
    task child_task(); endtask

    initial begin
        fork
            child_task();
        join_none
        disable fork;
        child_task();
    end
endmodule
)");
    CHECK(result);
}

TEST_CASE("ChildCalledBeforeStartCheck: fork..join present but different function called after, no re-call at all -negative ") {
    auto result = runCheckTest("ChildCalledBeforeStartCheck", R"(
module join_no_recall_clean;
    task child_task(); endtask
    task other_task(); endtask

    initial begin
        fork
            child_task();
        join
        other_task();
    end
endmodule
)");
    CHECK(result);
}

TEST_CASE("ChildCalledBeforeStartCheck: call to child task happens inside sibling fork thread, not sequential parent flow -negative ") {
    auto result = runCheckTest("ChildCalledBeforeStartCheck", R"(
module sibling_thread_call_clean;
    task child_task(); endtask

    initial begin
        fork
            child_task();
            child_task();
        join_none
    end
endmodule
)");
    CHECK(result);
}






TEST_CASE("ChildCalledBeforeStartCheck: semaphore get() is a scheduling point -negative ") {
    auto result = runCheckTest("ChildCalledBeforeStartCheck", R"(
module semaphore_clean;
    task child_task(); endtask
    semaphore sem;

    initial begin
        fork
            child_task();
        join_none
        sem.get();
        child_task();
    end
endmodule
)");
    CHECK_FALSE(result);
}

TEST_CASE("ChildCalledBeforeStartCheck: mailbox get() is a scheduling point -negative ") {
    auto result = runCheckTest("ChildCalledBeforeStartCheck", R"(
module mailbox_clean;
    task child_task(); endtask
    mailbox mbox;

    initial begin
        fork
            child_task();
        join_none
        mbox.get();
        child_task();
    end
endmodule
)");
    CHECK(result);
}






TEST_CASE("ChildCalledBeforeStartCheck: wait literal true does not suspend -positive") {
    auto result = runCheckTest("ChildCalledBeforeStartCheck", R"(
module wait_literal_true_warn;
    task child_task(); endtask

    initial begin
        fork
            child_task();
        join_none
        wait (1'b1);
        child_task();
    end
endmodule
)");
    CHECK_FALSE(result);
}



TEST_CASE("ChildCalledBeforeStartCheck: semaphore put does not suspend -positive") {
    auto result = runCheckTest("ChildCalledBeforeStartCheck", R"(
module semaphore_put_warn;
    semaphore sem = new(1);
    task child_task(); endtask

    initial begin
        fork
            child_task();
        join_none
        sem.put(1);
        child_task();
    end
endmodule
)");
    CHECK_FALSE(result);
}




TEST_CASE("ChildCalledBeforeStartCheck: if zero parent call -negative") {
    auto result = runCheckTest("ChildCalledBeforeStartCheck", R"(
module if_zero_no_warn;
    task child_task(); endtask

    initial begin
        fork
            child_task();
        join_none

        if (0)
            child_task();
    end
endmodule
)");
    CHECK(result);
}

TEST_CASE("ChildCalledBeforeStartCheck: if false constant parent call -negative") {
    auto result = runCheckTest("ChildCalledBeforeStartCheck", R"(
module if_constant_false_no_warn;
    localparam bit ENABLE = 0;
    task child_task(); endtask

    initial begin
        fork
            child_task();
        join_none

        if (ENABLE)
            child_task();
    end
endmodule
)");
    CHECK(result);
}

TEST_CASE("ChildCalledBeforeStartCheck: if true else unreachable -negative") {
    auto result = runCheckTest("ChildCalledBeforeStartCheck", R"(
module unreachable_else_no_warn;
    task child_task(); endtask

    initial begin
        fork
            child_task();
        join_none

        if (1)
            $display("reachable");
        else
            child_task();
    end
endmodule
)");
    CHECK(result);
}

TEST_CASE("ChildCalledBeforeStartCheck: if true parent call -positive") {
    auto result = runCheckTest("ChildCalledBeforeStartCheck", R"(
module if_true_warn;
    task child_task(); endtask

    initial begin
        fork
            child_task();
        join_none

        if (1)
            child_task();
    end
endmodule
)");
    CHECK_FALSE(result);
}



TEST_CASE("ChildCalledBeforeStartCheck: parent call inside nested begin-end -positive") {
    auto result = runCheckTest("ChildCalledBeforeStartCheck", R"(
module nested_parent_call_warn;
    task child_task(); endtask

    initial begin
        fork
            child_task();
        join_none

        begin
            child_task();
        end
    end
endmodule
)");
    CHECK_FALSE(result);
}

TEST_CASE("ChildCalledBeforeStartCheck: parent call inside if branch -positive") {
    auto result = runCheckTest("ChildCalledBeforeStartCheck", R"(
module conditional_parent_call_warn;
    bit condition = 1;
    task child_task(); endtask

    initial begin
        fork
            child_task();
        join_none

        if (condition)
            child_task();
    end
endmodule
)");
    CHECK_FALSE(result);
}



TEST_CASE("ChildCalledBeforeStartCheck: matching call nested in argument -positive") {
    auto result = runCheckTest("ChildCalledBeforeStartCheck", R"(
module nested_argument_warn;
    function int child_func(); return 1; endfunction
    function void wrapper(int value); endfunction

    initial begin
        fork
            child_func();
        join_none

        wrapper(child_func());
    end
endmodule
)");
    CHECK_FALSE(result);
}

TEST_CASE("ChildCalledBeforeStartCheck: two matching calls nested -positive") {
    auto result = runCheckTest("ChildCalledBeforeStartCheck", R"(
module nested_matching_calls_warn;
    function int foo(int value); return value; endfunction
    function int bar(); return 1; endfunction

    initial begin
        fork
            foo(0);
            bar();
        join_none

        foo(bar());
    end
endmodule
)");
    CHECK_FALSE(result);
}

TEST_CASE("ChildCalledBeforeStartCheck: multiple parent calls -positive") {
    auto result = runCheckTest("ChildCalledBeforeStartCheck", R"(
module multiple_parent_calls_warn;
    task child_task(); endtask

    initial begin
        fork
            child_task();
        join_none

        child_task();
        child_task();
    end
endmodule
)");
    CHECK_FALSE(result);
}


TEST_CASE("ChildCalledBeforeStartCheck: same method same object -positive") {
    auto result = runCheckTest("ChildCalledBeforeStartCheck", R"(
module same_object_warn;
    class Worker;
        task run(); endtask
    endclass

    Worker a = new();

    initial begin
        fork
            a.run();
        join_none

        a.run();
    end
endmodule
)");
    CHECK_FALSE(result);
}



TEST_CASE("ChildCalledBeforeStartCheck: delay zero releases children -negative") {
    auto result = runCheckTest("ChildCalledBeforeStartCheck", R"(
module delay_zero_no_warn;
    task child_task(); endtask

    initial begin
        fork
            child_task();
        join_none

        #0;
        child_task();
    end
endmodule
)");
    CHECK(result);
}

TEST_CASE("ChildCalledBeforeStartCheck: delay nonzero releases children -negative") {
    auto result = runCheckTest("ChildCalledBeforeStartCheck", R"(
module delay_nonzero_no_warn;
    task child_task(); endtask

    initial begin
        fork
            child_task();
        join_none

        #1;
        child_task();
    end
endmodule
)");
    CHECK(result);
}

TEST_CASE("ChildCalledBeforeStartCheck: timed call after delay -negative") {
    auto result = runCheckTest("ChildCalledBeforeStartCheck", R"(
module timed_call_no_warn;
    task child_task(); endtask

    initial begin
        fork
            child_task();
        join_none

        #0 child_task();
    end
endmodule
)");
    CHECK(result);
}

TEST_CASE("ChildCalledBeforeStartCheck: nested join all releases children -negative") {
    auto result = runCheckTest("ChildCalledBeforeStartCheck", R"(
module nested_join_all_no_warn;
    task child_task(); endtask

    initial begin
        fork
            child_task();
        join_none

        fork
            #1;
        join

        child_task();
    end
endmodule
)");
    CHECK(result);
}

TEST_CASE("ChildCalledBeforeStartCheck: nested join any releases children -negative") {
    auto result = runCheckTest("ChildCalledBeforeStartCheck", R"(
module nested_join_any_no_warn;
    task child_task(); endtask

    initial begin
        fork
            child_task();
        join_none

        fork
            #1;
        join_any

        child_task();
    end
endmodule
)");
    CHECK(result);
}


TEST_CASE("ChildCalledBeforeStartCheck: call before delay inside nested block -positive") {
    auto result = runCheckTest("ChildCalledBeforeStartCheck", R"(
module nested_call_before_delay_warn;
    task child_task(); endtask

    initial begin
        fork
            child_task();
        join_none

        begin
            child_task();
            #0;
        end
    end
endmodule
)");
    CHECK_FALSE(result);
}




TEST_CASE("ChildCalledBeforeStartCheck: unreachable call after return -negative") {
    auto result = runCheckTest("ChildCalledBeforeStartCheck", R"(
module return_before_call;
    task child_task(); endtask
    task parent_task();
        fork child_task(); join_none
        return;
        child_task();
    endtask
endmodule
)");
    CHECK(result);
}

TEST_CASE("ChildCalledBeforeStartCheck: unreachable call after forever -negative") {
    auto result = runCheckTest("ChildCalledBeforeStartCheck", R"(
module forever_before_call;
    task child_task(); endtask
    initial begin
        fork child_task(); join_none
        forever begin
            #1;
        end
        child_task();
    end
endmodule
)");
    CHECK(result);
}

TEST_CASE("ChildCalledBeforeStartCheck: repeat zero call unreachable -negative") {
    auto result = runCheckTest("ChildCalledBeforeStartCheck", R"(
module repeat_zero_call;
    task child_task(); endtask
    initial begin
        fork child_task(); join_none
        repeat (0) child_task();
    end
endmodule
)");
    CHECK(result);
}

TEST_CASE("ChildCalledBeforeStartCheck: call inside delay expression -positive") {
    auto result = runCheckTest("ChildCalledBeforeStartCheck", R"(
module call_in_delay;
    function int child_func(); return 1; endfunction
    initial begin
        fork child_func(); join_none
        #(child_func());
    end
endmodule
)");
    CHECK_FALSE(result);
}

TEST_CASE("ChildCalledBeforeStartCheck: call inside cycle delay -positive") {
    auto result = runCheckTest("ChildCalledBeforeStartCheck", R"(
module call_in_cycle_delay;
    bit clk;
    function int child_func(); return 1; endfunction
    default clocking cb @(posedge clk);
    endclocking
    initial begin
        fork child_func(); join_none
        ##(child_func());
    end
endmodule
)");
    CHECK_FALSE(result);
}

TEST_CASE("ChildCalledBeforeStartCheck: parameter delay releases children -negative") {
    auto result = runCheckTest("ChildCalledBeforeStartCheck", R"(
module parameter_delay;
    parameter int DELAY = 1;
    task child_task(); endtask
    initial begin
        fork child_task(); join_none
        #DELAY;
        child_task();
    end
endmodule
)");
    CHECK(result);
}

TEST_CASE("ChildCalledBeforeStartCheck: arithmetic delay releases children -negative") {
    auto result = runCheckTest("ChildCalledBeforeStartCheck", R"(
module arithmetic_delay;
    task child_task(); endtask
    initial begin
        fork child_task(); join_none
        #(1 + 1);
        child_task();
    end
endmodule
)");
    CHECK(result);
}

TEST_CASE("ChildCalledBeforeStartCheck: variable delay releases children -negative") {
    auto result = runCheckTest("ChildCalledBeforeStartCheck", R"(
module variable_delay;
    task child_task(); endtask
    int delay_amount = 1;
    initial begin
        fork child_task(); join_none
        #(delay_amount);
        child_task();
    end
endmodule
)");
    CHECK(result);
}

TEST_CASE("ChildCalledBeforeStartCheck: automatic declaration initializer -positive") {
    auto result = runCheckTest("ChildCalledBeforeStartCheck", R"(
module automatic_decl_initializer;
    function int child_func(); return 1; endfunction
    initial begin
        fork
            begin
                automatic int x = child_func();
            end
        join_none
        child_func();
    end
endmodule
)");
    CHECK_FALSE(result);
}

TEST_CASE("ChildCalledBeforeStartCheck: leading fork declaration -positive") {
    auto result = runCheckTest("ChildCalledBeforeStartCheck", R"(
module leading_fork_decl;
    function int child_func(); return 1; endfunction
    initial begin
        fork
            automatic int x = child_func();
        join_none
        child_func();
    end
endmodule
)");
    CHECK_FALSE(result);
}

TEST_CASE("ChildCalledBeforeStartCheck: child call in repeat zero -negative") {
    auto result = runCheckTest("ChildCalledBeforeStartCheck", R"(
module child_repeat_zero;
    task child_task(); endtask
    initial begin
        fork
            repeat (0) child_task();
        join_none
        child_task();
    end
endmodule
)");
    CHECK(result);
}

TEST_CASE("ChildCalledBeforeStartCheck: child call after return -negative") {
    auto result = runCheckTest("ChildCalledBeforeStartCheck", R"(
module child_after_return;
    task child_task(); endtask
    task parent_task();
        fork
            begin
                return;
                child_task();
            end
        join_none
        child_task();
    endtask
endmodule
)");
    CHECK(result);
}

TEST_CASE("ChildCalledBeforeStartCheck: semaphore get available key -positive") {
    auto result = runCheckTest("ChildCalledBeforeStartCheck", R"(
module semaphore_available;
    semaphore sem = new(1);
    task child_task(); endtask
    initial begin
        fork child_task(); join_none
        sem.get(1);
        child_task();
    end
endmodule
)");
    CHECK_FALSE(result);
}

TEST_CASE("ChildCalledBeforeStartCheck: mailbox get with message -positive") {
    auto result = runCheckTest("ChildCalledBeforeStartCheck", R"(
module mailbox_available;
    mailbox #(int) mbox = new();
    task child_task(); endtask
    initial begin
        mbox.put(42);
        fork child_task(); join_none
        begin
            int value;
            mbox.get(value);
        end
        child_task();
    end
endmodule
)");
    CHECK_FALSE(result);
}

TEST_CASE("ChildCalledBeforeStartCheck: empty join_any does not suspend -positive") {
    auto result = runCheckTest("ChildCalledBeforeStartCheck", R"(
module empty_join_any;
    task child_task(); endtask
    initial begin
        fork child_task(); join_none
        fork join_any
        child_task();
    end
endmodule
)");
    CHECK_FALSE(result);
}

TEST_CASE("ChildCalledBeforeStartCheck: wait true variable does not suspend -positive") {
    auto result = runCheckTest("ChildCalledBeforeStartCheck", R"(
module wait_true_variable;
    task child_task(); endtask
    bit ready = 1;
    initial begin
        fork child_task(); join_none
        wait (ready);
        child_task();
    end
endmodule
)");
    CHECK_FALSE(result);
}


//AI used for tests gen, not for lint.
