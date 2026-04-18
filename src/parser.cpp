// =============================================================================
// parser.cpp  —  Hand-written recursive-descent parser for ResIR
// =============================================================================
//
// KEY IDEA: Recursive Descent Parsing
//
//   The grammar is a set of rules.  Each rule says "a <Thing> looks like X
//   followed by Y followed by Z".  We write ONE function per rule, and each
//   function:
//     1. Looks at the NEXT TOKEN (peek) to decide which alternative to take.
//     2. Calls sub-functions for sub-rules.
//     3. Returns false and records an error if the input doesn't match.
//
//   Because ResIR is designed to be predictable (one token of lookahead is
//   always enough), we never need to "backtrack".
//
// FLOW:
//   parse()
//    └─ parse_program()
//        ├─ parse_extern_type()  (zero or more times)
//        └─ parse_function()
//            ├─ parse_params()   → calls parse_type() per param
//            ├─ parse_type()     (return type)
//            ├─ locals section   → calls parse_type() per local
//            └─ parse_block()    (zero or more times)
//                ├─ parse_stmt() (zero or more times)
//                │   └─ parse_rhs()
//                └─ parse_term()
// =============================================================================

#include "parser.hpp"
#include "token.hpp"
#include "cfg.hpp"

#include <sstream>    // std::ostringstream — builds error strings
#include <cassert>    // assert() — catches logic bugs during development

// =============================================================================
// Section 1 – Constructor and token-stream helpers
// =============================================================================

Parser::Parser(std::vector<Token> tokens)
    : tokens_(std::move(tokens))  // take ownership of the token list
    , pos_(0)                      // start at the first token
{}

// peek() — look at the current token without consuming it.
// We always have at least one token (the END_OF_FILE sentinel), so this
// is always safe to call.
const Token& Parser::peek() const {
    // If somehow pos_ is past the end, return the last token (EOF).
    if (pos_ >= tokens_.size()) return tokens_.back();
    return tokens_[pos_];
}

// peek_kind() — just the kind of the current token (saves typing).
TokenKind Parser::peek_kind() const {
    return peek().kind;
}

// advance() — consume and return the current token, then move forward.
Token Parser::advance() {
    Token t = tokens_[pos_];
    // Only advance if we haven't gone past the end.
    if (pos_ < tokens_.size() - 1) ++pos_;
    return t;
}

// check() — does the current token have this kind?  Does NOT consume.
bool Parser::check(TokenKind kind) const {
    return peek_kind() == kind;
}

// expect() — consume a token of the expected kind or record an error.
//
// This is the most-called helper.  Almost every grammar rule has at least
// one mandatory token (like the `;` at the end of a statement).  We write:
//
//   Token semi;
//   if (!expect(TokenKind::SEMICOLON, semi)) return false;
//
// If the current token IS a semicolon it is consumed into `out`.
// If it is NOT, we record an error like:
//   Syntax error at 5:3 — expected `;` but found `function`
bool Parser::expect(TokenKind kind, Token& out) {
    if (peek_kind() == kind) {
        out = advance();
        return true;
    }
    // Build a descriptive error message.
    std::ostringstream oss;
    oss << "Syntax error at " << peek().line << ":" << peek().col
        << " — expected `" << token_kind_name(kind)
        << "` but found `" << peek().text << "`";
    error_ = oss.str();
    return false;
}

// error() — record a free-form error with position info.
bool Parser::error(const std::string& msg, const Token& at) {
    std::ostringstream oss;
    oss << "Syntax error at " << at.line << ":" << at.col << " — " << msg;
    error_ = oss.str();
    return false;
}

// =============================================================================
// Section 2 – Top-level entry point
// =============================================================================

bool Parser::parse(Program& out) {
    return parse_program(out);
}

// =============================================================================
// Section 3 – Program ::= { ExternType } Function
// =============================================================================
//
// The outer structure of a ResIR file is:
//   0 or more `extern type` blocks
//   exactly 1 `function` block
//
// We can tell them apart by looking at the next token:
//   `extern`   → parse_extern_type()
//   `function` → parse_function()
// =============================================================================

