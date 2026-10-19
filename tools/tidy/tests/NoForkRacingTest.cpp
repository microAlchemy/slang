// SPDX-FileCopyrightText: Michael Popoloski
// SPDX-License-Identifier: MIT

#include "Test.h"
#include "TidyFactory.h"
#include "TidyTest.h"

#include <string>
#include <string_view>

static size_t countOf(const std::string& haystack, std::string_view needle) {
    size_t n = 0;
    for (auto pos = haystack.find(needle); pos != std::string::npos;
         pos = haystack.find(needle, pos + needle.size()))
        ++n;
    return n;
}

// BASIC SHARED WRITES

TEST_CASE("NoForkRacing: same scalar -positive") {
    auto result = runCheckTest("NoForkRacing", R"(
module m;
    int x;

    initial fork
        x = 1;
        x = 2;
    join
endmodule
)");
    CHECK_FALSE(result);
}

TEST_CASE("NoForkRacing: different scalars -negative") {
    auto result = runCheckTest("NoForkRacing", R"(
module m;
    int x, y;

    initial fork
        x = 1;
        y = 2;
    join
endmodule
)");
    CHECK(result);
}

TEST_CASE("NoForkRacing: writes within one child -negative") {
    auto result = runCheckTest("NoForkRacing", R"(
module m;
    int x, y;

    initial fork
        begin
            x = 1;
            x = 2;
        end
        y = 3;
    join
endmodule
)");
    CHECK(result);
}

TEST_CASE("NoForkRacing: three conflicting children -positive") {
    auto result = runCheckTest("NoForkRacing", R"(
module m;
    int x;

    initial fork
        x = 1;
        x = 2;
        x = 3;
    join
endmodule
)");
    CHECK_FALSE(result);
}

// FORK VARIANTS

TEST_CASE("NoForkRacing: join_any -positive") {
    auto result = runCheckTest("NoForkRacing", R"(
module m;
    int x;

    initial fork
        x = 1;
        x = 2;
    join_any
endmodule
)");
    CHECK_FALSE(result);
}

TEST_CASE("NoForkRacing: join_none -positive") {
    auto result = runCheckTest("NoForkRacing", R"(
module m;
    int x;

    initial fork
        x = 1;
        x = 2;
    join_none
endmodule
)");
    CHECK_FALSE(result);
}

TEST_CASE("NoForkRacing: single child -negative") {
    auto result = runCheckTest("NoForkRacing", R"(
module m;
    int x;

    initial fork
        begin
            x = 1;
            x = 2;
        end
    join
endmodule
)");
    CHECK(result);
}

TEST_CASE("NoForkRacing: sequential statements -negative") {
    auto result = runCheckTest("NoForkRacing", R"(
module m;
    int x;

    initial begin
        x = 1;
        x = 2;
    end
endmodule
)");
    CHECK(result);
}

// NESTED FORKS

TEST_CASE("NoForkRacing: nested fork versus outer sibling -positive") {
    auto result = runCheckTest("NoForkRacing", R"(
module m;
    int x;

    initial fork
        begin
            fork
                x = 1;
            join
        end

        x = 2;
    join
endmodule
)");
    CHECK_FALSE(result);
}

TEST_CASE("NoForkRacing: nested join_none versus outer sibling -positive") {
    auto result = runCheckTest("NoForkRacing", R"(
module m;
    int x;

    initial fork
        begin
            fork
                x = 1;
            join_none
        end

        x = 2;
    join
endmodule
)");
    CHECK_FALSE(result);
}

TEST_CASE("NoForkRacing: nested internal conflict -positive") {
    auto result = runCheckTest("NoForkRacing", R"(
module m;
    int x;

    initial fork
        begin
            fork
                x = 1;
                x = 2;
            join
        end
    join
endmodule
)");
    CHECK_FALSE(result);
}

TEST_CASE("NoForkRacing: nested disjoint writes -negative") {
    auto result = runCheckTest("NoForkRacing", R"(
module m;
    int x, y, z;

    initial fork
        begin
            fork
                x = 1;
                y = 2;
            join
        end

        z = 3;
    join
endmodule
)");
    CHECK(result);
}

