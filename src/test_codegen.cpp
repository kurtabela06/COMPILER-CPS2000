// =============================================================================
// test_codegen.cpp  —  Unit tests for Task 4: C Code Generator
// =============================================================================
//
// HOW THESE TESTS WORK
// ─────────────────────
// Each test:
//   1. Writes a small ResIR program as a string.
//   2. Runs it through the Lexer → Parser → Validator → CodeGen pipeline.
//   3. Checks that the generated C output CONTAINS expected substrings.
//
// We check substrings (not exact output) because:
//   - Formatting details (spacing, indentation) are implementation choices.
//   - We care about correctness: "int32_t a", "goto bb0", "a - b", etc.
//
// HOW TO RUN:
//   make test_codegen
//   ./resir_codegen_test
// =============================================================================

#include "lexer.hpp"
#include "token.hpp"
#include "parser.hpp"
#include "cfg.hpp"
#include "validator.hpp"
#include "codegen.hpp"

#include <iostream>
#include <string>
#include <vector>
#include <sstream>

// =============================================================================
// Test harness
// =============================================================================

static int g_pass = 0;
static int g_fail = 0;

// check() — verify that `output` contains the substring `expected`.
// If it does, the test passes; otherwise it fails and prints a diagnostic.
static void check(const std::string& test_name,
                  const std::string& output,
                  const std::string& expected) {
    if (output.find(expected) != std::string::npos) {
        std::cout << "[PASS] " << test_name
                  << " : contains \"" << expected << "\"\n";
        ++g_pass;
    } else {
        std::cout << "[FAIL] " << test_name
                  << " : expected to find \"" << expected << "\"\n"
                  << "       Full output:\n" << output << "\n";
        ++g_fail;
    }
}

// check_absent() — verify that `output` does NOT contain `absent`.
static void check_absent(const std::string& test_name,
                          const std::string& output,
                          const std::string& absent) {
    if (output.find(absent) == std::string::npos) {
        std::cout << "[PASS] " << test_name
                  << " : correctly absent \"" << absent << "\"\n";
        ++g_pass;
    } else {
        std::cout << "[FAIL] " << test_name
                  << " : unexpectedly found \"" << absent << "\"\n";
        ++g_fail;
    }
}

// run_codegen() — full pipeline helper.
// Returns the generated C string, or an empty string on any earlier error.
static std::string run_codegen(const std::string& source,
                                std::string& error_out) {
    // Step 1: lex
    Lexer lexer(source);
    std::vector<Token> tokens;
    if (!lexer.tokenise(tokens)) {
        error_out = "LEX ERROR: " + lexer.get_error();
        return "";
    }

    // Step 2: parse
    Parser parser(tokens);
    Program prog;
    if (!parser.parse(prog)) {
        error_out = "PARSE ERROR: " + parser.get_error();
        return "";
    }

    // Step 3: validate
    Validator validator(prog);
    if (!validator.validate()) {
        error_out = "VALIDATE ERROR: " + validator.get_error();
        return "";
    }

    // Step 4: code gen
    CodeGen gen(prog);
    return gen.emit();
}

// =============================================================================
// Test 1 — Basic type mapping
// =============================================================================
// Checks that all five primitive types appear as the correct C equivalents.
static void test_type_mapping() {
    // A function with one param of each type so we can see all the C types.
    const std::string src = R"(
function Test::types(%a: i32, %b: i64, %c: u32, %d: f64, %e: bool) -> i32 {
    locals { %r : i32; }
    entry bb0;
    bb0:
        %r = %a;
        return %r;
}
)";
    std::string err;
    std::string out = run_codegen(src, err);
    if (!err.empty()) { std::cout << "[FAIL] type_mapping: " << err << "\n"; ++g_fail; return; }

    check("type_mapping: i32 param",  out, "int32_t a");
    check("type_mapping: i64 param",  out, "int64_t b");
    check("type_mapping: u32 param",  out, "uint32_t c");
    check("type_mapping: f64 param",  out, "double d");
    check("type_mapping: bool param", out, "bool e");
}