bool Parser::parse_program(Program& out) {
    // Parse zero or more extern type declarations.
    // We keep looping as long as the next token is `extern`.
    while (check(TokenKind::KW_EXTERN)) {
        ExternType et;
        if (!parse_extern_type(et)) return false;
        out.extern_types.push_back(std::move(et));
    }

    // Now we must see `function`.
    if (!check(TokenKind::KW_FUNCTION)) {
        return error("expected `function` declaration", peek());
    }

    return parse_function(out.function);
}

// =============================================================================
// Section 4 – ExternType ::= 'extern' 'type' Path '{' { Field ';' } '}'
// =============================================================================

bool Parser::parse_extern_type(ExternType& out) {
    Token tok;

    // Consume `extern`
    if (!expect(TokenKind::KW_EXTERN, tok)) return false;

    // Consume `type`
    if (!expect(TokenKind::KW_TYPE, tok)) return false;

    // Parse the path (e.g. "Custom::Struct::Point")
    if (!parse_path(out.path)) return false;

    // Consume `{`
    if (!expect(TokenKind::LBRACE, tok)) return false;

    // Parse zero or more fields until we see `}`
    while (!check(TokenKind::RBRACE) && !check(TokenKind::END_OF_FILE)) {
        ExternField f;
        if (!parse_field(f)) return false;
        out.fields.push_back(std::move(f));
    }

    // Consume `}`
    if (!expect(TokenKind::RBRACE, tok)) return false;

    return true;
}

// Field ::= Ident ':' Type ';'
bool Parser::parse_field(ExternField& out) {
    Token tok;

    // Field names are plain identifiers (not locals — they have no `%`)
    if (!check(TokenKind::IDENT)) {
        return error("expected field name (identifier)", peek());
    }
    out.name = advance().text;

    if (!expect(TokenKind::COLON, tok)) return false;
    if (!parse_type(out.type)) return false;
    if (!expect(TokenKind::SEMICOLON, tok)) return false;

    return true;
}

// =============================================================================
// Section 5 – Function
// =============================================================================
//
// Function ::= 'function' Path '(' [Params] ')' '->' RetType '{'
//                'locals' '{' { Local ':' Type ';' } '}'
//                'entry' Label ';'
//                { Block }
//              '}'
// =============================================================================

bool Parser::parse_function(Function& out) {
    Token tok;

    // Consume `function`
    if (!expect(TokenKind::KW_FUNCTION, tok)) return false;

    // Parse the function name path (e.g. "Math::abs_diff")
    if (!parse_path(out.path)) return false;

    // Consume `(`
    if (!expect(TokenKind::LPAREN, tok)) return false;

    // Parse optional parameter list
    if (!check(TokenKind::RPAREN)) {
        if (!parse_params(out.params)) return false;
    }

    // Consume `)`
    if (!expect(TokenKind::RPAREN, tok)) return false;

    // Consume `->`
    if (!expect(TokenKind::ARROW, tok)) return false;

    // Parse return type: either `void` or a real type
    if (check(TokenKind::KW_VOID)) {
        advance(); // consume `void`
        out.returns_void = true;
    } else {
        out.returns_void = false;
        if (!parse_type(out.return_type)) return false;
    }

    // Consume `{`
    if (!expect(TokenKind::LBRACE, tok)) return false;

    // -----------------------------------------------------------------------
    // Locals section:  'locals' '{' { Local ':' Type ';' } '}'
    // -----------------------------------------------------------------------
    if (!expect(TokenKind::KW_LOCALS, tok)) return false;
    if (!expect(TokenKind::LBRACE,    tok)) return false;

    // Parse local declarations until we see `}`
    while (!check(TokenKind::RBRACE) && !check(TokenKind::END_OF_FILE)) {
        LocalDecl ld;
        // A local starts with `%`
        if (!parse_local(ld.name)) return false;
        if (!expect(TokenKind::COLON, tok)) return false;
        if (!parse_type(ld.type)) return false;
        if (!expect(TokenKind::SEMICOLON, tok)) return false;
        out.locals.push_back(std::move(ld));
    }

    if (!expect(TokenKind::RBRACE, tok)) return false;

    // -----------------------------------------------------------------------
    // Entry label:  'entry' Label ';'
    // -----------------------------------------------------------------------
    if (!expect(TokenKind::KW_ENTRY, tok)) return false;

    if (!check(TokenKind::LABEL)) {
        return error("expected a label (e.g. bb0) after `entry`", peek());
    }
    out.entry_label = advance().text; // e.g. "bb0"

    if (!expect(TokenKind::SEMICOLON, tok)) return false;

    // -----------------------------------------------------------------------
    // Parse basic blocks until we see the closing `}`
    // -----------------------------------------------------------------------
    while (!check(TokenKind::RBRACE) && !check(TokenKind::END_OF_FILE)) {
        Block blk;
        if (!parse_block(blk)) return false;
        out.blocks.push_back(std::move(blk));
    }

    // Consume the closing `}` of the function
    if (!expect(TokenKind::RBRACE, tok)) return false;

    return true;
}