TEST_CASE("NoForkRacing: deeply nested fork conflict -positive") {
    auto result = runCheckTest("NoForkRacing", R"(
module m;
    int x;

    initial fork
        begin
            fork
                begin
                    fork
                        x = 1;
                    join
                end
            join
        end

        x = 2;
    join
endmodule
)");
    CHECK_FALSE(result);
}

// PACKED SELECTIONS

TEST_CASE("NoForkRacing: same packed bit -positive") {
    auto result = runCheckTest("NoForkRacing", R"(
module m;
    logic [7:0] data;

    initial fork
        data[3] = 1;
        data[3] = 0;
    join
endmodule
)");
    CHECK_FALSE(result);
}

TEST_CASE("NoForkRacing: different packed bits -negative") {
    auto result = runCheckTest("NoForkRacing", R"(
module m;
    logic [7:0] data;

    initial fork
        data[0] = 1;
        data[1] = 0;
    join
endmodule
)");
    CHECK(result);
}

TEST_CASE("NoForkRacing: overlapping packed ranges -positive") {
    auto result = runCheckTest("NoForkRacing", R"(
module m;
    logic [15:0] data;

    initial fork
        data[7:0] = 0;
        data[11:4] = 0;
    join
endmodule
)");
    CHECK_FALSE(result);
}

TEST_CASE("NoForkRacing: disjoint packed ranges -negative") {
    auto result = runCheckTest("NoForkRacing", R"(
module m;
    logic [15:0] data;

    initial fork
        data[7:0] = 0;
        data[15:8] = 0;
    join
endmodule
)");
    CHECK(result);
}

TEST_CASE("NoForkRacing: whole vector versus bit -positive") {
    auto result = runCheckTest("NoForkRacing", R"(
module m;
    logic [7:0] data;

    initial fork
        data = 0;
        data[3] = 1;
    join
endmodule
)");
    CHECK_FALSE(result);
}

TEST_CASE("NoForkRacing: indexed upward overlap -positive") {
    auto result = runCheckTest("NoForkRacing", R"(
module m;
    logic [31:0] data;

    initial fork
        data[4 +: 8] = 0;
        data[8 +: 8] = 0;
    join
endmodule
)");
    CHECK_FALSE(result);
}

TEST_CASE("NoForkRacing: indexed upward disjoint -negative") {
    auto result = runCheckTest("NoForkRacing", R"(
module m;
    logic [31:0] data;

    initial fork
        data[0 +: 8] = 0;
        data[8 +: 8] = 0;
    join
endmodule
)");
    CHECK(result);
}

TEST_CASE("NoForkRacing: indexed downward overlap -positive") {
    auto result = runCheckTest("NoForkRacing", R"(
module m;
    logic [31:0] data;

    initial fork
        data[15 -: 8] = 0;
        data[11 -: 8] = 0;
    join
endmodule
)");
    CHECK_FALSE(result);
}

TEST_CASE("NoForkRacing: indexed downward disjoint -negative") {
    auto result = runCheckTest("NoForkRacing", R"(
module m;
    logic [31:0] data;

    initial fork
        data[15 -: 8] = 0;
        data[7 -: 8] = 0;
    join
endmodule
)");
    CHECK(result);
}

// UNPACKED ARRAYS

TEST_CASE("NoForkRacing: same unpacked element -positive") {
    auto result = runCheckTest("NoForkRacing", R"(
module m;
    int data[4];

    initial fork
        data[0] = 1;
        data[0] = 2;
    join
endmodule
)");
    CHECK_FALSE(result);
}

TEST_CASE("NoForkRacing: different unpacked elements -negative") {
    auto result = runCheckTest("NoForkRacing", R"(
module m;
    int data[4];

    initial fork
        data[0] = 1;
        data[1] = 2;
    join
endmodule
)");
    CHECK(result);
}

TEST_CASE("NoForkRacing: whole unpacked array versus element -positive") {
    auto result = runCheckTest("NoForkRacing", R"(
module m;
    int data[4];

    initial fork
        data = '{default: 0};
        data[1] = 2;
    join
endmodule
)");
    CHECK_FALSE(result);
}

TEST_CASE("NoForkRacing: multidimensional disjoint -negative") {
    auto result = runCheckTest("NoForkRacing", R"(
module m;
    int data[4][4];

    initial fork
        data[0][1] = 1;
        data[0][2] = 2;
    join
endmodule
)");
    CHECK(result);
}

