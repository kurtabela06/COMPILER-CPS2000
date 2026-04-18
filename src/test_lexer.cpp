// =============================================================================
// test_lexer.cpp  —  Unit tests for the ResIR table-driven lexer
// =============================================================================
//
// How to run:
//   make test
//   ./resir_test
//
// Each test() call checks that a given snippet produces the expected sequence
// of TokenKinds.  The helper `kinds()` strips position info so tests stay
// short and readable.
// =============================================================================

#include "lexer.hpp"
#include "token.hpp"

#include <iostream>
#include <vector>
#include <string>
#include <cassert>   // assert() — aborts the program with a message if false

// ---------------------------------------------------------------------------
// Test infrastructure
// ---------------------------------------------------------------------------

// Pass / fail counters
static int passed = 0;
static int failed = 0;

// kinds() — run the lexer on `src` and return just the token kind sequence.
// Asserts that lexing succeeds (no error).
static std::vector<TokenKind> kinds(const std::string& src) {
    Lexer lex(src);
    std::vector<Token> tokens;
    bool ok = lex.tokenise(tokens);
    if (!ok) {
        std::cerr << "[ERROR in test helper] " << lex.get_error() << "\n";
        assert(false && "tokenise() failed unexpectedly");
    }
    std::vector<TokenKind> ks;
    for (auto& t : tokens) ks.push_back(t.kind);
    return ks;
}

// text_of() — run the lexer and return a vector of (kind, text) pairs.
static std::vector<std::pair<TokenKind,std::string>> text_of(const std::string& src) {
    Lexer lex(src);
    std::vector<Token> tokens;
    bool ok = lex.tokenise(tokens);
    if (!ok) {
        std::cerr << "[ERROR in test helper] " << lex.get_error() << "\n";
        assert(false && "tokenise() failed unexpectedly");
    }
    std::vector<std::pair<TokenKind,std::string>> result;
    for (auto& t : tokens) result.push_back({t.kind, t.text});
    return result;
}

// should_error() — expect the lexer to fail on `src`.
static bool should_error(const std::string& src) {
    Lexer lex(src);
    std::vector<Token> tokens;
    return !lex.tokenise(tokens);
}

// CHECK macro — prints PASS/FAIL and updates counters.
#define CHECK(name, condition) do { \
    if (condition) { \
        std::cout << "  PASS  " << (name) << "\n"; \
        ++passed; \
    } else { \
        std::cout << "  FAIL  " << (name) << "\n"; \
        ++failed; \
    } \
} while(0)

using K = TokenKind;

// ---------------------------------------------------------------------------
// Tests
// ---------------------------------------------------------------------------

static void test_keywords() {
    std::cout << "\n[Keywords]\n";

    auto ks = kinds("function locals entry");
    CHECK("function/locals/entry",
        ks == std::vector<K>({K::KW_FUNCTION, K::KW_LOCALS, K::KW_ENTRY, K::END_OF_FILE}));

    auto ks2 = kinds("const cast un bin addr_of member_ptr load store call jump cjump return void ptr to extern type");
    std::vector<K> expected2 = {
        K::KW_CONST, K::KW_CAST, K::KW_UN, K::KW_BIN,
        K::KW_ADDR_OF, K::KW_MEMBER_PTR, K::KW_LOAD, K::KW_STORE,
        K::KW_CALL, K::KW_JUMP, K::KW_CJUMP, K::KW_RETURN,
        K::KW_VOID, K::KW_PTR, K::KW_TO, K::KW_EXTERN, K::KW_TYPE,
        K::END_OF_FILE
    };
    CHECK("all statement keywords", ks2 == expected2);
}

static void test_primitive_types() {
    std::cout << "\n[Primitive types]\n";
    auto ks = kinds("bool i32 i64 u32 f64");
    CHECK("bool i32 i64 u32 f64",
        ks == std::vector<K>({K::KW_BOOL, K::KW_I32, K::KW_I64, K::KW_U32, K::KW_F64, K::END_OF_FILE}));
}

static void test_binops_unops() {
    std::cout << "\n[Operators]\n";
    auto ks = kinds("neg not add sub mul div mod eq ne lt le gt ge and or");
    std::vector<K> exp = {
        K::KW_NEG, K::KW_NOT, K::KW_ADD, K::KW_SUB, K::KW_MUL,
        K::KW_DIV, K::KW_MOD, K::KW_EQ, K::KW_NE, K::KW_LT,
        K::KW_LE, K::KW_GT_OP, K::KW_GE, K::KW_AND, K::KW_OR,
        K::END_OF_FILE
    };
    CHECK("all operators", ks == exp);
}

static void test_boolean_null_literals() {
    std::cout << "\n[Bool/null literals]\n";
    auto ks = kinds("true false null");
    CHECK("true false null",
        ks == std::vector<K>({K::KW_TRUE, K::KW_FALSE, K::KW_NULL, K::END_OF_FILE}));
}

static void test_identifiers() {
    std::cout << "\n[Identifiers]\n";
    auto tv = text_of("myVar _tmp foo123");
    CHECK("plain ident text",
        tv[0] == std::make_pair(K::IDENT, std::string("myVar")) &&
        tv[1] == std::make_pair(K::IDENT, std::string("_tmp"))  &&
        tv[2] == std::make_pair(K::IDENT, std::string("foo123")));
}

