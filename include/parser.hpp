// =============================================================================
// parser.hpp  —  Public interface of the ResIR hand-written top-down parser
// =============================================================================
//
// What is a parser?
//   The lexer gave us a flat list of tokens.  The parser reads that list and
//   checks that the tokens follow the grammar rules.  If they do, it builds
//   an in-memory CFG (Program struct) that the rest of the compiler uses.
//
// What kind of parser is this?
//   A RECURSIVE DESCENT parser.  For each grammar rule (like parseType,
//   parseBlock, parseStatement) there is ONE function in the Parser class.
//   Each function "consumes" the tokens it needs and calls sub-functions for
//   nested rules.
//
// How to use it:
//   Parser p(tokens);       // give it the token list from the Lexer
//   Program prog;
//   if (!p.parse(prog)) {
//       std::cerr << p.get_error() << "\n";
//   }
// =============================================================================

#pragma once
#include "token.hpp"   // Token, TokenKind
#include "cfg.hpp"     // Program, Function, Block, Statement, …

#include <vector>
#include <string>

// ---------------------------------------------------------------------------
// Parser
// ---------------------------------------------------------------------------
class Parser {
public:
    // Constructor: takes the token list produced by Lexer::tokenise().
    // We keep a copy (or move) of the whole list and index into it.
    explicit Parser(std::vector<Token> tokens);

    // parse() — entry point.
    //   Fills `out` with the parsed Program.
    //   Returns true on success, false on the first syntax error.
    //   On failure, call get_error() for the message.
    bool parse(Program& out);

    // Return the last error message.
    const std::string& get_error() const { return error_; }

private:
    // -----------------------------------------------------------------------
    // Token stream management
    // -----------------------------------------------------------------------

    std::vector<Token> tokens_; // the full token list (including EOF)
    size_t             pos_;    // index of the current token

    // peek() — look at the current token without consuming it.
    // Returns a reference so we can read its kind/text/line without copying.
    const Token& peek() const;

    // peek_kind() — just the kind of the current token (very common).
    TokenKind peek_kind() const;

    // advance() — consume the current token and return it.
    Token advance();

    // expect(kind) — consume the current token if it matches `kind`.
    //   If it does NOT match, records a syntax error and returns false.
    //   This is how we enforce "the grammar says `;` must appear here".
    bool expect(TokenKind kind, Token& out);

    // check(kind) — does the current token have this kind?  (does not consume)
    bool check(TokenKind kind) const;

    // -----------------------------------------------------------------------
    // Error handling
    // -----------------------------------------------------------------------

    std::string error_;  // set on the first error

    // error() — format and record a "expected X but got Y" message.
    // Always returns false so callers can write:  return error_(...)
    bool error(const std::string& msg, const Token& at);

    // -----------------------------------------------------------------------
    // Grammar rule functions (one per major EBNF production)
    // -----------------------------------------------------------------------
    // Each function's doc-comment shows the grammar rule it implements.
    // They all return false and set error_ on failure.

    // Program ::= { ExternType } Function
    bool parse_program(Program& out);

    // ExternType ::= 'extern' 'type' Path '{' { Field ';' } '}'
    bool parse_extern_type(ExternType& out);

    // Field ::= Ident ':' Type
    bool parse_field(ExternField& out);

    // Function ::= 'function' Path '(' [Params] ')' '->' RetType '{'
    //               'locals' '{' { Local ':' Type ';' } '}'
    //               'entry' Label ';'
    //               { Block }
    //             '}'
    bool parse_function(Function& out);

    // Params ::= Param { ',' Param }
    // Param  ::= Local ':' Type
    bool parse_params(std::vector<Param>& out);

    // Type ::= PrimType | Path | 'ptr' '<' Type '>'
    bool parse_type(ResIRType& out);

    // Block ::= Label ':' { Stmt ';' } Term ';'
    bool parse_block(Block& out);

    // Stmt  ::= [ Local '=' ] Rhs
    // Returns false if the current token is not the start of a statement
    // (e.g. it is a terminator keyword), in which case `is_term` is set true.
    bool parse_stmt(Statement& out, bool& is_term);

    // Rhs — dispatches to the correct sub-parser based on the keyword
    bool parse_rhs(Rhs& out, int line);

    // Term ::= 'jump' Label
    //        | 'cjump' Local ',' Label ',' Label
    //        | 'return' [ Local ]
    bool parse_term(Terminator& out);

    // -----------------------------------------------------------------------
    // Helpers
    // -----------------------------------------------------------------------

    // Parse a Path (one or more IDENTs joined by ::) into a single string.
    // e.g. "Custom::Struct::Point"
    bool parse_path(std::string& out);

    // Parse a Local token (%name) and return just the name (without %).
    // Strips the leading `%` that the lexer kept in the text field.
    bool parse_local(std::string& out);

    // Convert a TokenKind keyword to a BinOp enum.
    static bool token_to_binop(TokenKind k, BinOp& out);

    // Convert a TokenKind keyword to a UnOp enum.
    static bool token_to_unop(TokenKind k, UnOp& out);

    // Is the current token a terminator keyword?
    bool is_terminator_start() const;

    // Is the current token the start of a Rhs?
    bool is_rhs_start() const;
};