TEST_CASE("NoForkRacing: multidimensional overlap -positive") {
    auto result = runCheckTest("NoForkRacing", R"(
module m;
    int data[4][4];

    initial fork
        data[0][1] = 1;
        data[0][1] = 2;
    join
endmodule
)");
    CHECK_FALSE(result);
}

// MIXED PACKED AND UNPACKED SELECTIONS

TEST_CASE("NoForkRacing: same element disjoint slices -negative") {
    auto result = runCheckTest("NoForkRacing", R"(
module m;
    logic [7:0] data[4];

    initial fork
        data[0][3:0] = 0;
        data[0][7:4] = 0;
    join
endmodule
)");
    CHECK(result);
}

TEST_CASE("NoForkRacing: same element overlapping slices -positive") {
    auto result = runCheckTest("NoForkRacing", R"(
module m;
    logic [7:0] data[4];

    initial fork
        data[0][5:0] = 0;
        data[0][7:4] = 0;
    join
endmodule
)");
    CHECK_FALSE(result);
}

TEST_CASE("NoForkRacing: different elements same slices -negative") {
    auto result = runCheckTest("NoForkRacing", R"(
module m;
    logic [7:0] data[4];

    initial fork
        data[0][3:0] = 0;
        data[1][3:0] = 0;
    join
endmodule
)");
    CHECK(result);
}

// DYNAMIC INDEXES

TEST_CASE("NoForkRacing: same dynamic index -positive") {
    auto result = runCheckTest("NoForkRacing", R"(
module m;
    int data[8];
    int i;

    initial fork
        data[i] = 1;
        data[i] = 2;
    join
endmodule
)");
    CHECK_FALSE(result);
}


TEST_CASE("NoForkRacing: dynamic indices disjoint slices -negative") {
    auto result = runCheckTest("NoForkRacing", R"(
module m;
    logic [7:0] data[8];
    int i, j;

    initial fork
        data[i][3:0] = 0;
        data[j][7:4] = 0;
    join
endmodule
)");
    CHECK(result);
}


// STRUCT MEMBERS

TEST_CASE("NoForkRacing: same struct member -positive") {
    auto result = runCheckTest("NoForkRacing", R"(
module m;
    typedef struct {
        int x;
        int y;
    } pair_t;

    pair_t data;

    initial fork
        data.x = 1;
        data.x = 2;
    join
endmodule
)");
    CHECK_FALSE(result);
}

TEST_CASE("NoForkRacing: different struct members -negative") {
    auto result = runCheckTest("NoForkRacing", R"(
module m;
    typedef struct {
        int x;
        int y;
    } pair_t;

    pair_t data;

    initial fork
        data.x = 1;
        data.y = 2;
    join
endmodule
)");
    CHECK(result);
}

TEST_CASE("NoForkRacing: whole struct versus member -positive") {
    auto result = runCheckTest("NoForkRacing", R"(
module m;
    typedef struct {
        int x;
        int y;
    } pair_t;

    pair_t data;

    initial fork
        data = '{1, 2};
        data.x = 3;
    join
endmodule
)");
    CHECK_FALSE(result);
}

TEST_CASE("NoForkRacing: nested struct disjoint members -negative") {
    auto result = runCheckTest("NoForkRacing", R"(
module m;
    typedef struct {
        int x;
        int y;
    } inner_t;

    typedef struct {
        inner_t a;
        inner_t b;
    } outer_t;

    outer_t data;

    initial fork
        data.a.x = 1;
        data.a.y = 2;
    join
endmodule
)");
    CHECK(result);
}

TEST_CASE("NoForkRacing: packed struct disjoint members -negative") {
    auto result = runCheckTest("NoForkRacing", R"(
module m;
    typedef struct packed {
        logic [7:0] x;
        logic [7:0] y;
    } pair_t;

    pair_t data;

    initial fork
        data.x = 8'h12;
        data.y = 8'h34;
    join
endmodule
)");
    CHECK(result);
}

// ASSIGNMENT OPERATOR

TEST_CASE("NoForkRacing: compound assignments -positive") {
    auto result = runCheckTest("NoForkRacing", R"(
module m;
    int x;

    initial fork
        x += 1;
        x -= 1;
    join
endmodule
)");
    CHECK_FALSE(result);
}

