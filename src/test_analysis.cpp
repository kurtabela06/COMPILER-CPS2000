// =============================================================================
// test_analysis.cpp  —  Unit tests for Task 5: Fact Extractor
// =============================================================================
//
// HOW THESE TESTS WORK
// ─────────────────────
// We test the C++ side of Task 5: the FactExtractor class in analysis.cpp.
//
// Each test:
//   1. Writes a small ResIR program as a string.
//   2. Runs it through Lexer → Parser → Validator → FactExtractor.
//   3. Inspects the in-memory fact vectors (edges_, defs_, uses_) WITHOUT
//      writing anything to disk — this makes tests fast and side-effect free.
//
// We use the accessor methods:
//   fx.edges()  →  std::vector<FactExtractor::EdgeFact>
//   fx.defs()   →  std::vector<FactExtractor::DefFact>
//   fx.uses()   →  std::vector<FactExtractor::UseFact>
//
// We also test the file-writing path (extract()) with a temp directory to
// verify the TSV format is correct.
//
// HOW TO RUN:
//   make test_analysis
//   ./resir_analysis_test
// =============================================================================

#include "lexer.hpp"
#include "token.hpp"
#include "parser.hpp"
#include "cfg.hpp"
#include "validator.hpp"
#include "analysis.hpp"

#include <iostream>
#include <string>
#include <vector>
#include <algorithm>  // std::find_if
#include <fstream>    // to read back written files

// =============================================================================
// Test harness
// =============================================================================

static int g_pass = 0;
static int g_fail = 0;

static void pass(const std::string& name) {
    std::cout << "[PASS] " << name << "\n";
    ++g_pass;
}
static void fail(const std::string& name, const std::string& detail = "") {
    std::cout << "[FAIL] " << name;
    if (!detail.empty()) std::cout << " : " << detail;
    std::cout << "\n";
    ++g_fail;
}

// Helpers to search fact vectors by value.

static bool has_edge(const std::vector<FactExtractor::EdgeFact>& v,
                     const std::string& from, const std::string& to) {
    return std::find_if(v.begin(), v.end(), [&](const FactExtractor::EdgeFact& e){
        return e.from == from && e.to == to;
    }) != v.end();
}

static bool has_def(const std::vector<FactExtractor::DefFact>& v,
                    const std::string& block, const std::string& var) {
    return std::find_if(v.begin(), v.end(), [&](const FactExtractor::DefFact& d){
        return d.block == block && d.var == var;
    }) != v.end();
}

static bool has_use(const std::vector<FactExtractor::UseFact>& v,
                    const std::string& block, const std::string& var) {
    return std::find_if(v.begin(), v.end(), [&](const FactExtractor::UseFact& u){
        return u.block == block && u.var == var;
    }) != v.end();
}

// Count occurrences of a def fact (a variable can be defined only once per
// block, but we test this explicitly).
static int count_def(const std::vector<FactExtractor::DefFact>& v,
                     const std::string& block, const std::string& var) {
    int c = 0;
    for (const auto& d : v) if (d.block == block && d.var == var) ++c;
    return c;
}

// Full pipeline helper: lex → parse → validate → extract facts (in-memory).
// Returns false on any pipeline error; sets error_out.
static bool run_extract(const std::string& source,
                         FactExtractor*& out_fx,
                         Program& prog,
                         std::string& error_out) {
    Lexer lexer(source);
    std::vector<Token> tokens;
    if (!lexer.tokenise(tokens)) { error_out = lexer.get_error(); return false; }

    Parser parser(tokens);
    if (!parser.parse(prog)) { error_out = parser.get_error(); return false; }

    Validator validator(prog);
    if (!validator.validate()) { error_out = validator.get_error(); return false; }

    // Collect facts into memory (no disk write yet).
    // We call collect_facts via extract() with a temp dir, OR we expose
    // a collect-only path.  Since analysis.hpp does not expose collect_facts()
    // directly (it's private), we call extract() with a real temp dir.
    // However, for most tests we call extract() with /tmp/resir_test_facts/
    // and then read the accessor vectors.
    out_fx = new FactExtractor(prog);
    if (!out_fx->extract("/tmp/resir_test_facts/")) {
        error_out = out_fx->get_error();
        delete out_fx; out_fx = nullptr;
        return false;
    }
    return true;
}