// =============================================================================
// Test 2 — Path mangling  (:: → _)
// =============================================================================
static void test_path_mangling() {
    const std::string src = R"(
function Math::abs_diff(%a: i32, %b: i32) -> i32 {
    locals { %r : i32; }
    entry bb0;
    bb0:
        %r = %a;
        return %r;
}
)";
    std::string err;
    std::string out = run_codegen(src, err);
    if (!err.empty()) { std::cout << "[FAIL] path_mangling: " << err << "\n"; ++g_fail; return; }

    // "Math::abs_diff" in ResIR must become "Math_abs_diff" in C.
    check("path_mangling: function name", out, "Math_abs_diff");
    // The original "::" must not appear anywhere in the generated C.
    check_absent("path_mangling: no colon-colon", out, "::");
}

// =============================================================================
// Test 3 — abs_diff full translation (from the spec)
// =============================================================================
static void test_abs_diff() {
    const std::string src = R"(
function Math::abs_diff(%a: i32, %b: i32) -> i32 {
    locals {
        %zero   : i32;
        %d      : i32;
        %is_neg : bool;
        %r      : i32;
    }
    entry bb0;

    bb0:
        %zero   = const 0;
        %d      = bin sub %a, %b;
        %is_neg = bin lt %d, %zero;
        cjump %is_neg, bb1, bb2;

    bb1:
        %r = un neg %d;
        return %r;

    bb2:
        return %d;
}
)";
    std::string err;
    std::string out = run_codegen(src, err);
    if (!err.empty()) { std::cout << "[FAIL] abs_diff: " << err << "\n"; ++g_fail; return; }

    // Headers
    check("abs_diff: stdint header",  out, "#include <stdint.h>");
    check("abs_diff: stdbool header", out, "#include <stdbool.h>");

    // Function signature
    check("abs_diff: return type",    out, "int32_t");
    check("abs_diff: fn name",        out, "Math_abs_diff");

    // Locals declared at top of function
    check("abs_diff: local zero",   out, "int32_t zero");
    check("abs_diff: local d",      out, "int32_t d");
    check("abs_diff: local is_neg", out, "bool is_neg");
    check("abs_diff: local r",      out, "int32_t r");

    // Entry goto
    check("abs_diff: entry goto",   out, "goto bb0");

    // Block bb0 label
    check("abs_diff: bb0 label",    out, "bb0:");

    // const 0 → 0
    check("abs_diff: const 0",      out, "zero = 0");

    // bin sub → -
    check("abs_diff: bin sub",      out, "d = a - b");

    // bin lt → <   (producing bool)
    check("abs_diff: bin lt",       out, "is_neg = d < zero");

    // cjump
    check("abs_diff: cjump",        out, "if (is_neg) goto bb1; else goto bb2");

    // Block bb1 and bb2
    check("abs_diff: bb1 label",    out, "bb1:");
    check("abs_diff: bb2 label",    out, "bb2:");

    // un neg → -
    check("abs_diff: un neg",       out, "r = -d");

    // return
    check("abs_diff: return r",     out, "return r");
    check("abs_diff: return d",     out, "return d");
}

// =============================================================================
// Test 4 — struct_access (member_ptr + load)
// =============================================================================
static void test_struct_access() {
    const std::string src = R"(
extern type Custom::Struct::Point {
    x : i32;
    y : i32;
}

function Geom::point_x(%p: ptr<Custom::Struct::Point>) -> i32 {
    locals {
        %fp : ptr<i32>;
        %v  : i32;
    }
    entry bb0;

    bb0:
        %fp = member_ptr %p, x;
        %v  = load %fp;
        return %v;
}
)";
    std::string err;
    std::string out = run_codegen(src, err);
    if (!err.empty()) { std::cout << "[FAIL] struct_access: " << err << "\n"; ++g_fail; return; }

    // Forward struct declaration
    check("struct_access: typedef",  out, "typedef struct Custom_Struct_Point Custom_Struct_Point");

    // Pointer param type
    check("struct_access: ptr param", out, "Custom_Struct_Point* p");

    // member_ptr → &ptr->field
    check("struct_access: member_ptr", out, "fp = &p->x");

    // load → dereference
    check("struct_access: load", out, "v = *fp");

    // return
    check("struct_access: return", out, "return v");
}