// =============================================================================
// Section 6 – Params ::= Param { ',' Param }
//             Param  ::= Local ':' Type
// =============================================================================

bool Parser::parse_params(std::vector<Param>& out) {
    Token tok;

    while (true) {
        Param p;
        if (!parse_local(p.name)) return false;
        if (!expect(TokenKind::COLON, tok)) return false;
        if (!parse_type(p.type)) return false;
        out.push_back(std::move(p));

        // After each param, if we see `,` there is another one.
        if (check(TokenKind::COMMA)) {
            advance(); // consume `,`
        } else {
            break; // no more params
        }
    }

    return true;
}

// =============================================================================
// Section 7 – Type ::= PrimType | Path | 'ptr' '<' Type '>'
// =============================================================================
//
// This is a recursive rule because `ptr` can be nested:
//   ptr<ptr<i32>>  parses as:  PTR → PTR → I32
//
// We use std::make_unique<ResIRType>(...) to allocate the inner type on the
// heap.  std::make_unique is the safe way to create a unique_ptr:
//   auto p = std::make_unique<T>(args);   // allocates T(args), returns unique_ptr<T>
// =============================================================================

bool Parser::parse_type(ResIRType& out) {
    Token tok = peek();

    // --- Primitive types ----------------------------------------------------
    if (check(TokenKind::KW_BOOL)) { advance(); out = ResIRType(TypeKind::BOOL); return true; }
    if (check(TokenKind::KW_I32))  { advance(); out = ResIRType(TypeKind::I32);  return true; }
    if (check(TokenKind::KW_I64))  { advance(); out = ResIRType(TypeKind::I64);  return true; }
    if (check(TokenKind::KW_U32))  { advance(); out = ResIRType(TypeKind::U32);  return true; }
    if (check(TokenKind::KW_F64))  { advance(); out = ResIRType(TypeKind::F64);  return true; }

    // --- ptr<Type> ----------------------------------------------------------
    if (check(TokenKind::KW_PTR)) {
        advance(); // consume `ptr`

        Token lt;
        if (!expect(TokenKind::LT, lt)) return false;   // consume `<`

        // Recursively parse the inner type
        auto inner = std::make_unique<ResIRType>();
        if (!parse_type(*inner)) return false;

        Token gt;
        if (!expect(TokenKind::GT, gt)) return false;   // consume `>`

        // Move the inner pointer into the PTR type.
        out = ResIRType(TypeKind::PTR, std::move(inner));
        return true;
    }

    // --- Path (user-defined type) -------------------------------------------
    // A path starts with an IDENT token.
    if (check(TokenKind::IDENT)) {
        std::string path;
        if (!parse_path(path)) return false;
        out = ResIRType(TypeKind::PATH, path);
        return true;
    }

    return error("expected a type (e.g. i32, bool, ptr<i32>, Custom::Type)", tok);
}

// =============================================================================
// Section 8 – Block ::= Label ':' { Stmt ';' } Term ';'
// =============================================================================
//
// A basic block starts with a label followed by a colon, then zero or more
// statements, then exactly one terminator.
//
// How do we know when we've reached the terminator?
//   The grammar defines terminators as: jump, cjump, return.
//   We call is_terminator_start() to peek at the next keyword.
// =============================================================================

