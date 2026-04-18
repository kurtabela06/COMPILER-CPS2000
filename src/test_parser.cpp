// =============================================================================
// test_parser.cpp  —  Unit tests for the ResIR parser (Task 2)
// =============================================================================
//
// How to run:
//   make test_parser
//   ./resir_parser_test
//
// Each test() call parses a small ResIR snippet and checks that the resulting
// Program struct has the correct shape.
// =============================================================================

#include "lexer.hpp"
#include "parser.hpp"
#include "cfg.hpp"
#include "token.hpp"

#include <iostream>
#include <vector>
#include <string>
#include <cassert>

// ---------------------------------------------------------------------------
// Test infrastructure
// ---------------------------------------------------------------------------

static int passed = 0;
static int failed = 0;

// CHECK macro — prints PASS/FAIL
#define CHECK(name, condition) do { \
    if (condition) { std::cout << "  PASS  " << (name) << "\n"; ++passed; } \
    else           { std::cout << "  FAIL  " << (name) << "\n"; ++failed; } \
} while(0)

// Helper: lex then parse a source string.
// Returns true on success and fills `prog`.
static bool do_parse(const std::string& src, Program& prog, std::string& err) {
    Lexer lex(src);
    std::vector<Token> toks;
    if (!lex.tokenise(toks)) { err = lex.get_error(); return false; }
    Parser p(toks);
    bool ok = p.parse(prog);
    if (!ok) err = p.get_error();
    return ok;
}

// Helper: parse src, assert success, return Program.
static Program must_parse(const std::string& src) {
    Program prog;
    std::string err;
    bool ok = do_parse(src, prog, err);
    if (!ok) {
        std::cerr << "[must_parse failed] " << err << "\n";
        assert(false);
    }
    return prog;
}

// ---------------------------------------------------------------------------
// Test: minimal function (void return, no params, no locals, single block)
// ---------------------------------------------------------------------------
static void test_minimal_function() {
    std::cout << "\n[Minimal void function]\n";

    const std::string src = R"(
function Foo::bar() -> void {
  locals {}
  entry bb0;
  bb0:
    return;
}
)";
    Program prog = must_parse(src);
    const Function& fn = prog.function;

    CHECK("function path is Foo::bar",  fn.path == "Foo::bar");
    CHECK("no parameters",              fn.params.empty());
    CHECK("returns void",               fn.returns_void);
    CHECK("no locals",                  fn.locals.empty());
    CHECK("entry is bb0",               fn.entry_label == "bb0");
    CHECK("one block",                  fn.blocks.size() == 1);
    CHECK("block label is bb0",         fn.blocks[0].label == "bb0");
    CHECK("no statements in bb0",       fn.blocks[0].stmts.empty());
    CHECK("terminator is return",       fn.blocks[0].term.kind == TermKind::RETURN);
    CHECK("return has no value",        !fn.blocks[0].term.has_ret_val);
}

// ---------------------------------------------------------------------------
// Test: abs_diff — the canonical example from the spec
// ---------------------------------------------------------------------------
static void test_abs_diff() {
    std::cout << "\n[abs_diff — canonical spec example]\n";

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
    Program prog = must_parse(src);
    const Function& fn = prog.function;

    CHECK("path is Math::abs_diff",     fn.path == "Math::abs_diff");
    CHECK("2 parameters",               fn.params.size() == 2);
    CHECK("param 0 name is a",          fn.params[0].name == "a");
    CHECK("param 0 type is i32",        fn.params[0].type.kind == TypeKind::I32);
    CHECK("param 1 name is b",          fn.params[1].name == "b");
    CHECK("return type is i32",         !fn.returns_void && fn.return_type.kind == TypeKind::I32);
    CHECK("4 locals",                   fn.locals.size() == 4);
    CHECK("local 0 name zero",          fn.locals[0].name == "zero");
    CHECK("local 2 name is_neg/bool",   fn.locals[2].name == "is_neg" &&
                                        fn.locals[2].type.kind == TypeKind::BOOL);
    CHECK("entry is bb0",               fn.entry_label == "bb0");
    CHECK("3 blocks",                   fn.blocks.size() == 3);

    // --- bb0 checks ---
    const Block& bb0 = fn.blocks[0];
    CHECK("bb0 label",                  bb0.label == "bb0");
    CHECK("bb0 has 3 statements",       bb0.stmts.size() == 3);

    // stmt 0: %zero = const 0
    CHECK("stmt0 dest is zero",         bb0.stmts[0].has_dest && bb0.stmts[0].dest == "zero");
    CHECK("stmt0 rhs is CONST",         bb0.stmts[0].rhs.kind == RhsKind::CONST);
    CHECK("stmt0 const val = 0",        bb0.stmts[0].rhs.literal.int_val == 0);

    // stmt 1: %d = bin sub %a, %b
    CHECK("stmt1 dest is d",            bb0.stmts[1].dest == "d");
    CHECK("stmt1 rhs is BIN",           bb0.stmts[1].rhs.kind == RhsKind::BIN);
    CHECK("stmt1 binop is sub",         bb0.stmts[1].rhs.bin_op == BinOp::SUB);
    CHECK("stmt1 op1 is a",             bb0.stmts[1].rhs.op1 == "a");
    CHECK("stmt1 op2 is b",             bb0.stmts[1].rhs.op2 == "b");

    // stmt 2: %is_neg = bin lt %d, %zero
    CHECK("stmt2 binop is lt",          bb0.stmts[2].rhs.bin_op == BinOp::LT);

    // terminator: cjump %is_neg, bb1, bb2
    CHECK("bb0 term is cjump",          bb0.term.kind == TermKind::CJUMP);
    CHECK("cjump cond is is_neg",       bb0.term.cond == "is_neg");
    CHECK("cjump true  is bb1",         bb0.term.label1 == "bb1");
    CHECK("cjump false is bb2",         bb0.term.label2 == "bb2");

    // --- bb1 checks ---
    const Block& bb1 = fn.blocks[1];
    CHECK("bb1 has 1 statement",        bb1.stmts.size() == 1);
    CHECK("bb1 stmt0 is UN neg",        bb1.stmts[0].rhs.kind == RhsKind::UN &&
                                        bb1.stmts[0].rhs.un_op == UnOp::NEG);
    CHECK("bb1 terminates with return", bb1.term.kind == TermKind::RETURN);
    CHECK("bb1 returns %r",             bb1.term.has_ret_val && bb1.term.ret_local == "r");

    // --- bb2 checks ---
    const Block& bb2 = fn.blocks[2];
    CHECK("bb2 returns %d",             bb2.term.has_ret_val && bb2.term.ret_local == "d");
}