TEST_CASE("NoForkRacing: increments -positive") {
    auto result = runCheckTest("NoForkRacing", R"(
module m;
    int x;

    initial fork
        x++;
        ++x;
    join
endmodule
)");
    CHECK_FALSE(result);
}

TEST_CASE("NoForkRacing: decrements -positive") {
    auto result = runCheckTest("NoForkRacing", R"(
module m;
    int x;

    initial fork
        x--;
        --x;
    join
endmodule
)");
    CHECK_FALSE(result);
}

TEST_CASE("NoForkRacing: increment inside index -positive") {
    auto result = runCheckTest("NoForkRacing", R"(
module m;
    int data[8];
    int index;

    initial fork
        data[index++] = 1;
        index = 0;
    join
endmodule
)");
    CHECK_FALSE(result);
}

TEST_CASE("NoForkRacing: increment versus assignment -positive") {
    auto result = runCheckTest("NoForkRacing", R"(
module m;
    int x;

    initial fork
        x++;
        x = 5;
    join
endmodule
)");
    CHECK_FALSE(result);
}

// CONCATENATION LVALUES

TEST_CASE("NoForkRacing: concatenation overlap -positive") {
    auto result = runCheckTest("NoForkRacing", R"(
module m;
    logic [7:0] a, b;

    initial fork
        {a, b} = 16'h1234;
        a = 8'hff;
    join
endmodule
)");
    CHECK_FALSE(result);
}

TEST_CASE("NoForkRacing: concatenation disjoint -negative") {
    auto result = runCheckTest("NoForkRacing", R"(
module m;
    logic [7:0] a, b, c;

    initial fork
        {a, b} = 16'h1234;
        c = 8'hff;
    join
endmodule
)");
    CHECK(result);
}

TEST_CASE("NoForkRacing: concatenation selected ranges -positive") {
    auto result = runCheckTest("NoForkRacing", R"(
module m;
    logic [15:0] a, b;

    initial fork
        {a[3:0], b[7:0]} = 12'habc;
        a[2:0] = 3'b111;
    join
endmodule
)");
    CHECK_FALSE(result);
}

// TASK ARGUMENT

TEST_CASE("NoForkRacing: ref argument versus assignment -positive") {
    auto result = runCheckTest("NoForkRacing", R"(
module m;
    int x;

    task automatic modify(ref int value);
        value = 42;
    endtask

    initial fork
        modify(x);
        x = 1;
    join
endmodule
)");
    CHECK_FALSE(result);
}

TEST_CASE("NoForkRacing: two ref calls -positive") {
    auto result = runCheckTest("NoForkRacing", R"(
module m;
    int x;

    task automatic modify(ref int value);
        value = 42;
    endtask

    initial fork
        modify(x);
        modify(x);
    join
endmodule
)");
    CHECK_FALSE(result);
}

TEST_CASE("NoForkRacing: const ref argument -negative") {
    auto result = runCheckTest("NoForkRacing", R"(
module m;
    int x;

    task automatic inspect(const ref int value);
        int temp;
        temp = value;
    endtask

    initial fork
        inspect(x);
        x = 1;
    join
endmodule
)");
    CHECK(result);
}

TEST_CASE("NoForkRacing: output argument -positive") {
    auto result = runCheckTest("NoForkRacing", R"(
module m;
    int x;

    task automatic modify(output int value);
        value = 42;
    endtask

    initial fork
        modify(x);
        x = 1;
    join
endmodule
)");
    CHECK_FALSE(result);
}

TEST_CASE("NoForkRacing: inout argument -positive") {
    auto result = runCheckTest("NoForkRacing", R"(
module m;
    int x;

    task automatic modify(inout int value);
        value = 42;
    endtask

    initial fork
        modify(x);
        x = 1;
    join
endmodule
)");
    CHECK_FALSE(result);
}

TEST_CASE("NoForkRacing: input argument -negative") {
    auto result = runCheckTest("NoForkRacing", R"(
module m;
    int x;

    task automatic inspect(input int value);
        int temp;
        temp = value;
    endtask

    initial fork
        inspect(x);
        x = 1;
    join
endmodule
)");
    CHECK(result);
}

TEST_CASE("NoForkRacing: ref selected element -positive") {
    auto result = runCheckTest("NoForkRacing", R"(
module m;
    int data[4];

    task automatic modify(ref int value);
        value = 42;
    endtask

    initial fork
        modify(data[2]);
        data[2] = 1;
    join
endmodule
)");
    CHECK_FALSE(result);
}