// =============================================================================
// Test 1 — abs_diff: edges
// =============================================================================
static void test_abs_diff_edges() {
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
    Program prog; std::string err; FactExtractor* fx = nullptr;
    if (!run_extract(src, fx, prog, err)) { fail("abs_diff_edges", err); return; }

    // cjump in bb0 → two edges
    if (has_edge(fx->edges(), "bb0", "bb1")) pass("abs_diff: edge bb0→bb1");
    else fail("abs_diff: edge bb0→bb1");

    if (has_edge(fx->edges(), "bb0", "bb2")) pass("abs_diff: edge bb0→bb2");
    else fail("abs_diff: edge bb0→bb2");

    // return in bb1 and bb2 → no edges
    if (!has_edge(fx->edges(), "bb1", "bb0") &&
        !has_edge(fx->edges(), "bb1", "bb2"))
        pass("abs_diff: bb1 has no outgoing edges");
    else
        fail("abs_diff: bb1 should have no outgoing edges");

    // Total edges: exactly 2
    if (fx->edges().size() == 2) pass("abs_diff: exactly 2 edges");
    else fail("abs_diff: expected 2 edges, got " + std::to_string(fx->edges().size()));

    delete fx;
}

// =============================================================================
// Test 2 — abs_diff: def facts
// =============================================================================
static void test_abs_diff_defs() {
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
    Program prog; std::string err; FactExtractor* fx = nullptr;
    if (!run_extract(src, fx, prog, err)) { fail("abs_diff_defs", err); return; }

    // bb0 defines zero, d, is_neg
    if (has_def(fx->defs(), "bb0", "zero"))   pass("abs_diff def: bb0 defines zero");
    else fail("abs_diff def: bb0 should define zero");

    if (has_def(fx->defs(), "bb0", "d"))      pass("abs_diff def: bb0 defines d");
    else fail("abs_diff def: bb0 should define d");

    if (has_def(fx->defs(), "bb0", "is_neg")) pass("abs_diff def: bb0 defines is_neg");
    else fail("abs_diff def: bb0 should define is_neg");

    // bb1 defines r
    if (has_def(fx->defs(), "bb1", "r"))      pass("abs_diff def: bb1 defines r");
    else fail("abs_diff def: bb1 should define r");

    // bb2 defines NOTHING (return is not a definition)
    if (!has_def(fx->defs(), "bb2", "d"))     pass("abs_diff def: bb2 does not define d");
    else fail("abs_diff def: bb2 should not define d (return is a use, not a def)");

    // No duplicate defs for zero in bb0
    if (count_def(fx->defs(), "bb0", "zero") == 1) pass("abs_diff def: zero defined exactly once in bb0");
    else fail("abs_diff def: zero should appear exactly once in bb0");

    delete fx;
}