// ---------------------------------------------------------------------------
// Test: struct_access — member_ptr + load
// ---------------------------------------------------------------------------
static void test_struct_access() {
    std::cout << "\n[Struct access — extern type + member_ptr + load]\n";

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
    Program prog = must_parse(src);

    // Extern type checks
    CHECK("one extern type",            prog.extern_types.size() == 1);
    const ExternType& et = prog.extern_types[0];
    CHECK("extern path",                et.path == "Custom::Struct::Point");
    CHECK("two fields",                 et.fields.size() == 2);
    CHECK("field 0 name x",            et.fields[0].name == "x");
    CHECK("field 0 type i32",           et.fields[0].type.kind == TypeKind::I32);
    CHECK("field 1 name y",             et.fields[1].name == "y");

    const Function& fn = prog.function;
    CHECK("1 param",                    fn.params.size() == 1);
    CHECK("param type is ptr",          fn.params[0].type.kind == TypeKind::PTR);
    CHECK("ptr inner is PATH",          fn.params[0].type.inner != nullptr &&
                                        fn.params[0].type.inner->kind == TypeKind::PATH);

    const Block& bb0 = fn.blocks[0];
    CHECK("bb0 has 2 stmts",            bb0.stmts.size() == 2);

    // stmt 0: member_ptr
    CHECK("stmt0 is MEMBER_PTR",        bb0.stmts[0].rhs.kind == RhsKind::MEMBER_PTR);
    CHECK("member_ptr op1 is p",        bb0.stmts[0].rhs.op1 == "p");
    CHECK("member_ptr field is x",      bb0.stmts[0].rhs.field == "x");

    // stmt 1: load
    CHECK("stmt1 is LOAD",              bb0.stmts[1].rhs.kind == RhsKind::LOAD);
    CHECK("load op1 is fp",             bb0.stmts[1].rhs.op1 == "fp");

    // terminator
    CHECK("return %v",                  bb0.term.has_ret_val && bb0.term.ret_local == "v");
}

// ---------------------------------------------------------------------------
// Test: call opcode (sum_abs_diffs from spec)
// ---------------------------------------------------------------------------
static void test_call() {
    std::cout << "\n[Call opcode — sum_abs_diffs]\n";

    const std::string src = R"(
function Client::sum_abs_diffs(%a: i32, %b: i32, %c: i32, %d: i32) -> i32 {
  locals {
    %dx : i32;
    %dy : i32;
    %r  : i32;
  }
  entry bb0;
  bb0:
    %dx = call Math::abs_diff(%a, %b);
    %dy = call Math::abs_diff(%c, %d);
    %r  = bin add %dx, %dy;
    return %r;
}
)";
    Program prog = must_parse(src);
    const Block& bb0 = prog.function.blocks[0];

    CHECK("3 statements",               bb0.stmts.size() == 3);

    // stmt 0: %dx = call Math::abs_diff(%a, %b)
    CHECK("stmt0 is CALL",              bb0.stmts[0].rhs.kind == RhsKind::CALL);
    CHECK("call path",                  bb0.stmts[0].rhs.call_path == "Math::abs_diff");
    CHECK("call has 2 args",            bb0.stmts[0].rhs.call_args.size() == 2);
    CHECK("call arg0 is a",             bb0.stmts[0].rhs.call_args[0] == "a");
    CHECK("call arg1 is b",             bb0.stmts[0].rhs.call_args[1] == "b");

    // stmt 2: %r = bin add %dx, %dy
    CHECK("stmt2 is BIN add",           bb0.stmts[2].rhs.kind == RhsKind::BIN &&
                                        bb0.stmts[2].rhs.bin_op == BinOp::ADD);
}