// =============================================================================
// Test 5 — const literals (int, float, bool, null)
// =============================================================================
static void test_const_literals() {
    const std::string src = R"(
function Test::consts(%dummy: i32) -> i32 {
    locals {
        %i : i32;
        %f : f64;
        %b : bool;
        %r : i32;
    }
    entry bb0;

    bb0:
        %i = const 42;
        %f = const 3.14;
        %b = const true;
        %r = %i;
        return %r;
}
)";
    std::string err;
    std::string out = run_codegen(src, err);
    if (!err.empty()) { std::cout << "[FAIL] const_literals: " << err << "\n"; ++g_fail; return; }

    check("const_literals: int 42",   out, "i = 42");
    check("const_literals: float",    out, "f = 3.14");
    check("const_literals: bool",     out, "b = true");
}

// =============================================================================
// Test 6 — cast opcode
// =============================================================================
static void test_cast() {
    const std::string src = R"(
function Test::cast_test(%a: i32) -> f64 {
    locals { %r : f64; }
    entry bb0;

    bb0:
        %r = cast %a to f64;
        return %r;
}
)";
    std::string err;
    std::string out = run_codegen(src, err);
    if (!err.empty()) { std::cout << "[FAIL] cast: " << err << "\n"; ++g_fail; return; }

    // cast %a to f64  →  (double)a
    check("cast: c cast expression", out, "r = (double)a");
    check("cast: return type double", out, "double Test_cast_test");
}

// =============================================================================
// Test 7 — addr_of opcode
// =============================================================================
static void test_addr_of() {
    const std::string src = R"(
function Test::addr(%a: i32) -> i32 {
    locals {
        %p : ptr<i32>;
        %v : i32;
    }
    entry bb0;

    bb0:
        %p = addr_of %a;
        %v = load %p;
        return %v;
}
)";
    std::string err;
    std::string out = run_codegen(src, err);
    if (!err.empty()) { std::cout << "[FAIL] addr_of: " << err << "\n"; ++g_fail; return; }

    // addr_of → &
    check("addr_of: ampersand", out, "p = &a");
    // ptr<i32> local
    check("addr_of: ptr local decl", out, "int32_t* p");
    // load → dereference
    check("addr_of: load", out, "v = *p");
}

// =============================================================================
// Test 8 — store opcode (no destination)
// =============================================================================
static void test_store() {
    const std::string src = R"(
function Test::store_test(%p: ptr<i32>, %v: i32) -> i32 {
    locals { %r : i32; }
    entry bb0;

    bb0:
        store %p, %v;
        %r = load %p;
        return %r;
}
)";
    std::string err;
    std::string out = run_codegen(src, err);
    if (!err.empty()) { std::cout << "[FAIL] store: " << err << "\n"; ++g_fail; return; }

    // store → *ptr = val;
    check("store: pointer assignment", out, "*p = v");
    // load after store
    check("store: load after store", out, "r = *p");
}

// =============================================================================
// Test 9 — call opcode
// =============================================================================
static void test_call() {
    const std::string src = R"(
function Test::caller(%x: i32) -> i32 {
    locals { %result : i32; }
    entry bb0;

    bb0:
        %result = call Math::abs_diff(%x, %x);
        return %result;
}
)";
    std::string err;
    std::string out = run_codegen(src, err);
    if (!err.empty()) { std::cout << "[FAIL] call: " << err << "\n"; ++g_fail; return; }

    // call path mangled + args
    check("call: mangled call", out, "Math_abs_diff(x, x)");
    check("call: assignment",   out, "result = Math_abs_diff");
}

