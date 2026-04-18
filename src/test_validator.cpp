// =============================================================================
// test_validator.cpp  —  Unit tests for the ResIR semantic validator (Task 3)
// =============================================================================
//
// How to run:
//   make test_validator
//   ./resir_validator_test
//
// Each test parses a ResIR snippet and checks whether validation passes or
// fails with the expected error keyword.
// =============================================================================

#include "lexer.hpp"
#include "parser.hpp"
#include "validator.hpp"
#include "cfg.hpp"

#include <iostream>
#include <string>
#include <vector>
#include <cassert>

// ---------------------------------------------------------------------------
// Test infrastructure
// ---------------------------------------------------------------------------

static int passed = 0;
static int failed = 0;

#define CHECK(name, condition) do { \
    if (condition) { std::cout << "  PASS  " << (name) << "\n"; ++passed; } \
    else           { std::cout << "  FAIL  " << (name) << "\n"; ++failed; } \
} while(0)

// Helper: lex + parse + validate a ResIR snippet.
// Returns true if ALL three stages succeed.
// Fills `err` with the first error from whichever stage failed.
static bool do_validate(const std::string& src, std::string& err) {
    // Lex
    Lexer lex(src);
    std::vector<Token> toks;
    if (!lex.tokenise(toks)) { err = lex.get_error(); return false; }
    // Parse
    Parser p(toks);
    Program prog;
    if (!p.parse(prog)) { err = p.get_error(); return false; }
    // Validate
    Validator v(prog);
    if (!v.validate()) { err = v.get_error(); return false; }
    return true;
}

// Helper: expect validation to SUCCEED.
static void expect_ok(const std::string& name, const std::string& src) {
    std::string err;
    bool ok = do_validate(src, err);
    if (!ok) std::cout << "    [error was: " << err << "]\n";
    CHECK(name, ok);
}

// Helper: expect validation to FAIL, and the error message to contain `keyword`.
static void expect_fail(const std::string& name, const std::string& src,
                        const std::string& keyword) {
    std::string err;
    bool ok = do_validate(src, err);
    bool has_keyword = err.find(keyword) != std::string::npos;
    if (ok)          std::cout << "    [should have failed but passed]\n";
    if (!ok && !has_keyword)
        std::cout << "    [error was: " << err << "]\n";
    CHECK(name, !ok && has_keyword);
}

// ---------------------------------------------------------------------------
// VALID programs (should all pass)
// ---------------------------------------------------------------------------