bool Parser::parse_block(Block& out) {
    Token tok;

    // The block must start with a LABEL token (e.g. "bb0").
    if (!check(TokenKind::LABEL)) {
        return error("expected a block label (e.g. bb0)", peek());
    }
    Token label_tok = advance();
    out.label = label_tok.text;
    out.line  = label_tok.line;

    // Consume the `:` after the label
    if (!expect(TokenKind::COLON, tok)) return false;

    // Parse zero or more statements.
    // We stop when we see a terminator keyword or end-of-file.
    while (!is_terminator_start() && !check(TokenKind::END_OF_FILE)
           && !check(TokenKind::RBRACE)) {
        Statement s;
        bool is_term = false;
        if (!parse_stmt(s, is_term)) return false;
        if (is_term) {
            // parse_stmt should not set is_term for non-terminators,
            // but guard against it just in case.
            break;
        }
        out.stmts.push_back(std::move(s));
    }

    // Parse the mandatory terminator.
    if (!is_terminator_start()) {
        return error("expected a terminator (jump/cjump/return) to end block `"
                     + out.label + "`", peek());
    }
    if (!parse_term(out.term)) return false;

    // Consume the `;` after the terminator.
    if (!expect(TokenKind::SEMICOLON, tok)) return false;

    return true;
}

// =============================================================================
// Section 9 – Stmt ::= [ Local '=' ] Rhs ';'
// =============================================================================
//
// A statement is either:
//   %dest = Rhs     (assignment — has_dest = true)
//   Rhs             (effect-only — has_dest = false, e.g. store)
//
// How to tell them apart?
//   If the current token is a LOCAL (`%name`), look ahead.
//   In ResIR, a LOCAL followed by `=` is an assignment.
//   A LOCAL followed by anything else (including being used as a COPY rhs
//   with no `=`) is just a COPY rhs with no destination.
//
//   Wait — but a COPY rhs also starts with a LOCAL.  So when we see a LOCAL,
//   how do we know if it's the destination or the rhs?
//
//   The trick: we peek TWO tokens ahead.
//   tokens_[pos_]   = LOCAL
//   tokens_[pos_+1] = EQ  → it's an assignment: destination = rhs
//   tokens_[pos_+1] = anything else → it IS the rhs (plain copy)
// =============================================================================

bool Parser::parse_stmt(Statement& s, bool& is_term) {
    is_term = false;
    s.line = peek().line;

    // If the current token is a terminator, signal the caller to stop.
    if (is_terminator_start()) {
        is_term = true;
        return true; // not an error — caller checks is_term
    }

    // Detect assignment:  %dest = Rhs
    //   pos_  → LOCAL
    //   pos_+1 → EQ
    if (check(TokenKind::LOCAL) &&
        pos_ + 1 < tokens_.size() &&
        tokens_[pos_ + 1].kind == TokenKind::EQ)
    {
        // Consume the destination local
        std::string dest_name;
        if (!parse_local(dest_name)) return false;
        s.has_dest = true;
        s.dest     = dest_name;

        // Consume `=`
        Token eq;
        if (!expect(TokenKind::EQ, eq)) return false;
    }

    // Parse the right-hand side.
    if (!parse_rhs(s.rhs, s.line)) return false;

    // Consume the `;` at the end of the statement.
    Token semi;
    if (!expect(TokenKind::SEMICOLON, semi)) return false;

    return true;
}

// =============================================================================
// Section 10 – Rhs (dispatch on the leading keyword or LOCAL)
// =============================================================================
//
// Rhs ::= Local
//       | 'const' Literal
//       | 'cast' Local 'to' Type
//       | 'un' UnOp Local
//       | 'bin' BinOp Local ',' Local
//       | 'addr_of' Local
//       | 'member_ptr' Local ',' Ident
//       | 'load' Local
//       | 'store' Local ',' Local
//       | 'call' Path '(' [Args] ')'
//
// We look at the FIRST token to decide which alternative:
//   LOCAL    → COPY
//   `const`  → CONST
//   `cast`   → CAST
//   `un`     → UN
//   `bin`    → BIN
//   `addr_of`→ ADDR_OF
//   `member_ptr` → MEMBER_PTR
//   `load`   → LOAD
//   `store`  → STORE
//   `call`   → CALL
// =============================================================================