// =============================================================================
// Test 10 — binary ops: all comparison operators
// =============================================================================
static void test_binops_compare() {
    const std::string src = R"(
function Test::cmp(%a: i32, %b: i32) -> bool {
    locals {
        %r1 : bool;
        %r2 : bool;
        %r3 : bool;
        %r4 : bool;
        %r5 : bool;
        %r6 : bool;
        %out : bool;
    }
    entry bb0;

    bb0:
        %r1 = bin eq %a, %b;
        %r2 = bin ne %a, %b;
        %r3 = bin lt %a, %b;
        %r4 = bin le %a, %b;
        %r5 = bin gt %a, %b;
        %r6 = bin ge %a, %b;
        %out = %r1;
        return %out;
}
)";
    std::string err;
    std::string out = run_codegen(src, err);
    if (!err.empty()) { std::cout << "[FAIL] binops_compare: " << err << "\n"; ++g_fail; return; }

    check("binops: eq",  out, "r1 = a == b");
    check("binops: ne",  out, "r2 = a != b");
    check("binops: lt",  out, "r3 = a < b");
    check("binops: le",  out, "r4 = a <= b");
    check("binops: gt",  out, "r5 = a > b");
    check("binops: ge",  out, "r6 = a >= b");
}

// =============================================================================
// Test 11 — binary ops: logical and/or
// =============================================================================
static void test_binops_logical() {
    const std::string src = R"(
function Test::logic(%a: bool, %b: bool) -> bool {
    locals {
        %r1 : bool;
        %r2 : bool;
        %out : bool;
    }
    entry bb0;

    bb0:
        %r1 = bin and %a, %b;
        %r2 = bin or  %a, %b;
        %out = %r1;
        return %out;
}
)";
    std::string err;
    std::string out = run_codegen(src, err);
    if (!err.empty()) { std::cout << "[FAIL] binops_logical: " << err << "\n"; ++g_fail; return; }

    check("binops: and", out, "r1 = a && b");
    check("binops: or",  out, "r2 = a || b");
}

// =============================================================================
// Test 12 — void function (no return value)
// =============================================================================
static void test_void_function() {
    const std::string src = R"(
function Test::void_fn(%a: i32) -> void {
    locals {}
    entry bb0;

    bb0:
        jump bb1;

    bb1:
        return;
}
)";
    std::string err;
    std::string out = run_codegen(src, err);
    if (!err.empty()) { std::cout << "[FAIL] void_function: " << err << "\n"; ++g_fail; return; }

    // Return type must be "void"
    check("void_function: return type", out, "void Test_void_fn");
    // Bare return
    check("void_function: bare return", out, "return;");
    // Unconditional jump
    check("void_function: jump",        out, "goto bb1");
}

// =============================================================================
// Test 13 — nested pointer type  ptr<ptr<i32>>
// =============================================================================
static void test_nested_ptr() {
    const std::string src = R"(
function Test::nested(%pp: ptr<ptr<i32>>) -> i32 {
    locals {
        %p : ptr<i32>;
        %v : i32;
    }
    entry bb0;

    bb0:
        %p = load %pp;
        %v = load %p;
        return %v;
}
)";
    std::string err;
    std::string out = run_codegen(src, err);
    if (!err.empty()) { std::cout << "[FAIL] nested_ptr: " << err << "\n"; ++g_fail; return; }

    // ptr<ptr<i32>> → int32_t**
    check("nested_ptr: double ptr param", out, "int32_t** pp");
    // ptr<i32> local
    check("nested_ptr: single ptr local", out, "int32_t* p");
    // first load: *pp → p
    check("nested_ptr: outer load", out, "p = *pp");
    // second load: *p → v
    check("nested_ptr: inner load", out, "v = *p");
}