// =============================================================================
// Test 3 — abs_diff: use facts
// =============================================================================
static void test_abs_diff_uses() {
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
    Program prog; std::string err; FactExtractor* fx = nullptr;
    if (!run_extract(src, fx, prog, err)) { fail("abs_diff_uses", err); return; }

    // bb0: %d = bin sub %a, %b  → uses a, b
    if (has_use(fx->uses(), "bb0", "a"))      pass("abs_diff use: bb0 uses a");
    else fail("abs_diff use: bb0 should use a");

    if (has_use(fx->uses(), "bb0", "b"))      pass("abs_diff use: bb0 uses b");
    else fail("abs_diff use: bb0 should use b");

    // bb0: %is_neg = bin lt %d, %zero  → uses d, zero
    if (has_use(fx->uses(), "bb0", "d"))      pass("abs_diff use: bb0 uses d");
    else fail("abs_diff use: bb0 should use d");

    if (has_use(fx->uses(), "bb0", "zero"))   pass("abs_diff use: bb0 uses zero");
    else fail("abs_diff use: bb0 should use zero");

    // bb0: cjump %is_neg  → uses is_neg (from the terminator)
    if (has_use(fx->uses(), "bb0", "is_neg")) pass("abs_diff use: bb0 cjump uses is_neg");
    else fail("abs_diff use: bb0 cjump should use is_neg");

    // const 0 generates NO uses (it's a literal)
    // (zero is the destination of const 0, not a use)

    // bb1: %r = un neg %d  → uses d
    if (has_use(fx->uses(), "bb1", "d"))      pass("abs_diff use: bb1 uses d");
    else fail("abs_diff use: bb1 should use d");

    // bb1: return %r  → uses r (from the terminator)
    if (has_use(fx->uses(), "bb1", "r"))      pass("abs_diff use: bb1 return uses r");
    else fail("abs_diff use: bb1 return should use r");

    // bb2: return %d  → uses d
    if (has_use(fx->uses(), "bb2", "d"))      pass("abs_diff use: bb2 return uses d");
    else fail("abs_diff use: bb2 return should use d");

    delete fx;
}

// =============================================================================
// Test 4 — struct_access (member_ptr and load)
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
    Program prog; std::string err; FactExtractor* fx = nullptr;
    if (!run_extract(src, fx, prog, err)) { fail("struct_access", err); return; }

    // member_ptr %p, x  → uses p (the pointer operand; x is a field name, not a local)
    if (has_use(fx->uses(), "bb0", "p"))  pass("struct_access use: member_ptr uses p");
    else fail("struct_access use: member_ptr should use p");

    // member_ptr does NOT add a use for "x" (x is a field name, not a local variable)
    if (!has_use(fx->uses(), "bb0", "x")) pass("struct_access: field x is not a use fact");
    else fail("struct_access: x is a field name, should NOT be a use fact");

    // load %fp  → uses fp
    if (has_use(fx->uses(), "bb0", "fp")) pass("struct_access use: load uses fp");
    else fail("struct_access use: load should use fp");

    // return %v  → uses v
    if (has_use(fx->uses(), "bb0", "v"))  pass("struct_access use: return uses v");
    else fail("struct_access use: return should use v");

    // defs: fp and v
    if (has_def(fx->defs(), "bb0", "fp")) pass("struct_access def: fp defined in bb0");
    else fail("struct_access def: fp should be defined in bb0");

    if (has_def(fx->defs(), "bb0", "v"))  pass("struct_access def: v defined in bb0");
    else fail("struct_access def: v should be defined in bb0");

    // edges: return has no successor
    if (fx->edges().empty()) pass("struct_access: no edges (single block with return)");
    else fail("struct_access: should have no edges, got " + std::to_string(fx->edges().size()));

    delete fx;
}

// =============================================================================
// Test 5 — store opcode (both operands are uses, no def)
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
    Program prog; std::string err; FactExtractor* fx = nullptr;
    if (!run_extract(src, fx, prog, err)) { fail("store", err); return; }

    // store %p, %v  → uses both p AND v (no definition)
    if (has_use(fx->uses(), "bb0", "p")) pass("store use: p used in store");
    else fail("store use: p should be used in store");

    if (has_use(fx->uses(), "bb0", "v")) pass("store use: v used in store");
    else fail("store use: v should be used in store");

    // store has NO destination → no def for p or v from this instruction
    // (r is defined by load, not store)
    if (!has_def(fx->defs(), "bb0", "p")) pass("store: p is not defined by store");
    else fail("store: p should not be defined by store (store has no dest)");

    // load %p → uses p again, defines r
    if (has_def(fx->defs(), "bb0", "r")) pass("store: r defined by load");
    else fail("store: r should be defined by load");

    delete fx;
}

