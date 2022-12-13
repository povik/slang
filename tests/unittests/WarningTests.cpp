// SPDX-FileCopyrightText: Michael Popoloski
// SPDX-License-Identifier: MIT

#include "Test.h"

TEST_CASE("Diagnose unused modules / interfaces") {
    auto tree = SyntaxTree::fromText(R"(
interface I;
endinterface

interface J;
endinterface

module bar (I i);
endmodule

module top;
endmodule

module top2({a[1:0], a[3:2]});
    ref int a;
endmodule

module top3(ref int a);
endmodule
)");

    CompilationOptions coptions;
    coptions.suppressUnused = false;
    coptions.scriptMode = false;

    Compilation compilation(coptions);
    compilation.addSyntaxTree(tree);

    auto& diags = compilation.getAllDiagnostics();
    REQUIRE(diags.size() == 4);
    CHECK(diags[0].code == diag::UnusedDefinition);
    CHECK(diags[1].code == diag::TopModuleIfacePort);
    CHECK(diags[2].code == diag::TopModuleUnnamedRefPort);
    CHECK(diags[3].code == diag::TopModuleRefPort);
}

TEST_CASE("Unused nets and vars") {
    auto tree = SyntaxTree::fromText(R"(
module m #(int foo)(input baz, output bar);
    int i;
    if (foo > 1) assign i = 0;

    int x = 1;
    int z;
    int y = x + z;

    wire j = 1;
    wire k;
    wire l = k;
    wire m;

    assign m = 1;
endmodule

module top;
    logic baz,bar;
    m #(1) m1(.*);
    m #(2) m2(bar, baz);
    m #(3) m3(a, b);
endmodule
)");

    CompilationOptions coptions;
    coptions.suppressUnused = false;

    Compilation compilation(coptions);
    compilation.addSyntaxTree(tree);

    auto& diags = compilation.getAllDiagnostics();
    REQUIRE(diags.size() == 11);
    CHECK(diags[0].code == diag::UnusedPort);
    CHECK(diags[1].code == diag::UndrivenPort);
    CHECK(diags[2].code == diag::UnusedButSetVariable);
    CHECK(diags[3].code == diag::UnassignedVariable);
    CHECK(diags[4].code == diag::UnusedVariable);
    CHECK(diags[5].code == diag::UnusedNet);
    CHECK(diags[6].code == diag::UndrivenNet);
    CHECK(diags[7].code == diag::UnusedNet);
    CHECK(diags[8].code == diag::UnusedButSetNet);
    CHECK(diags[9].code == diag::UnusedImplicitNet);
    CHECK(diags[10].code == diag::UnusedImplicitNet);
}

TEST_CASE("Unused nets and vars false positives regress") {
    auto tree = SyntaxTree::fromText(R"(
interface I(input clk);
    logic baz;
    modport m(input clk, baz);
    modport n(output baz);
endinterface

module m(output v);
    wire clk = 1;
    I i(clk);

    int x,z;
    if (0) begin
        assign x = 1;
        always_ff @(posedge clk) v <= x;

        assign z = 1;
    end
    else begin
        assign z = 1;
    end

    int y = z;
    initial $dumpvars(m.y);

    event e[4];
    initial begin
       for (int i = 0; i < 4; i++) begin
           ->e[i];
       end
       @ e[0] begin end
    end

    initial begin
        int b[$];
        static int q = 1;
        string s1;
        s1.itoa(q);
        b.push_back(1);
    end
endmodule

(* unused *) module n #(parameter int i)(input x, output y, output z);
    logic [i-1:0] a = 1;
    assign y = a[x];
endmodule

package p;
    int i;
endpackage

module q(
    output logic [7:0] lhs,
    input  logic [7:0] rhs,
    input  logic [2:0] lhs_lsb,
    input  logic [2:0] rhs_lsb
);
   always_comb begin
       lhs = 0;
       lhs[lhs_lsb +: 2] = rhs[rhs_lsb +: 2];
   end
endmodule

class C;
    extern function int foo(int a);
    virtual function bar(int b);
        int c[$];
        c.push_back(1);
    endfunction
endclass

function int C::foo(int a);
    return a;
endfunction

import "DPI-C" function void dpi_func(int i);

class D;
    int s1[$];
    int s2[int];
    function void f();
        int i = 0;
        foreach (s2[j]) begin
            int k = j * 4;
            s1[i++] = k;
        end
    endfunction
endclass
)");

    CompilationOptions coptions;
    coptions.suppressUnused = false;

    Compilation compilation(coptions);
    compilation.addSyntaxTree(tree);
    NO_COMPILATION_ERRORS;
}