// ---------------------------------------------------------------------------
// Test: jump (unconditional)
// ---------------------------------------------------------------------------
static void test_jump() {
    std::cout << "\n[Unconditional jump]\n";

    const std::string src = R"(
function Test::loop() -> void {
  locals {}
  entry bb0;
  bb0:
    jump bb0;
}
)";
    Program prog = must_parse(src);
    const Block& bb0 = prog.function.blocks[0];
    CHECK("terminator is JUMP",         bb0.term.kind == TermKind::JUMP);
    CHECK("jump target is bb0",         bb0.term.label1 == "bb0");
}

// ---------------------------------------------------------------------------
// Test: ptr types
// ---------------------------------------------------------------------------
static void test_ptr_type() {
    std::cout << "\n[Pointer and nested pointer types]\n";

    const std::string src = R"(
function Test::ptr(%p: ptr<i32>, %pp: ptr<ptr<bool>>) -> void {
  locals {}
  entry bb0;
  bb0:
    return;
}
)";
    Program prog = must_parse(src);
    const Function& fn = prog.function;

    CHECK("param0 is ptr<i32>",
        fn.params[0].type.kind == TypeKind::PTR &&
        fn.params[0].type.inner->kind == TypeKind::I32);

    CHECK("param1 is ptr<ptr<bool>>",
        fn.params[1].type.kind == TypeKind::PTR &&
        fn.params[1].type.inner->kind == TypeKind::PTR &&
        fn.params[1].type.inner->inner->kind == TypeKind::BOOL);
}

// ---------------------------------------------------------------------------
// Test: float literal and cast
// ---------------------------------------------------------------------------
static void test_float_and_cast() {
    std::cout << "\n[Float literal and cast]\n";

    const std::string src = R"(
function Test::f(%x: i32) -> f64 {
  locals {
    %y : f64;
  }
  entry bb0;
  bb0:
    %y = cast %x to f64;
    return %y;
}
)";
    Program prog = must_parse(src);
    const Block& bb0 = prog.function.blocks[0];
    CHECK("stmt0 is CAST",              bb0.stmts[0].rhs.kind == RhsKind::CAST);
    CHECK("cast source is x",           bb0.stmts[0].rhs.op1 == "x");
    CHECK("cast target is f64",         bb0.stmts[0].rhs.cast_type.kind == TypeKind::F64);
}

// ---------------------------------------------------------------------------
// Test: error recovery — bad syntax should produce a clear message
// ---------------------------------------------------------------------------
static void test_syntax_errors() {
    std::cout << "\n[Syntax error messages]\n";

    // Missing `->` return type arrow
    {
        const std::string src = "function Foo() i32 { locals {} entry bb0; bb0: return; }";
        Program prog; std::string err;
        bool ok = do_parse(src, prog, err);
        CHECK("missing arrow reports error", !ok);
        // The error should mention `->` or the bad token
        CHECK("error mentions position",     err.find("Syntax error at") != std::string::npos);
    }

    // Missing semicolon after local declaration
    {
        const std::string src = R"(
function Foo() -> void {
  locals { %x : i32 }
  entry bb0;
  bb0: return;
}
)";
        Program prog; std::string err;
        bool ok = do_parse(src, prog, err);
        CHECK("missing semicolon reports error", !ok);
    }

    // Wrong token after `bin` (not an operator)
    {
        const std::string src = R"(
function Foo() -> void {
  locals { %x : i32; }
  entry bb0;
  bb0:
    %x = bin foo %x, %x;
    return;
}
)";
        Program prog; std::string err;
        bool ok = do_parse(src, prog, err);
        CHECK("bad binop keyword reports error", !ok);
    }
}

// ---------------------------------------------------------------------------
// main
// ---------------------------------------------------------------------------
int main() {
    std::cout << "==============================\n";
    std::cout << " ResIR Parser — Unit Tests\n";
    std::cout << "==============================\n";

    test_minimal_function();
    test_abs_diff();
    test_struct_access();
    test_call();
    test_jump();
    test_ptr_type();
    test_float_and_cast();
    test_syntax_errors();

    std::cout << "\n==============================\n";
    std::cout << " Results: " << passed << " passed, " << failed << " failed\n";
    std::cout << "==============================\n";

    return (failed == 0) ? 0 : 1;
}