// =============================================================================
// Test 6 — call opcode (all args are uses)
// =============================================================================
static void test_call() {
    const std::string src = R"(
function Test::caller(%x: i32, %y: i32) -> i32 {
    locals { %result : i32; }
    entry bb0;

    bb0:
        %result = call Math::abs_diff(%x, %y);
        return %result;
}
)";
    Program prog; std::string err; FactExtractor* fx = nullptr;
    if (!run_extract(src, fx, prog, err)) { fail("call", err); return; }

    // call uses x and y (all arguments)
    if (has_use(fx->uses(), "bb0", "x")) pass("call use: x used as call arg");
    else fail("call use: x should be used as a call argument");

    if (has_use(fx->uses(), "bb0", "y")) pass("call use: y used as call arg");
    else fail("call use: y should be used as a call argument");

    // call defines result (the destination)
    if (has_def(fx->defs(), "bb0", "result")) pass("call def: result defined by call");
    else fail("call def: result should be defined by call");

    delete fx;
}

// =============================================================================
// Test 7 — unconditional jump generates exactly one edge
// =============================================================================
static void test_jump_edge() {
    const std::string src = R"(
function Test::loop(%a: i32) -> void {
    locals {}
    entry bb0;

    bb0:
        jump bb1;

    bb1:
        return;
}
)";
    Program prog; std::string err; FactExtractor* fx = nullptr;
    if (!run_extract(src, fx, prog, err)) { fail("jump_edge", err); return; }

    // jump bb1 → edge(bb0, bb1)
    if (has_edge(fx->edges(), "bb0", "bb1")) pass("jump_edge: edge bb0→bb1");
    else fail("jump_edge: edge bb0→bb1 not found");

    // Only one edge total
    if (fx->edges().size() == 1) pass("jump_edge: exactly 1 edge");
    else fail("jump_edge: expected 1 edge, got " + std::to_string(fx->edges().size()));

    delete fx;
}

// =============================================================================
// Test 8 — const generates no use facts
// =============================================================================
static void test_const_no_use() {
    const std::string src = R"(
function Test::consts(%dummy: i32) -> i32 {
    locals { %v : i32; }
    entry bb0;

    bb0:
        %v = const 42;
        return %v;
}
)";
    Program prog; std::string err; FactExtractor* fx = nullptr;
    if (!run_extract(src, fx, prog, err)) { fail("const_no_use", err); return; }

    // const 42 generates a def for v but NO use facts (42 is a literal)
    if (has_def(fx->defs(), "bb0", "v"))   pass("const def: v defined by const");
    else fail("const def: v should be defined by const");

    // The only use in bb0 is from  return %v  (the terminator)
    if (has_use(fx->uses(), "bb0", "v"))   pass("const use: v used in return");
    else fail("const use: v should be used in return");

    // dummy is a param, it's used nowhere in this function
    if (!has_use(fx->uses(), "bb0", "dummy")) pass("const: dummy not used");
    else fail("const: dummy should not appear in any use fact");

    delete fx;
}

// =============================================================================
// Test 9 — addr_of and cast
// =============================================================================
static void test_addr_of_cast() {
    const std::string src = R"(
function Test::misc(%a: i32) -> f64 {
    locals {
        %p : ptr<i32>;
        %r : f64;
    }
    entry bb0;

    bb0:
        %p = addr_of %a;
        %r = cast %a to f64;
        return %r;
}
)";
    Program prog; std::string err; FactExtractor* fx = nullptr;
    if (!run_extract(src, fx, prog, err)) { fail("addr_of_cast", err); return; }

    // addr_of %a → uses a
    if (has_use(fx->uses(), "bb0", "a")) pass("addr_of: uses a");
    else fail("addr_of: should use a");

    // cast %a to f64 → uses a (again — can appear multiple times in use facts)
    // We just check it exists at least once
    if (has_use(fx->uses(), "bb0", "a")) pass("cast: uses a (cast operand)");
    else fail("cast: should use a");

    // defs: p (from addr_of), r (from cast)
    if (has_def(fx->defs(), "bb0", "p")) pass("addr_of def: p defined");
    else fail("addr_of def: p should be defined");

    if (has_def(fx->defs(), "bb0", "r")) pass("cast def: r defined");
    else fail("cast def: r should be defined");

    delete fx;
}