bool Parser::parse_rhs(Rhs& out, int /*line*/) {
    Token tok;

    // -----------------------------------------------------------------------
    // COPY:  Rhs = Local  (just another local variable — a copy)
    // -----------------------------------------------------------------------
    if (check(TokenKind::LOCAL)) {
        out.kind = RhsKind::COPY;
        // parse_local strips the `%` prefix and returns just the name.
        return parse_local(out.op1);
    }

    // -----------------------------------------------------------------------
    // CONST:  const Literal
    // -----------------------------------------------------------------------
    if (check(TokenKind::KW_CONST)) {
        advance(); // consume `const`
        out.kind = RhsKind::CONST;

        if (check(TokenKind::INT_LIT)) {
            Token t = advance();
            out.literal.kind    = LiteralKind::INT;
            out.literal.int_val = t.int_val;
            return true;
        }
        if (check(TokenKind::FLOAT_LIT)) {
            Token t = advance();
            out.literal.kind      = LiteralKind::FLOAT;
            out.literal.float_val = t.float_val;
            return true;
        }
        if (check(TokenKind::KW_TRUE)) {
            advance();
            out.literal.kind     = LiteralKind::BOOL;
            out.literal.bool_val = true;
            return true;
        }
        if (check(TokenKind::KW_FALSE)) {
            advance();
            out.literal.kind     = LiteralKind::BOOL;
            out.literal.bool_val = false;
            return true;
        }
        if (check(TokenKind::KW_NULL)) {
            advance();
            out.literal.kind = LiteralKind::NULL_LIT;
            return true;
        }
        return error("expected a literal after `const`", peek());
    }

    // -----------------------------------------------------------------------
    // CAST:  cast Local 'to' Type
    // -----------------------------------------------------------------------
    if (check(TokenKind::KW_CAST)) {
        advance(); // consume `cast`
        out.kind = RhsKind::CAST;
        if (!parse_local(out.op1)) return false;
        if (!expect(TokenKind::KW_TO, tok)) return false;
        if (!parse_type(out.cast_type)) return false;
        return true;
    }

    // -----------------------------------------------------------------------
    // UN:  un UnOp Local
    // -----------------------------------------------------------------------
    if (check(TokenKind::KW_UN)) {
        advance(); // consume `un`
        out.kind = RhsKind::UN;
        if (!token_to_unop(peek_kind(), out.un_op)) {
            return error("expected a unary operator (neg/not)", peek());
        }
        advance(); // consume the operator keyword
        if (!parse_local(out.op1)) return false;
        return true;
    }

    // -----------------------------------------------------------------------
    // BIN:  bin BinOp Local ',' Local
    // -----------------------------------------------------------------------
    if (check(TokenKind::KW_BIN)) {
        advance(); // consume `bin`
        out.kind = RhsKind::BIN;
        if (!token_to_binop(peek_kind(), out.bin_op)) {
            return error("expected a binary operator (add/sub/mul/…)", peek());
        }
        advance(); // consume the operator keyword
        if (!parse_local(out.op1)) return false;
        if (!expect(TokenKind::COMMA, tok)) return false;
        if (!parse_local(out.op2)) return false;
        return true;
    }

    // -----------------------------------------------------------------------
    // ADDR_OF:  addr_of Local
    // -----------------------------------------------------------------------
    if (check(TokenKind::KW_ADDR_OF)) {
        advance();
        out.kind = RhsKind::ADDR_OF;
        return parse_local(out.op1);
    }

    // -----------------------------------------------------------------------
    // MEMBER_PTR:  member_ptr Local ',' Ident
    // -----------------------------------------------------------------------
    if (check(TokenKind::KW_MEMBER_PTR)) {
        advance();
        out.kind = RhsKind::MEMBER_PTR;
        if (!parse_local(out.op1)) return false;
        if (!expect(TokenKind::COMMA, tok)) return false;
        // The field name is a plain IDENT (no `%`)
        if (!check(TokenKind::IDENT)) {
            return error("expected a field name after `member_ptr %ptr,`", peek());
        }
        out.field = advance().text;
        return true;
    }

    // -----------------------------------------------------------------------
    // LOAD:  load Local
    // -----------------------------------------------------------------------
    if (check(TokenKind::KW_LOAD)) {
        advance();
        out.kind = RhsKind::LOAD;
        return parse_local(out.op1);
    }

    // -----------------------------------------------------------------------
    // STORE:  store Local ',' Local
    // -----------------------------------------------------------------------
    if (check(TokenKind::KW_STORE)) {
        advance();
        out.kind = RhsKind::STORE;
        if (!parse_local(out.op1)) return false;
        if (!expect(TokenKind::COMMA, tok)) return false;
        return parse_local(out.op2);
    }

    // -----------------------------------------------------------------------
    // CALL:  call Path '(' [Args] ')'
    // -----------------------------------------------------------------------
    if (check(TokenKind::KW_CALL)) {
        advance(); // consume `call`
        out.kind = RhsKind::CALL;

        // Parse the callee path (e.g. "Math::abs_diff")
        if (!parse_path(out.call_path)) return false;

        // Consume `(`
        if (!expect(TokenKind::LPAREN, tok)) return false;

        // Parse optional argument list:  Local { ',' Local }
        if (!check(TokenKind::RPAREN)) {
            while (true) {
                std::string arg;
                if (!parse_local(arg)) return false;
                out.call_args.push_back(std::move(arg));
                if (check(TokenKind::COMMA)) {
                    advance(); // consume `,`
                } else {
                    break;
                }
            }
        }

        // Consume `)`
        if (!expect(TokenKind::RPAREN, tok)) return false;
        return true;
    }

    // Nothing matched — unexpected token in rhs position.
    return error("expected an instruction (const/cast/un/bin/load/…) or a local variable", peek());
}