static void test_locals() {
    std::cout << "\n[Locals]\n";
    auto tv = text_of("%dx %is_neg %r");
    CHECK("local tokens carry %name",
        tv[0].first == K::LOCAL && tv[0].second == "%dx"     &&
        tv[1].first == K::LOCAL && tv[1].second == "%is_neg" &&
        tv[2].first == K::LOCAL && tv[2].second == "%r");
}

static void test_labels() {
    std::cout << "\n[Labels]\n";
    auto tv = text_of("bb0 bb12 bb99");
    CHECK("labels",
        tv[0].first == K::LABEL && tv[0].second == "bb0"  &&
        tv[1].first == K::LABEL && tv[1].second == "bb12" &&
        tv[2].first == K::LABEL && tv[2].second == "bb99");
}

static void test_integer_literals() {
    std::cout << "\n[Integer literals]\n";
    Lexer lex("0 42 1000");
    std::vector<Token> toks;
    lex.tokenise(toks);
    CHECK("integer values",
        toks[0].kind == K::INT_LIT && toks[0].int_val == 0    &&
        toks[1].kind == K::INT_LIT && toks[1].int_val == 42   &&
        toks[2].kind == K::INT_LIT && toks[2].int_val == 1000);
}

static void test_float_literals() {
    std::cout << "\n[Float literals]\n";
    Lexer lex("3.14 0.5");
    std::vector<Token> toks;
    lex.tokenise(toks);
    CHECK("float values",
        toks[0].kind == K::FLOAT_LIT && toks[0].float_val == 3.14 &&
        toks[1].kind == K::FLOAT_LIT && toks[1].float_val == 0.5);
}

static void test_punctuation() {
    std::cout << "\n[Punctuation]\n";
    auto ks = kinds("{ } ( ) : ; , -> < > = ::");
    std::vector<K> exp = {
        K::LBRACE, K::RBRACE, K::LPAREN, K::RPAREN,
        K::COLON, K::SEMICOLON, K::COMMA,
        K::ARROW, K::LT, K::GT, K::EQ, K::COLON_COLON,
        K::END_OF_FILE
    };
    CHECK("all punctuation", ks == exp);
}

static void test_line_comment() {
    std::cout << "\n[Comments]\n";
    auto ks = kinds("i32 // this is a comment\ni64");
    CHECK("line comment skipped",
        ks == std::vector<K>({K::KW_I32, K::KW_I64, K::END_OF_FILE}));
}

static void test_position_tracking() {
    std::cout << "\n[Position tracking]\n";
    Lexer lex("i32\n  %x");
    std::vector<Token> toks;
    lex.tokenise(toks);
    CHECK("i32 on line 1 col 1", toks[0].line == 1 && toks[0].col == 1);
    CHECK("%x on line 2 col 3",  toks[1].line == 2 && toks[1].col == 3);
}

static void test_errors() {
    std::cout << "\n[Error handling]\n";
    CHECK("lone `-` is an error",          should_error("- "));
    CHECK("`%123` invalid local",          should_error("%123"));
    CHECK("lone `@` is an error",          should_error("@foo"));
    CHECK("lone `/` is an error",          should_error("/"));
}

static void test_abs_diff_snippet() {
    std::cout << "\n[Full snippet — abs_diff from spec]\n";
    const std::string src = R"(
function Math::abs_diff(%a: i32, %b: i32) -> i32 {
  locals {
    %zero : i32;
    %d    : i32;
    %is_neg : bool;
    %r    : i32;
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
    Lexer lex(src);
    std::vector<Token> toks;
    bool ok = lex.tokenise(toks);
    CHECK("abs_diff tokenises without error", ok);
    CHECK("first token is `function`", ok && toks[0].kind == K::KW_FUNCTION);
    CHECK("path Math::abs_diff present",
        ok && toks[1].kind == K::IDENT && toks[1].text == "Math" &&
        toks[2].kind == K::COLON_COLON &&
        toks[3].kind == K::IDENT && toks[3].text == "abs_diff");
}

static void test_struct_snippet() {
    std::cout << "\n[Full snippet — struct access from spec]\n";
    const std::string src = R"(
extern type Custom::Struct::Point {
  x : i32;
  y : i32;
}
function Geom::point_x(%p: ptr<Custom::Struct::Point>) -> i32 {
  locals { %fp : ptr<i32>; %v : i32; }
  entry bb0;
  bb0:
    %fp = member_ptr %p, x;
    %v  = load %fp;
    return %v;
}
)";
    Lexer lex(src);
    std::vector<Token> toks;
    bool ok = lex.tokenise(toks);
    CHECK("struct_access tokenises without error", ok);
    CHECK("starts with `extern`", ok && toks[0].kind == K::KW_EXTERN);
}

// ---------------------------------------------------------------------------
// main
// ---------------------------------------------------------------------------
int main() {
    std::cout << "==============================\n";
    std::cout << " ResIR Lexer — Unit Tests\n";
    std::cout << "==============================\n";

    test_keywords();
    test_primitive_types();
    test_binops_unops();
    test_boolean_null_literals();
    test_identifiers();
    test_locals();
    test_labels();
    test_integer_literals();
    test_float_literals();
    test_punctuation();
    test_line_comment();
    test_position_tracking();
    test_errors();
    test_abs_diff_snippet();
    test_struct_snippet();

    std::cout << "\n==============================\n";
    std::cout << " Results: " << passed << " passed, " << failed << " failed\n";
    std::cout << "==============================\n";

    return (failed == 0) ? 0 : 1;
}