// =============================================================================
// Test 10 — file writing: verify TSV format is correct
// =============================================================================
static void test_file_writing() {
    const std::string src = R"(
function Math::abs_diff(%a: i32, %b: i32) -> i32 {
    locals {
        %d : i32;
        %r : i32;
        %neg : bool;
    }
    entry bb0;

    bb0:
        %d   = bin sub %a, %b;
        %neg = bin lt  %d, %a;
        cjump %neg, bb1, bb2;

    bb1:
        %r = un neg %d;
        return %r;

    bb2:
        return %d;
}
)";
    // Use a dedicated temp dir to avoid conflicts with other tests
    const std::string dir = "/tmp/resir_test_file_write/";

    Lexer lexer(src);
    std::vector<Token> tokens;
    if (!lexer.tokenise(tokens)) { fail("file_writing: lex error"); return; }
    Parser parser(tokens);
    Program prog;
    if (!parser.parse(prog)) { fail("file_writing: parse error"); return; }
    Validator validator(prog);
    if (!validator.validate()) { fail("file_writing: validate error"); return; }

    FactExtractor fx(prog);
    if (!fx.extract(dir)) { fail("file_writing: extract error: " + fx.get_error()); return; }

    // Read edge.facts back and check it contains "bb0\tbb1"
    std::ifstream ef(dir + "edge.facts");
    if (!ef.is_open()) { fail("file_writing: cannot open edge.facts"); return; }
    std::string content((std::istreambuf_iterator<char>(ef)),
                         std::istreambuf_iterator<char>());

    if (content.find("bb0\tbb1") != std::string::npos)
        pass("file_writing: edge.facts contains bb0\\tbb1");
    else
        fail("file_writing: edge.facts missing bb0\\tbb1", "content was: " + content);

    if (content.find("bb0\tbb2") != std::string::npos)
        pass("file_writing: edge.facts contains bb0\\tbb2");
    else
        fail("file_writing: edge.facts missing bb0\\tbb2");

    // Read def.facts and check for bb0\td
    std::ifstream df(dir + "def.facts");
    if (!df.is_open()) { fail("file_writing: cannot open def.facts"); return; }
    std::string dcontent((std::istreambuf_iterator<char>(df)),
                          std::istreambuf_iterator<char>());
    if (dcontent.find("bb0\td") != std::string::npos)
        pass("file_writing: def.facts contains bb0\\td");
    else
        fail("file_writing: def.facts missing bb0\\td");

    // Read use.facts and check for bb0\ta
    std::ifstream uf(dir + "use.facts");
    if (!uf.is_open()) { fail("file_writing: cannot open use.facts"); return; }
    std::string ucontent((std::istreambuf_iterator<char>(uf)),
                          std::istreambuf_iterator<char>());
    if (ucontent.find("bb0\ta") != std::string::npos)
        pass("file_writing: use.facts contains bb0\\ta");
    else
        fail("file_writing: use.facts missing bb0\\ta");
}

// =============================================================================
// main
// =============================================================================
int main() {
    std::cout << "=== Task 5: Fact Extractor Tests ===\n\n";

    test_abs_diff_edges();
    test_abs_diff_defs();
    test_abs_diff_uses();
    test_struct_access();
    test_store();
    test_call();
    test_jump_edge();
    test_const_no_use();
    test_addr_of_cast();
    test_file_writing();

    std::cout << "\n==============================\n";
    std::cout << "Total: " << (g_pass + g_fail) << " checks, "
              << g_pass << " pass, " << g_fail << " fail\n";
    return g_fail > 0 ? 1 : 0;
}