static void test_valid_programs() {
    std::cout << "\n[Valid programs — must PASS]\n";

    // Canonical abs_diff from the spec
    expect_ok("abs_diff passes validation", R"(
function Math::abs_diff(%a: i32, %b: i32) -> i32 {
  locals { %zero:i32; %d:i32; %is_neg:bool; %r:i32; }
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
)");

    // Struct access with member_ptr + load
    expect_ok("struct access passes validation", R"(
extern type Custom::Struct::Point { x:i32; y:i32; }
function Geom::point_x(%p: ptr<Custom::Struct::Point>) -> i32 {
  locals { %fp:ptr<i32>; %v:i32; }
  entry bb0;
  bb0:
    %fp = member_ptr %p, x;
    %v  = load %fp;
    return %v;
}
)");

    // Void function with unconditional jump (infinite loop is valid IR)
    expect_ok("void function with jump passes", R"(
function Test::spin() -> void {
  locals {}
  entry bb0;
  bb0:
    jump bb0;
}
)");

    // Cast between types
    expect_ok("cast passes validation", R"(
function Test::to_f64(%x: i32) -> f64 {
  locals { %y:f64; }
  entry bb0;
  bb0:
    %y = cast %x to f64;
    return %y;
}
)");

    // addr_of then load (round-trip)
    expect_ok("addr_of + load passes validation", R"(
function Test::roundtrip(%x: i32) -> i32 {
  locals { %p:ptr<i32>; %v:i32; }
  entry bb0;
  bb0:
    %p = addr_of %x;
    %v = load %p;
    return %v;
}
)");

    // Call opcode (we trust the callee signature)
    expect_ok("call passes validation", R"(
function Client::run(%a: i32, %b: i32) -> i32 {
  locals { %r:i32; }
  entry bb0;
  bb0:
    %r = call Math::abs_diff(%a, %b);
    return %r;
}
)");

    // Logical operators on bool
    expect_ok("logical and/or passes validation", R"(
function Test::logic(%a: bool, %b: bool) -> bool {
  locals { %c:bool; }
  entry bb0;
  bb0:
    %c = bin and %a, %b;
    return %c;
}
)");

    // un not on bool
    expect_ok("un not passes validation", R"(
function Test::negate(%a: bool) -> bool {
  locals { %r:bool; }
  entry bb0;
  bb0:
    %r = un not %a;
    return %r;
}
)");

    // f64 arithmetic
    expect_ok("f64 arithmetic passes validation", R"(
function Test::add_f64(%a: f64, %b: f64) -> f64 {
  locals { %r:f64; }
  entry bb0;
  bb0:
    %r = bin add %a, %b;
    return %r;
}
)");

    // store (no destination)
    expect_ok("store passes validation", R"(
function Test::write(%p: ptr<i32>, %v: i32) -> void {
  locals {}
  entry bb0;
  bb0:
    store %p, %v;
    return;
}
)");
}

// ---------------------------------------------------------------------------
// VARIABLE INTEGRITY errors
// ---------------------------------------------------------------------------

static void test_variable_integrity() {
    std::cout << "\n[Variable integrity errors]\n";

    // Using an undeclared local
    expect_fail("undeclared local in bin op", R"(
function Test::f(%a: i32) -> i32 {
  locals { %r:i32; }
  entry bb0;
  bb0:
    %r = bin add %a, %ghost;
    return %r;
}
)", "ghost");

    // Using a local that exists in a different function (not in scope)
    expect_fail("undeclared destination local", R"(
function Test::f() -> i32 {
  locals { %r:i32; }
  entry bb0;
  bb0:
    %mystery = const 1;
    return %r;
}
)", "mystery");

    // Return an undeclared local
    expect_fail("return undeclared local", R"(
function Test::f() -> i32 {
  locals {}
  entry bb0;
  bb0:
    return %ghost;
}
)", "ghost");
}

// ---------------------------------------------------------------------------
// CONTROL FLOW INTEGRITY errors
// ---------------------------------------------------------------------------

static void test_control_flow() {
    std::cout << "\n[Control flow integrity errors]\n";

    // Entry label that doesn't exist
    expect_fail("bad entry label", R"(
function Test::f() -> void {
  locals {}
  entry bb99;
  bb0:
    return;
}
)", "bb99");

    // jump to non-existent label (bb99 is a valid label token but no such block)
    expect_fail("jump to missing label", R"(
function Test::f() -> void {
  locals {}
  entry bb0;
  bb0:
    jump bb99;
}
)", "bb99");

    // cjump to non-existent true-branch (bb55 doesn't exist)
    expect_fail("cjump true-branch missing", R"(
function Test::f(%c: bool) -> void {
  locals {}
  entry bb0;
  bb0:
    cjump %c, bb55, bb0;
}
)", "bb55");

    // Duplicate block label
    expect_fail("duplicate block label", R"(
function Test::f() -> void {
  locals {}
  entry bb0;
  bb0:
    jump bb0;
  bb0:
    return;
}
)", "bb0");
}

// ---------------------------------------------------------------------------
// TYPE CONSISTENCY errors
// ---------------------------------------------------------------------------

static void test_type_consistency() {
    std::cout << "\n[Type consistency errors]\n";

    // cjump condition is not bool
    expect_fail("cjump on i32 is an error", R"(
function Test::f(%x: i32) -> void {
  locals {}
  entry bb0;
  bb0:
    cjump %x, bb0, bb0;
}
)", "bool");

    // Arithmetic on bool (neg of a bool)
    expect_fail("neg on bool is an error", R"(
function Test::f(%b: bool) -> bool {
  locals { %r:bool; }
  entry bb0;
  bb0:
    %r = un neg %b;
    return %r;
}
)", "numeric");

    // Mixed numeric types in arithmetic
    expect_fail("i32 + f64 is an error", R"(
function Test::f(%a: i32, %b: f64) -> i32 {
  locals { %r:i32; }
  entry bb0;
  bb0:
    %r = bin add %a, %b;
    return %r;
}
)", "same numeric");

    // Return type mismatch
    expect_fail("return type mismatch", R"(
function Test::f(%a: bool) -> i32 {
  locals {}
  entry bb0;
  bb0:
    return %a;
}
)", "return type mismatch");

    // Void function returning a value
    expect_fail("void function returning value", R"(
function Test::f(%a: i32) -> void {
  locals {}
  entry bb0;
  bb0:
    return %a;
}
)", "void");

    // Non-void function with bare return
    expect_fail("non-void function with no return value", R"(
function Test::f() -> i32 {
  locals {}
  entry bb0;
  bb0:
    return;
}
)", "non-void");

    // and/or on numeric type
    expect_fail("and on i32 is an error", R"(
function Test::f(%a: i32, %b: i32) -> i32 {
  locals { %r:i32; }
  entry bb0;
  bb0:
    %r = bin and %a, %b;
    return %r;
}
)", "bool");

    // not on numeric type
    expect_fail("not on i32 is an error", R"(
function Test::f(%a: i32) -> bool {
  locals { %r:bool; }
  entry bb0;
  bb0:
    %r = un not %a;
    return %r;
}
)", "bool");
}

// ---------------------------------------------------------------------------
// POINTER & STRUCT errors
// ---------------------------------------------------------------------------

static void test_pointer_struct() {
    std::cout << "\n[Pointer and struct errors]\n";

    // load on non-pointer
    expect_fail("load on i32 is an error", R"(
function Test::f(%x: i32) -> i32 {
  locals { %r:i32; }
  entry bb0;
  bb0:
    %r = load %x;
    return %r;
}
)", "pointer");

    // store to non-pointer
    expect_fail("store to i32 is an error", R"(
function Test::f(%x: i32, %v: i32) -> void {
  locals {}
  entry bb0;
  bb0:
    store %x, %v;
    return;
}
)", "pointer");

    // store value type mismatch
    expect_fail("store type mismatch", R"(
function Test::f(%p: ptr<i32>, %v: f64) -> void {
  locals {}
  entry bb0;
  bb0:
    store %p, %v;
    return;
}
)", "type mismatch");

    // member_ptr on non-pointer
    expect_fail("member_ptr on i32 is an error", R"(
function Test::f(%x: i32) -> void {
  locals { %fp:ptr<i32>; }
  entry bb0;
  bb0:
    %fp = member_ptr %x, y;
    return;
}
)", "pointer");

    // member_ptr on pointer to primitive (not a struct)
    expect_fail("member_ptr on ptr<i32> is an error", R"(
function Test::f(%p: ptr<i32>) -> void {
  locals { %fp:ptr<i32>; }
  entry bb0;
  bb0:
    %fp = member_ptr %p, x;
    return;
}
)", "struct path");

    // member_ptr: undeclared extern type
    expect_fail("member_ptr with undeclared extern type", R"(
function Test::f(%p: ptr<Foo::Bar>) -> void {
  locals { %fp:ptr<i32>; }
  entry bb0;
  bb0:
    %fp = member_ptr %p, x;
    return;
}
)", "extern type");

    // member_ptr: field does not exist
    expect_fail("member_ptr with missing field", R"(
extern type MyStruct { x:i32; }
function Test::f(%p: ptr<MyStruct>) -> void {
  locals { %fp:ptr<i32>; }
  entry bb0;
  bb0:
    %fp = member_ptr %p, z;
    return;
}
)", "no field");
}

// ---------------------------------------------------------------------------
// main
// ---------------------------------------------------------------------------

int main() {
    std::cout << "==============================\n";
    std::cout << " ResIR Validator — Unit Tests\n";
    std::cout << "==============================\n";

    test_valid_programs();
    test_variable_integrity();
    test_control_flow();
    test_type_consistency();
    test_pointer_struct();

    std::cout << "\n==============================\n";
    std::cout << " Results: " << passed << " passed, " << failed << " failed\n";
    std::cout << "==============================\n";

    return (failed == 0) ? 0 : 1;
}