TEST_CASE("NoForkRacing: ref disjoint elements -negative") {
    auto result = runCheckTest("NoForkRacing", R"(
module m;
    int data[4];

    task automatic modify(ref int value);
        value = 42;
    endtask

    initial fork
        modify(data[0]);
        data[1] = 1;
    join
endmodule
)");
    CHECK(result);
}

// CLASS MEMBERS

TEST_CASE("NoForkRacing: explicit this -positive") {
    auto result = runCheckTest("NoForkRacing", R"(
module m;
    class C;
        int x;

        task run();
            fork
                this.x = 1;
                this.x = 2;
            join
        endtask
    endclass
endmodule
)");
    CHECK_FALSE(result);
}

TEST_CASE("NoForkRacing: different this members -negative") {
    auto result = runCheckTest("NoForkRacing", R"(
module m;
    class C;
        int x, y;

        task run();
            fork
                this.x = 1;
                this.y = 2;
            join
        endtask
    endclass
endmodule
)");
    CHECK(result);
}

TEST_CASE("NoForkRacing: different object handles -negative") {
    auto result = runCheckTest("NoForkRacing", R"(
module m;
    class C;
        int x;
    endclass

    C a, b;

    initial begin
        a = new;
        b = new;

        fork
            a.x = 1;
            b.x = 2;
        join
    end
endmodule
)");
    CHECK(result);
}

TEST_CASE("NoForkRacing: array of objects disjoint elements -negative") {
    auto result = runCheckTest("NoForkRacing", R"(
module m;
    class C;
        int x;
    endclass

    C objects[2];

    initial begin
        objects[0] = new;
        objects[1] = new;

        fork
            objects[0].x = 1;
            objects[1].x = 2;
        join
    end
endmodule
)");
    CHECK(result);
}

TEST_CASE("NoForkRacing: array of objects same element -positive") {
    auto result = runCheckTest("NoForkRacing", R"(
module m;
    class C;
        int x;
    endclass

    C objects[2];

    initial begin
        objects[0] = new;

        fork
            objects[0].x = 1;
            objects[0].x = 2;
        join
    end
endmodule
)");
    CHECK_FALSE(result);
}

// VARIABLE SCOPE

TEST_CASE("NoForkRacing: distinct child locals -negative") {
    auto result = runCheckTest("NoForkRacing", R"(
module m;
    initial fork
        begin
            int x;
            x = 1;
        end

        begin
            int x;
            x = 2;
        end
    join
endmodule
)");
    CHECK(result);
}

TEST_CASE("NoForkRacing: shared automatic variable -positive") {
    auto result = runCheckTest("NoForkRacing", R"(
module m;
    initial begin
        automatic int x;

        fork
            x = 1;
            x = 2;
        join
    end
endmodule
)");
    CHECK_FALSE(result);
}

TEST_CASE("NoForkRacing: named fork shared declaration -positive") {
    auto result = runCheckTest("NoForkRacing", R"(
module m;
    initial begin
        fork : workers
            int x;

            x = 1;
            x = 2;
        join
    end
endmodule
)");
    CHECK_FALSE(result);
}

// CONTROL FLOW

TEST_CASE("NoForkRacing: conditional writes -positive") {
    auto result = runCheckTest("NoForkRacing", R"(
module m;
    int x;
    bit a, b;

    initial fork
        if (a)
            x = 1;

        if (b)
            x = 2;
    join
endmodule
)");
    CHECK_FALSE(result);
}

TEST_CASE("NoForkRacing: loop versus assignment -positive") {
    auto result = runCheckTest("NoForkRacing", R"(
module m;
    int x;

    initial fork
        for (int i = 0; i < 4; i++)
            x = i;

        x = 2;
    join
endmodule
)");
    CHECK_FALSE(result);
}

TEST_CASE("NoForkRacing: case versus assignment -positive") {
    auto result = runCheckTest("NoForkRacing", R"(
module m;
    int x;
    bit select;

    initial fork
        case (select)
            0: x = 1;
            1: x = 2;
        endcase

        x = 3;
    join
endmodule
)");
    CHECK_FALSE(result);
}