// =============================================================================
// Section 11 – Term ::= 'jump' Label
//                     | 'cjump' Local ',' Label ',' Label
//                     | 'return' [ Local ]
// =============================================================================

bool Parser::parse_term(Terminator& out) {
    Token tok;
    out.line = peek().line;

    // -----------------------------------------------------------------------
    // JUMP:  jump Label
    // -----------------------------------------------------------------------
    if (check(TokenKind::KW_JUMP)) {
        advance(); // consume `jump`
        out.kind = TermKind::JUMP;
        if (!check(TokenKind::LABEL)) {
            return error("expected a label after `jump`", peek());
        }
        out.label1 = advance().text;
        return true;
    }

    // -----------------------------------------------------------------------
    // CJUMP:  cjump Local ',' Label ',' Label
    // -----------------------------------------------------------------------
    if (check(TokenKind::KW_CJUMP)) {
        advance(); // consume `cjump`
        out.kind = TermKind::CJUMP;
        if (!parse_local(out.cond)) return false;
        if (!expect(TokenKind::COMMA, tok)) return false;
        if (!check(TokenKind::LABEL)) {
            return error("expected true-branch label in `cjump`", peek());
        }
        out.label1 = advance().text;
        if (!expect(TokenKind::COMMA, tok)) return false;
        if (!check(TokenKind::LABEL)) {
            return error("expected false-branch label in `cjump`", peek());
        }
        out.label2 = advance().text;
        return true;
    }

    // -----------------------------------------------------------------------
    // RETURN:  return [ Local ]
    // -----------------------------------------------------------------------
    if (check(TokenKind::KW_RETURN)) {
        advance(); // consume `return`
        out.kind = TermKind::RETURN;

        // Optional return value: present only if next token is a LOCAL.
        if (check(TokenKind::LOCAL)) {
            out.has_ret_val = true;
            if (!parse_local(out.ret_local)) return false;
        } else {
            out.has_ret_val = false;
        }
        return true;
    }

    return error("expected a terminator (jump/cjump/return)", peek());
}

// =============================================================================
// Section 12 – Helpers
// =============================================================================

