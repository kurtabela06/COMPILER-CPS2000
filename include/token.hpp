// =============================================================================
// token.hpp  —  Token type definitions for the ResIR lexer
// =============================================================================
//
// What is a token?
//   After a lexer reads raw source text it groups characters into "tokens"
//   — the smallest meaningful units of the language.  For example:
//     "function"  →  Token{ kind=KW_FUNCTION }
//     "%x"        →  Token{ kind=LOCAL, text="x" }
//     "42"        →  Token{ kind=INT_LIT, int_val=42 }
//
// Design:
//   We use a plain C-style enum for `TokenKind` (the "what type is it?")
//   and a simple struct for `Token` (the full token with text and position).
//   This keeps the code straightforward and easy to read.
// =============================================================================

#pragma once              // Only include this file once per compilation unit
#include <string>         // std::string — for storing token text
#include <cstdint>        // int64_t, etc.

// ---------------------------------------------------------------------------
// TokenKind — every possible kind of token the lexer can produce
// ---------------------------------------------------------------------------
// Each entry in this enum corresponds to one type of "word" in ResIR.
// The parser will switch on these values to decide what to do next.
enum class TokenKind {

    // --- Keywords (reserved words the grammar defines) ----------------------
    KW_FUNCTION,    // `function`
    KW_LOCALS,      // `locals`
    KW_ENTRY,       // `entry`
    KW_CONST,       // `const`
    KW_CAST,        // `cast`
    KW_UN,          // `un`
    KW_BIN,         // `bin`
    KW_ADDR_OF,     // `addr_of`
    KW_MEMBER_PTR,  // `member_ptr`
    KW_LOAD,        // `load`
    KW_STORE,       // `store`
    KW_CALL,        // `call`
    KW_JUMP,        // `jump`
    KW_CJUMP,       // `cjump`
    KW_RETURN,      // `return`
    KW_VOID,        // `void`
    KW_PTR,         // `ptr`
    KW_TO,          // `to`
    KW_EXTERN,      // `extern`
    KW_TYPE,        // `type`
    KW_NEG,         // `neg`   (UnOp)
    KW_NOT,         // `not`   (UnOp)
    KW_ADD,         // `add`   (BinOp)
    KW_SUB,         // `sub`   (BinOp)
    KW_MUL,         // `mul`   (BinOp)
    KW_DIV,         // `div`   (BinOp)
    KW_MOD,         // `mod`   (BinOp)
    KW_EQ,          // `eq`    (BinOp)
    KW_NE,          // `ne`    (BinOp)
    KW_LT,          // `lt`    (BinOp)
    KW_LE,          // `le`    (BinOp)
    KW_GT_OP,       // `gt`    (BinOp — named GT_OP to avoid clash with GT punct)
    KW_GE,          // `ge`    (BinOp)
    KW_AND,         // `and`   (BinOp)
    KW_OR,          // `or`    (BinOp)

    // --- Primitive type keywords --------------------------------------------
    KW_BOOL,        // `bool`
    KW_I32,         // `i32`
    KW_I64,         // `i64`
    KW_U32,         // `u32`
    KW_F64,         // `f64`

    // --- Literal keywords ---------------------------------------------------
    KW_TRUE,        // `true`
    KW_FALSE,       // `false`
    KW_NULL,        // `null`

    // --- Identifiers --------------------------------------------------------
    // A plain identifier that is NOT a keyword: [A-Za-z_][A-Za-z0-9_]*
    IDENT,

    // --- Locals: %ident -----------------------------------------------------
    // The `%` prefix has been consumed; `text` holds just the ident part.
    LOCAL,

    // --- Labels: bb[0-9]+ ---------------------------------------------------
    // The full text is stored in `text` (e.g. "bb0", "bb12").
    LABEL,

    // --- Literals -----------------------------------------------------------
    INT_LIT,        // decimal integer, value in `int_val`
    FLOAT_LIT,      // decimal float,   value in `float_val`

    // --- Punctuation --------------------------------------------------------
    LBRACE,         // {
    RBRACE,         // }
    LPAREN,         // (
    RPAREN,         // )
    COLON,          // :
    SEMICOLON,      // ;
    COMMA,          // ,
    ARROW,          // ->
    LT,             // <
    GT,             // >
    EQ,             // =
    COLON_COLON,    // ::

    // --- End of file --------------------------------------------------------
    END_OF_FILE,
};

// ---------------------------------------------------------------------------
// Token — one token with its kind, raw text, and source position
// ---------------------------------------------------------------------------
struct Token {
    TokenKind kind;       // What type of token is this?
    std::string text;     // The raw characters from the source (always set)
    int64_t  int_val;     // Parsed integer value  (only valid when kind==INT_LIT)
    double   float_val;   // Parsed float value    (only valid when kind==FLOAT_LIT)
    int      line;        // 1-based line number where this token starts
    int      col;         // 1-based column number where this token starts

    // Default constructor — zeroes numeric fields so they're never garbage.
    Token()
        : kind(TokenKind::END_OF_FILE)
        , int_val(0)
        , float_val(0.0)
        , line(0)
        , col(0)
    {}

    // Convenience constructor for the common case (kind + text + position).
    Token(TokenKind k, std::string t, int ln, int cl)
        : kind(k)
        , text(std::move(t))
        , int_val(0)
        , float_val(0.0)
        , line(ln)
        , col(cl)
    {}
};

// ---------------------------------------------------------------------------
// token_kind_name() — human-readable name for error messages
// ---------------------------------------------------------------------------
// The parser will call this to say "expected ';' but got <X>".
const char* token_kind_name(TokenKind k);