TEST_CASE("NoForkRacing: mutually exclusive writes in one child -negative") {
    auto result = runCheckTest("NoForkRacing", R"(
module m;
    int x, y;
    bit select;

    initial fork
        begin
            if (select)
                x = 1;
            else
                x = 2;
        end

        y = 3;
    join
endmodule
)");
    CHECK(result);
}

// KNOWN LIMITATIONS

TEST_CASE("NoForkRacing: handle aliasing -known limitation") {
    auto result = runCheckTest("NoForkRacing", R"(
module m;
    class C;
        int x;
    endclass

    C a, b;

    initial begin
        a = new;
        b = a;

        fork
            a.x = 1;
            b.x = 2;
        join
    end
endmodule
)");
    // No alias analysis: different root symbols.
    CHECK(result);
}

TEST_CASE("NoForkRacing: called task modifies global -known limitation") {
    auto result = runCheckTest("NoForkRacing", R"(
module m;
    int x;

    task automatic modify();
        x = 1;
    endtask

    initial fork
        modify();
        x = 2;
    join
endmodule
)");
    // No interprocedural side-effect analysis.
    CHECK(result);
}

TEST_CASE("NoForkRacing: semaphore synchronization -known limitation") {
    auto result = runCheckTest("NoForkRacing", R"(
module m;
    semaphore sem;
    int x;

    initial begin
        sem = new(1);

        fork
            begin
                sem.get();
                x = 1;
                sem.put();
            end

            begin
                sem.get();
                x = 2;
                sem.put();
            end
        join
    end
endmodule
)");
    // Synchronization is not analyzed, so a warning is expected.
    CHECK_FALSE(result);
}

//THE FOLLOWING FAIL:

TEST_CASE("NoForkRacing: dynamic indices overlapping slices -negative") {
    auto result = runCheckTest("NoForkRacing", R"(
module m;
    logic [7:0] data[8];
    int i, j;

    initial fork
        data[i][5:0] = 0;
        data[j][7:4] = 0;
    join
endmodule
)");
    CHECK(result);
}



TEST_CASE("NoForkRacing: constant versus dynamic index -negative") {
    auto result = runCheckTest("NoForkRacing", R"(
module m;
    int data[8];
    int i;

    initial fork
        data[3] = 1;
        data[i] = 2;
    join
endmodule
)");
    CHECK(result);
}


TEST_CASE("NoForkRacing: different dynamic indices -negative") {
    auto result = runCheckTest("NoForkRacing", R"(
module m;
    int data[8];
    int i, j;

    initial fork
        data[i] = 1;
        data[j] = 2;
    join
endmodule
)");
    CHECK(result);
}



TEST_CASE("NoForkRacing: implicit and explicit this -positive") {
    auto result = runCheckTest("NoForkRacing", R"(
module m;
    class C;
        int x;

        task run();
            fork
                x = 1;
                this.x = 2;
            join
        endtask
    endclass
endmodule
)");
    CHECK_FALSE(result);
}



// THIS-HANDLE NORMALIZATION

TEST_CASE("NoForkRacing: explicit this reports member name, not 'this'") {
    std::string output;
    auto result = runCheckTest("NoForkRacing", R"(
module m;
    class C;
        int x;

        task run();
            fork
                this.x = 1;
                this.x = 2;
            join
        endtask
    endclass
endmodule
)",
                               {}, &output);
    CHECK_FALSE(result);
    CHECK(countOf(output, "'x' may be written") == 1);
    CHECK(countOf(output, "'this'") == 0);
}

TEST_CASE("NoForkRacing: second this-member race not swallowed by dedupe") {
    std::string output;
    auto result = runCheckTest("NoForkRacing", R"(
module m;
    class C;
        int a, b;

        task run();
            fork
                begin
                    this.a = 1;
                    this.b = 1;
                end
                begin
                    this.a = 2;
                    this.b = 2;
                end
            join
        endtask
    endclass
endmodule
)",
                               {}, &output);
    CHECK_FALSE(result);
    CHECK(countOf(output, "may be written") == 2);
}

TEST_CASE("NoForkRacing: implicit vs explicit this, unpacked element -positive") {
    auto result = runCheckTest("NoForkRacing", R"(
module m;
    class C;
        int arr[4];

        task run();
            fork
                arr[3] = 1;
                this.arr[3] = 2;
            join
        endtask
    endclass
endmodule
)");
    CHECK_FALSE(result);
}