// parse_path — consume one or more IDENTs separated by `::`.
// Produces a single string like "Math::abs_diff" or "Custom::Struct::Point".
//
// Path ::= Ident { '::' Ident }
//
// NOTE: The lexer classifies every reserved word as a keyword token, not IDENT.
// After `::` the context is unambiguous — e.g. `Test::ptr` means a function
// named "ptr" inside namespace "Test", NOT the `ptr` type keyword.  So we
// accept any token after `::` and use its raw text as the path segment.
bool Parser::parse_path(std::string& out) {
    if (!check(TokenKind::IDENT)) {
        return error("expected an identifier to start a path", peek());
    }
    out = advance().text; // first segment

    // Keep consuming `:: <any-token>` pairs as long as they appear.
    // After `::` we just take whatever token is there and use its text,
    // because in a qualified path position even keywords are identifiers.
    while (check(TokenKind::COLON_COLON)) {
        advance(); // consume `::`
        // Accept any non-EOF, non-punctuation token as a path segment.
        TokenKind k = peek_kind();
        if (k == TokenKind::END_OF_FILE || k == TokenKind::SEMICOLON ||
            k == TokenKind::LBRACE     || k == TokenKind::RBRACE     ||
            k == TokenKind::LPAREN     || k == TokenKind::RPAREN) {
            return error("expected an identifier after `::`", peek());
        }
        out += "::" + advance().text;
    }
    return true;
}

// parse_local — consume a LOCAL token and strip the leading `%`.
// The lexer keeps the `%` in the text field (e.g. "%x"), so we need to
// strip it to get just the identifier name ("x").
bool Parser::parse_local(std::string& out) {
    if (!check(TokenKind::LOCAL)) {
        return error("expected a local variable (e.g. %x)", peek());
    }
    Token t = advance();
    // t.text is "%name"; we want "name".
    // t.text.substr(1) returns everything from index 1 onwards,
    // i.e. it skips the `%` character.
    out = t.text.substr(1);
    return true;
}

// token_to_binop — map a TokenKind keyword to the BinOp enum.
bool Parser::token_to_binop(TokenKind k, BinOp& out) {
    switch (k) {
        case TokenKind::KW_ADD: out = BinOp::ADD; return true;
        case TokenKind::KW_SUB: out = BinOp::SUB; return true;
        case TokenKind::KW_MUL: out = BinOp::MUL; return true;
        case TokenKind::KW_DIV: out = BinOp::DIV; return true;
        case TokenKind::KW_MOD: out = BinOp::MOD; return true;
        case TokenKind::KW_EQ:  out = BinOp::EQ;  return true;
        case TokenKind::KW_NE:  out = BinOp::NE;  return true;
        case TokenKind::KW_LT:  out = BinOp::LT;  return true;
        case TokenKind::KW_LE:  out = BinOp::LE;  return true;
        case TokenKind::KW_GT_OP: out = BinOp::GT; return true;
        case TokenKind::KW_GE:  out = BinOp::GE;  return true;
        case TokenKind::KW_AND: out = BinOp::AND; return true;
        case TokenKind::KW_OR:  out = BinOp::OR;  return true;
        default: return false;
    }
}

// token_to_unop — map a TokenKind keyword to the UnOp enum.
bool Parser::token_to_unop(TokenKind k, UnOp& out) {
    if (k == TokenKind::KW_NEG) { out = UnOp::NEG; return true; }
    if (k == TokenKind::KW_NOT) { out = UnOp::NOT; return true; }
    return false;
}

// is_terminator_start — is the current token one of jump/cjump/return?
bool Parser::is_terminator_start() const {
    TokenKind k = peek_kind();
    return k == TokenKind::KW_JUMP   ||
           k == TokenKind::KW_CJUMP  ||
           k == TokenKind::KW_RETURN;
}

// is_rhs_start — is the current token the start of a valid Rhs?
// (Used defensively in parse_stmt.)
bool Parser::is_rhs_start() const {
    TokenKind k = peek_kind();
    return k == TokenKind::LOCAL         ||
           k == TokenKind::KW_CONST      ||
           k == TokenKind::KW_CAST       ||
           k == TokenKind::KW_UN         ||
           k == TokenKind::KW_BIN        ||
           k == TokenKind::KW_ADDR_OF    ||
           k == TokenKind::KW_MEMBER_PTR ||
           k == TokenKind::KW_LOAD       ||
           k == TokenKind::KW_STORE      ||
           k == TokenKind::KW_CALL;
}