// =============================================================================
// Test 14 — arithmetic binary ops: add, sub, mul, div, mod
// =============================================================================
static void test_binops_arith() {
    const std::string src = R"(
function Test::arith(%a: i32, %b: i32) -> i32 {
    locals {
        %r1 : i32;
        %r2 : i32;
        %r3 : i32;
        %r4 : i32;
        %r5 : i32;
        %out : i32;
    }
    entry bb0;

    bb0:
        %r1 = bin add %a, %b;
        %r2 = bin sub %a, %b;
        %r3 = bin mul %a, %b;
        %r4 = bin div %a, %b;
        %r5 = bin mod %a, %b;
        %out = %r1;
        return %out;
}
)";
    std::string err;
    std::string out = run_codegen(src, err);
    if (!err.empty()) { std::cout << "[FAIL] binops_arith: " << err << "\n"; ++g_fail; return; }

    check("arith: add", out, "r1 = a + b");
    check("arith: sub", out, "r2 = a - b");
    check("arith: mul", out, "r3 = a * b");
    check("arith: div", out, "r4 = a / b");
    check("arith: mod", out, "r5 = a % b");
}

// =============================================================================
// Test 15 — un not operator
// =============================================================================
static void test_un_not() {
    const std::string src = R"(
function Test::not_test(%a: bool) -> bool {
    locals { %r : bool; }
    entry bb0;

    bb0:
        %r = un not %a;
        return %r;
}
)";
    std::string err;
    std::string out = run_codegen(src, err);
    if (!err.empty()) { std::cout << "[FAIL] un_not: " << err << "\n"; ++g_fail; return; }

    check("un_not: logical not", out, "r = !a");
}

// =============================================================================
// Test 16 — resir_structs.h placeholder always present
// =============================================================================
static void test_struct_header_present() {
    const std::string src = R"(
function Test::minimal(%a: i32) -> i32 {
    locals {}
    entry bb0;

    bb0:
        return %a;
}
)";
    std::string err;
    std::string out = run_codegen(src, err);
    if (!err.empty()) { std::cout << "[FAIL] struct_header: " << err << "\n"; ++g_fail; return; }

    check("struct_header: placeholder include", out, "#include \"resir_structs.h\"");
}

// =============================================================================
// Test 17 — const false and const null
// =============================================================================
static void test_const_false_null() {
    const std::string src = R"(
function Test::falseness(%dummy: bool) -> bool {
    locals {
        %b   : bool;
        %ptr : ptr<i32>;
        %out : bool;
    }
    entry bb0;

    bb0:
        %b   = const false;
        %ptr = const null;
        %out = %b;
        return %out;
}
)";
    std::string err;
    std::string out = run_codegen(src, err);
    if (!err.empty()) { std::cout << "[FAIL] const_false_null: " << err << "\n"; ++g_fail; return; }

    check("const_false_null: false", out, "b = false");
    check("const_false_null: null",  out, "ptr = NULL");
}

// =============================================================================
// Test 18 — f64 arithmetic
// =============================================================================
static void test_f64_arith() {
    const std::string src = R"(
function Test::f64_add(%a: f64, %b: f64) -> f64 {
    locals { %r : f64; }
    entry bb0;

    bb0:
        %r = bin add %a, %b;
        return %r;
}
)";
    std::string err;
    std::string out = run_codegen(src, err);
    if (!err.empty()) { std::cout << "[FAIL] f64_arith: " << err << "\n"; ++g_fail; return; }

    check("f64_arith: return type",   out, "double Test_f64_add");
    check("f64_arith: double params", out, "double a");
    check("f64_arith: add",           out, "r = a + b");
}

// =============================================================================
// main — run all tests and print summary
// =============================================================================
int main() {
    std::cout << "=== Task 4: Code Generation Tests ===\n\n";

    test_type_mapping();
    test_path_mangling();
    test_abs_diff();
    test_struct_access();
    test_const_literals();
    test_cast();
    test_addr_of();
    test_store();
    test_call();
    test_binops_compare();
    test_binops_logical();
    test_void_function();
    test_nested_ptr();
    test_binops_arith();
    test_un_not();
    test_struct_header_present();
    test_const_false_null();
    test_f64_arith();

    std::cout << "\n==============================\n";
    std::cout << "Total: " << (g_pass + g_fail) << " checks, "
              << g_pass << " pass, " << g_fail << " fail\n";
    return g_fail > 0 ? 1 : 0;
}