TEST_CASE("Ref args are considered used") {
    auto tree = SyntaxTree::fromText(R"(
class C;
    function void f1(ref bit [3:0] a);
        a = 4'hF;
    endfunction

    function int unsigned f2();
        bit [3:0] a;
        f1(a);
    endfunction
endclass

module top;
endmodule
)");

    CompilationOptions coptions;
    coptions.suppressUnused = false;

    Compilation compilation(coptions);
    compilation.addSyntaxTree(tree);
    NO_COMPILATION_ERRORS;
}

TEST_CASE("'unused' warnings with clock vars") {
    auto tree = SyntaxTree::fromText(R"(
interface I;
    logic clk;
    logic a;

    clocking cb @(posedge clk);
        input a;
    endclocking
endinterface

class TB;
    virtual I intf;
    task run();
        @(intf.cb);
        if (intf.cb.a) begin
            $display("error!");
        end
    endtask
endclass

module M(
    input logic clk,
    output logic a
);
   always_ff @(posedge clk) begin
       a <= 1'b1;
   end
endmodule

module top;
    logic a;
    logic clk;
    I i();

    M m(.*);

    assign i.clk = clk;
    assign i.a = a;

    initial begin
        clk = 0;
        forever begin
            #1ns;
            clk = ~clk;
        end
    end
endmodule
)");

    CompilationOptions coptions;
    coptions.suppressUnused = false;

    Compilation compilation(coptions);
    compilation.addSyntaxTree(tree);
    NO_COMPILATION_ERRORS;
}

TEST_CASE("'unassigned' warnings with clockvar outputs") {
    auto tree = SyntaxTree::fromText(R"(
interface I;
    logic clk;
    logic a;

    clocking cb_driver @(posedge clk);
        output a;
    endclocking
endinterface

class C;
    virtual I i;
    task drive();
        @(i.cb_driver);
        i.cb_driver.a <= 1'b0;
    endtask

    logic q = i.a;
endclass

module top;
   I i();
   C c;
   initial begin
       i.clk = 0;
       forever begin
           #1ns i.clk = ~i.clk;
       end
   end
   initial begin
       c = new();
       c.i = i;
       c.drive();
   end
endmodule
)");

    CompilationOptions coptions;
    coptions.suppressUnused = false;

    Compilation compilation(coptions);
    compilation.addSyntaxTree(tree);
    NO_COMPILATION_ERRORS;
}

TEST_CASE("Unused function args") {
    auto tree = SyntaxTree::fromText(R"(
function foo(input x, output y);
    y = 1;
endfunction

module m;
endmodule
)");

    CompilationOptions coptions;
    coptions.suppressUnused = false;

    Compilation compilation(coptions);
    compilation.addSyntaxTree(tree);

    auto& diags = compilation.getAllDiagnostics();
    REQUIRE(diags.size() == 1);
    CHECK(diags[0].code == diag::UnusedArgument);
}

TEST_CASE("System function args count as outputs") {
    auto tree = SyntaxTree::fromText(R"(
class C;
    function bit f();
        bit a;
        int rc = std::randomize(a);
        assert(rc);
        return a;
    endfunction
endclass

module m;
    int i;
    string a,s = "a 3";
    int b;
    initial begin
        $cast(i, i);
        void'($sscanf(s, "%s %d", a, b));
    end

    (* unused *) int q = b + a.len;
endmodule
)");

    CompilationOptions coptions;
    coptions.suppressUnused = false;

    Compilation compilation(coptions);
    compilation.addSyntaxTree(tree);
    NO_COMPILATION_ERRORS;
}