TEST_CASE("NoForkRacing: implicit vs explicit this, struct field -positive") {
    auto result = runCheckTest("NoForkRacing", R"(
module m;
    class C;
        typedef struct { int f; int g; } s_t;
        s_t s;

        task run();
            fork
                s.f = 1;
                this.s.f = 2;
            join
        endtask
    endclass
endmodule
)");
    CHECK_FALSE(result);
}

TEST_CASE("NoForkRacing: implicit vs explicit this, packed bit -positive") {
    auto result = runCheckTest("NoForkRacing", R"(
module m;
    class C;
        logic [7:0] v;

        task run();
            fork
                v[2] = 1'b1;
                this.v[2] = 1'b0;
            join
        endtask
    endclass
endmodule
)");
    CHECK_FALSE(result);
}

// BY-REF SYSTEM METHOD ARGS (isArgByRef path)

TEST_CASE("NoForkRacing: assoc array iterator writes index -positive") {
    auto result = runCheckTest("NoForkRacing", R"(
module m;
    int aa[int];
    int k;

    initial fork
        void'(aa.first(k));
        k = 0;
    join
endmodule
)");
    CHECK_FALSE(result);
}

TEST_CASE("NoForkRacing: two assoc iterators share index -positive") {
    auto result = runCheckTest("NoForkRacing", R"(
module m;
    int aa[int];
    int k;

    initial fork
        void'(aa.next(k));
        void'(aa.prev(k));
    join
endmodule
)");
    CHECK_FALSE(result);
}

// FORK-LEVEL DECLARATIONS ARE NOT CHILDREN

TEST_CASE("NoForkRacing: fork declaration initializer is not a child -negative") {
    auto result = runCheckTest("NoForkRacing", R"(
module m;
    int x;

    initial fork : w
        automatic int t = x++;
        x = 1;
    join
endmodule
)");
    // LRM 9.3.2: block-item declarations run in the parent before any
    // child process is spawned, so this is ordered, not a race.
    CHECK(result);
}


TEST_CASE("NoForkRacing: explicit this then implicit, reversed order -positive") {
    auto result = runCheckTest("NoForkRacing", R"(
module m;
    class C;
        int x;

        task run();
            fork
                this.x = 1;
                x = 2;
            join
        endtask
    endclass
endmodule
)");
    CHECK_FALSE(result);
}

TEST_CASE("NoForkRacing: super member vs implicit -positive") {
    auto result = runCheckTest("NoForkRacing", R"(
module m;
    class B;
        int x;
    endclass

    class C extends B;
        task run();
            fork
                super.x = 1;
                x = 2;
            join
        endtask
    endclass
endmodule
)");
    CHECK_FALSE(result);
}

TEST_CASE("NoForkRacing: two module-level races both reported") {
    std::string output;
    auto result = runCheckTest("NoForkRacing", R"(
module m;
    int a, b;

    initial fork
        begin a = 1; b = 1; end
        begin a = 2; b = 2; end
    join
endmodule
)",
                               {}, &output);
    CHECK_FALSE(result);
    CHECK(countOf(output, "may be written") == 2);
}

TEST_CASE("NoForkRacing: sscanf output arg -positive") {
    auto result = runCheckTest("NoForkRacing", R"(
module m;
    int x;

    initial fork
        void'($sscanf("5", "%d", x));
        x = 1;
    join
endmodule
)");
    CHECK_FALSE(result);
}

TEST_CASE("NoForkRacing: cast destination -positive") {
    auto result = runCheckTest("NoForkRacing", R"(
module m;
    typedef enum { A, B } e_t;
    e_t e;

    initial fork
        $cast(e, 1);
        e = A;
    join
endmodule
)");
    CHECK_FALSE(result);
}

TEST_CASE("NoForkRacing: std::randomize target -positive") {
    auto result = runCheckTest("NoForkRacing", R"(
module m;
    int x;

    initial fork
        void'(std::randomize(x));
        x = 1;
    join
endmodule
)");
    CHECK_FALSE(result);
}


TEST_CASE("NoForkRacing: mixed this form, disjoint elements") {
    auto result = runCheckTest("NoForkRacing", R"(
module m;
    class C;
        int arr[4];

        task run();
            fork
                arr[0] = 1;
                this.arr[1] = 2;
            join
        endtask
    endclass
endmodule
)");
    CHECK(result); 
}