TEST_CASE("Class handle access 'unused' warnings") {
    auto tree = SyntaxTree::fromText(R"(
class A;
    int i;
endclass

class C;
    task t1(A a);
        a.i = 3;
    endtask

    task t2(A a);
        A a1 = a;
        a1.i = 3;
    endtask
endclass

module m;
endmodule
)");

    CompilationOptions coptions;
    coptions.suppressUnused = false;

    Compilation compilation(coptions);
    compilation.addSyntaxTree(tree);
    NO_COMPILATION_ERRORS;
}

TEST_CASE("Virtual interface handle access 'unused' warnings") {
    auto tree = SyntaxTree::fromText(R"(
interface I;
    logic clk;
endinterface

class C;
    event sys_clk;

    virtual I i;
    function virtual I get_intf();
        return i;
    endfunction

    task t();
        virtual I intf = get_intf();
        @(intf.clk);
        ->sys_clk;
    endtask
endclass

module top;
    I intf();
    initial begin
        intf.clk = 0;
        forever begin
            #1ns;
            intf.clk = ~intf.clk;
        end
    end
endmodule
)");

    CompilationOptions coptions;
    coptions.suppressUnused = false;

    Compilation compilation(coptions);
    compilation.addSyntaxTree(tree);
    NO_COMPILATION_ERRORS;
}

TEST_CASE("Exclude 'unused' warnings based on attributes, underscore name") {
    auto tree = SyntaxTree::fromText(R"(
module m;
    int _;
    (* maybe_unused *) int foo;
endmodule
)");

    CompilationOptions coptions;
    coptions.suppressUnused = false;

    Compilation compilation(coptions);
    compilation.addSyntaxTree(tree);
    NO_COMPILATION_ERRORS;
}

TEST_CASE("Unused parameters") {
    auto tree = SyntaxTree::fromText(R"(
module m #(parameter p = 1, q = 2, parameter type t = int, u = real);
    (* unused *) u r = 3.14;
    (* unused *) int i = q;
endmodule
)");

    CompilationOptions coptions;
    coptions.suppressUnused = false;

    Compilation compilation(coptions);
    compilation.addSyntaxTree(tree);

    auto& diags = compilation.getAllDiagnostics();
    REQUIRE(diags.size() == 2);
    CHECK(diags[0].code == diag::UnusedParameter);
    CHECK(diags[1].code == diag::UnusedTypeParameter);
}

TEST_CASE("Unused typedefs") {
    auto tree = SyntaxTree::fromText(R"(
class C;
    parameter p = 1;
endclass

module m;
    typedef struct { int a, b; } asdf;
    typedef enum { A, B } foo;

    (* unused *) foo f = A;

    typedef C D;
    (* unused *) parameter p = D::p;

    typedef enum { E, F } bar;

    (* unused *) parameter q = E;
endmodule
)");

    CompilationOptions coptions;
    coptions.suppressUnused = false;

    Compilation compilation(coptions);
    compilation.addSyntaxTree(tree);

    auto& diags = compilation.getAllDiagnostics();
    REQUIRE(diags.size() == 1);
    CHECK(diags[0].code == diag::UnusedTypedef);
}

TEST_CASE("Covergroups and class handles are 'used' if constructed") {
    auto tree = SyntaxTree::fromText(R"(
interface I;
    logic a = 1;
    covergroup cg;
        a: coverpoint a;
    endgroup

    cg cov = new();
endinterface

class C;
    function new; $display("Hello!"); endfunction
endclass

module m;
    I i();
    C c1 = new;
endmodule
)");

    CompilationOptions coptions;
    coptions.suppressUnused = false;

    Compilation compilation(coptions);
    compilation.addSyntaxTree(tree);
    NO_COMPILATION_ERRORS;
}
