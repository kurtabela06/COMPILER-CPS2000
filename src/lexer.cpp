// =============================================================================
// lexer.cpp  —  Table-driven lexer implementation for ResIR
// =============================================================================
//
// Overview of how a table-driven lexer works:
//
//   A Deterministic Finite Automaton (DFA) can be visualised as a directed
//   graph where:
//     • each node  = a STATE  (e.g. "we are reading digits")
//     • each edge  = an INPUT CLASS that causes a state transition
//     • accepting nodes = states that mean "we just finished a token"
//
//   Instead of coding this graph as a big if/else chain, we encode it as a
//   2-D array:
//
//       next_state = TRANSITION_[current_state][input_class]
//
//   One tight loop drives the whole scanner:
//     1. Classify the current character into an InputClass (column index).
//     2. Look up TRANSITION_[state][class] to get the next state.
//     3. If next_state == DONE → emit a token and reset.
//     4. If next_state == ERR  → report an error.
//     5. Otherwise → advance position and loop.
//
// The big picture (state machine diagram) is in TASK1_GUIDE.md.
// =============================================================================

#include "lexer.hpp"
#include "token.hpp"

#include <cctype>     // std::isalpha, std::isdigit, etc.
#include <stdexcept>  // std::stoi, std::stod helpers
#include <sstream>    // std::ostringstream — for building error messages
#include <string>
#include <unordered_map>

// Bring the nested enums into scope locally so table rows stay readable.
using S = Lexer::State;
using C = Lexer::InputClass;

// =============================================================================
// Section 1 – Transition table definition
// =============================================================================
//
// TRANSITION_[state][class] = next state
//
// Row order matches the State enum (START=0, IN_IDENT=1, …).
// Column order matches the InputClass enum (ALPHA=0, DIGIT=1, …).
//
// Read each row as: "When I am in state X and I see a character of class Y,
//                    move to state Z."
//
// States used as row indices:
//   START=0  IN_IDENT=1  IN_LOCAL=2  IN_INT_LIT=3  IN_FLOAT_FRAC=4
//   IN_MINUS=5  IN_COLON=6  IN_SLASH=7  IN_LINE_COMMENT=8  DONE=9  ERR=10
//
// Column indices:
//   ALPHA=0  DIGIT=1  PERCENT=2  DOT=3  MINUS=4  GT=5  COLON=6
//   SLASH=7  PUNCT=8  SPACE=9  NEWLINE=10  OTHER=11
// =============================================================================

const Lexer::State Lexer::TRANSITION_
    [static_cast<int>(State::NUM_STATES)]
    [static_cast<int>(InputClass::NUM_CLASSES)] =
{
    //          ALPHA         DIGIT         PERCENT       DOT
    //          MINUS         GT            COLON         SLASH
    //          PUNCT         SPACE         NEWLINE       OTHER

    /* START */
    { S::IN_IDENT, S::IN_INT_LIT, S::IN_LOCAL, S::ERR,
      S::IN_MINUS, S::DONE,       S::IN_COLON, S::IN_SLASH,
      S::DONE,     S::START,      S::START,    S::ERR },

    /* IN_IDENT — keep consuming alphanumeric / underscore */
    // Any non-ident character means the identifier is complete (DONE), so the
    // main loop will NOT consume that character; it stays for the next token.
    { S::IN_IDENT, S::IN_IDENT, S::DONE,  S::DONE,
      S::DONE,     S::DONE,     S::DONE,  S::DONE,
      S::DONE,     S::DONE,     S::DONE,  S::DONE },

    /* IN_LOCAL — after `%`, reading the ident part */
    { S::IN_LOCAL, S::IN_LOCAL, S::DONE, S::DONE,
      S::DONE,     S::DONE,     S::DONE, S::DONE,
      S::DONE,     S::DONE,     S::DONE, S::DONE },

    /* IN_INT_LIT — accumulating decimal digits */
    // `.` transitions to IN_FLOAT_FRAC (start of fractional part).
    { S::DONE,         S::IN_INT_LIT, S::DONE, S::IN_FLOAT_FRAC,
      S::DONE,         S::DONE,       S::DONE, S::DONE,
      S::DONE,         S::DONE,       S::DONE, S::DONE },

    /* IN_FLOAT_FRAC — digits after the decimal point */
    // A second `.` is an error inside a float.
    { S::DONE, S::IN_FLOAT_FRAC, S::DONE, S::ERR,
      S::DONE, S::DONE,          S::DONE, S::DONE,
      S::DONE, S::DONE,          S::DONE, S::DONE },

    /* IN_MINUS — just saw `-`, hoping to see `>` for `->` */
    // Only `>` is valid here; everything else is an error.
    { S::ERR, S::ERR, S::ERR, S::ERR,
      S::ERR, S::DONE,S::ERR, S::ERR,
      S::ERR, S::ERR, S::ERR, S::ERR },

    /* IN_COLON — just saw `:`; a second `:` makes `::`, otherwise plain `:` */
    // The actual `:` vs `::` decision is made in the dispatcher below,
    // but we still need a valid table entry.  We set all to DONE because
    // the dispatcher peeks ahead before consuming.
    { S::DONE, S::DONE, S::DONE, S::DONE,
      S::DONE, S::DONE, S::DONE, S::DONE,
      S::DONE, S::DONE, S::DONE, S::DONE },

    /* IN_SLASH — just saw `/`, hoping for another `/` to make `//` */
    // Only a second `/` is valid; everything else is an error.
    { S::ERR,            S::ERR,            S::ERR, S::ERR,
      S::ERR,            S::ERR,            S::ERR, S::IN_LINE_COMMENT,
      S::ERR,            S::ERR,            S::ERR, S::ERR },

    /* IN_LINE_COMMENT — skip everything until newline */
    { S::IN_LINE_COMMENT, S::IN_LINE_COMMENT, S::IN_LINE_COMMENT, S::IN_LINE_COMMENT,
      S::IN_LINE_COMMENT, S::IN_LINE_COMMENT, S::IN_LINE_COMMENT, S::IN_LINE_COMMENT,
      S::IN_LINE_COMMENT, S::IN_LINE_COMMENT, S::START,           S::IN_LINE_COMMENT },

    /* DONE — terminal accepting state (not used as a source row) */
    { S::ERR, S::ERR, S::ERR, S::ERR,
      S::ERR, S::ERR, S::ERR, S::ERR,
      S::ERR, S::ERR, S::ERR, S::ERR },

    /* ERR — terminal error state (not used as a source row) */
    { S::ERR, S::ERR, S::ERR, S::ERR,
      S::ERR, S::ERR, S::ERR, S::ERR,
      S::ERR, S::ERR, S::ERR, S::ERR },
};

// =============================================================================
// Section 2 – Keyword lookup table
// =============================================================================
// We store all reserved words in an unordered_map for O(1) average lookup.
// After scanning an identifier we check this map; if the word is found we
// return its keyword kind instead of IDENT.

TokenKind Lexer::keyword_lookup(const std::string& s) {
    // Static means this map is built once, the first time we call the function.
    static const std::unordered_map<std::string, TokenKind> TABLE = {
        {"function",   TokenKind::KW_FUNCTION},
        {"locals",     TokenKind::KW_LOCALS},
        {"entry",      TokenKind::KW_ENTRY},
        {"const",      TokenKind::KW_CONST},
        {"cast",       TokenKind::KW_CAST},
        {"un",         TokenKind::KW_UN},
        {"bin",        TokenKind::KW_BIN},
        {"addr_of",    TokenKind::KW_ADDR_OF},
        {"member_ptr", TokenKind::KW_MEMBER_PTR},
        {"load",       TokenKind::KW_LOAD},
        {"store",      TokenKind::KW_STORE},
        {"call",       TokenKind::KW_CALL},
        {"jump",       TokenKind::KW_JUMP},
        {"cjump",      TokenKind::KW_CJUMP},
        {"return",     TokenKind::KW_RETURN},
        {"void",       TokenKind::KW_VOID},
        {"ptr",        TokenKind::KW_PTR},
        {"to",         TokenKind::KW_TO},
        {"extern",     TokenKind::KW_EXTERN},
        {"type",       TokenKind::KW_TYPE},
        {"neg",        TokenKind::KW_NEG},
        {"not",        TokenKind::KW_NOT},
        {"add",        TokenKind::KW_ADD},
        {"sub",        TokenKind::KW_SUB},
        {"mul",        TokenKind::KW_MUL},
        {"div",        TokenKind::KW_DIV},
        {"mod",        TokenKind::KW_MOD},
        {"eq",         TokenKind::KW_EQ},
        {"ne",         TokenKind::KW_NE},
        {"lt",         TokenKind::KW_LT},
        {"le",         TokenKind::KW_LE},
        {"gt",         TokenKind::KW_GT_OP},
        {"ge",         TokenKind::KW_GE},
        {"and",        TokenKind::KW_AND},
        {"or",         TokenKind::KW_OR},
        {"bool",       TokenKind::KW_BOOL},
        {"i32",        TokenKind::KW_I32},
        {"i64",        TokenKind::KW_I64},
        {"u32",        TokenKind::KW_U32},
        {"f64",        TokenKind::KW_F64},
        {"true",       TokenKind::KW_TRUE},
        {"false",      TokenKind::KW_FALSE},
        {"null",       TokenKind::KW_NULL},
    };

    auto it = TABLE.find(s);
    return (it != TABLE.end()) ? it->second : TokenKind::IDENT;
}

// =============================================================================
// Section 3 – Constructor
// =============================================================================

Lexer::Lexer(std::string source)
    : src_(std::move(source))  // take ownership of the source string
    , pos_(0)                   // start at the first character
    , line_(1)                  // line numbers are 1-based
    , col_(1)                   // column numbers are 1-based
{}

// =============================================================================
// Section 4 – Character helpers
// =============================================================================

// peek() — look at the current character without consuming it.
// Returns '\0' when we have reached the end of the file.
// This is a very common pattern: it lets us "look ahead" safely.
char Lexer::peek() const {
    if (pos_ >= src_.size()) return '\0';
    return src_[pos_];
}

// peek_next() — look one position ahead (used for `//`, `->`, `::`).
char Lexer::peek_next() const {
    if (pos_ + 1 >= src_.size()) return '\0';
    return src_[pos_ + 1];
}

// advance() — consume the current character and move forward.
// Also keeps line_ and col_ accurate for error messages.
char Lexer::advance() {
    char c = src_[pos_++];  // read and increment position
    if (c == '\n') {
        ++line_;             // newline → move to the next line
        col_ = 1;            // reset column to start of new line
    } else {
        ++col_;
    }
    return c;
}

// classify() — map a character to its InputClass (column in the table).
// This function is the "character classifier" part of the table-driven design.
Lexer::InputClass Lexer::classify(char c) {
    if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || c == '_')
        return InputClass::ALPHA;
    if (c >= '0' && c <= '9')
        return InputClass::DIGIT;
    if (c == '%')  return InputClass::PERCENT;
    if (c == '.')  return InputClass::DOT;
    if (c == '-')  return InputClass::MINUS;
    if (c == '>')  return InputClass::GT;
    if (c == ':')  return InputClass::COLON;
    if (c == '/')  return InputClass::SLASH;
    if (c == '{' || c == '}' || c == '(' || c == ')' ||
        c == ';' || c == ',' || c == '<' || c == '=' )
        return InputClass::PUNCT;
    if (c == ' ' || c == '\t' || c == '\r')
        return InputClass::SPACE;
    if (c == '\n') return InputClass::NEWLINE;
    return InputClass::OTHER;
}

// skip_whitespace_and_comments() — skip spaces, tabs, newlines, and // lines.
// This runs before every token so the main scanner starts at real input.
void Lexer::skip_whitespace_and_comments() {
    while (true) {
        char c = peek();
        if (c == ' ' || c == '\t' || c == '\r' || c == '\n') {
            advance();
        } else if (c == '/' && peek_next() == '/') {
            // Line comment: skip everything up to (but not including) '\n'
            advance(); // consume first '/'
            advance(); // consume second '/'
            while (peek() != '\n' && peek() != '\0') advance();
        } else {
            break; // not whitespace or comment — stop
        }
    }
}

// =============================================================================
// Section 5 – Token-specific scanners
// =============================================================================

// is_label() — check if a string looks like bb[0-9]+ (e.g. "bb0", "bb12").
bool Lexer::is_label(const std::string& s) {
    if (s.size() < 3) return false;           // need at least "bb" + one digit
    if (s[0] != 'b' || s[1] != 'b') return false;
    for (size_t i = 2; i < s.size(); ++i)
        if (s[i] < '0' || s[i] > '9') return false;
    return true;
}

// scan_ident() — collect a full identifier (or keyword / label).
// The FIRST character has already been consumed by next_token(); it is passed
// in as `first`.
//
// After collecting all alphanumeric / underscore characters we check:
//   1. Does it look like a label? (bb[0-9]+) → LABEL
//   2. Is it a reserved word?               → keyword TokenKind
//   3. Otherwise                            → IDENT
Token Lexer::scan_ident(char first, int tok_line, int tok_col) {
    std::string text;
    text += first;

    // Keep consuming while the next character is alphanumeric or '_'.
    while (true) {
        char c = peek();
        if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
            (c >= '0' && c <= '9') || c == '_') {
            text += advance();
        } else {
            break;
        }
    }

    // Determine token kind from the collected text.
    TokenKind kind;
    if (is_label(text)) {
        kind = TokenKind::LABEL;
    } else {
        kind = keyword_lookup(text); // returns IDENT if not a keyword
    }

    Token t(kind, text, tok_line, tok_col);
    return t;
}

// scan_local() — collect the identifier part of a `%name` local.
// The `%` has already been consumed by next_token().
// On error (e.g. `%123`), sets error_msg_ and returns false.
bool Lexer::scan_local(int tok_line, int tok_col, Token& out) {
    char c = peek();

    // The first character after `%` must be a letter or underscore.
    if (!((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || c == '_')) {
        std::ostringstream oss;
        oss << "Lexical error at " << tok_line << ":" << tok_col
            << " — expected identifier after `%`, but found `"
            << (c == '\0' ? "EOF" : std::string(1, c)) << "`";
        error_msg_ = oss.str();
        return false;
    }

    std::string text = "%";  // keep % in text so the parser sees the full form
    text += advance();       // consume the first letter

    while (true) {
        char nc = peek();
        if ((nc >= 'A' && nc <= 'Z') || (nc >= 'a' && nc <= 'z') ||
            (nc >= '0' && nc <= '9') || nc == '_') {
            text += advance();
        } else {
            break;
        }
    }

    out = Token(TokenKind::LOCAL, text, tok_line, tok_col);
    return true;
}

// scan_number() — collect an integer or float literal.
// `first` is the first digit, already consumed.
Token Lexer::scan_number(char first, int tok_line, int tok_col) {
    std::string text;
    text += first;
    bool is_float = false;

    while (true) {
        char c = peek();
        if (c >= '0' && c <= '9') {
            text += advance();
        } else if (c == '.' && !is_float && peek_next() >= '0' && peek_next() <= '9') {
            // Decimal point followed by a digit → start of fractional part.
            // We check peek_next() to avoid consuming `.` in something like `0.`
            // at end of file or `0.)`.
            text += advance(); // consume '.'
            is_float = true;
        } else {
            break;
        }
    }

    Token t;
    t.line = tok_line;
    t.col  = tok_col;
    t.text = text;
    if (is_float) {
        t.kind      = TokenKind::FLOAT_LIT;
        t.float_val = std::stod(text);  // std::stod: string to double
    } else {
        t.kind    = TokenKind::INT_LIT;
        t.int_val = std::stoll(text);   // std::stoll: string to long long
    }
    return t;
}

// =============================================================================
// Section 6 – Main token scanner
// =============================================================================
//
// next_token() produces exactly ONE token.  It:
//   1. Skips whitespace and comments.
//   2. Classifies the next character with classify().
//   3. Looks up the initial state in TRANSITION_[START][class].
//   4. Dispatches to the appropriate scanner or handles single-char tokens.

bool Lexer::next_token(Token& out) {
    skip_whitespace_and_comments();

    // Record position of this token's first character BEFORE advancing.
    int tok_line = line_;
    int tok_col  = col_;

    // End of file?
    if (peek() == '\0') {
        out = Token(TokenKind::END_OF_FILE, "", tok_line, tok_col);
        return true;
    }

    // Consume the first character and classify it.
    char c = advance();
    InputClass cls = classify(c);

    // Look up the state we enter after seeing this first character.
    State initial = TRANSITION_[static_cast<int>(State::START)][static_cast<int>(cls)];

    switch (initial) {

        // ----------------------------------------------------------------
        // Identifiers / keywords / labels
        // ----------------------------------------------------------------
        case State::IN_IDENT:
            out = scan_ident(c, tok_line, tok_col);
            return true;

        // ----------------------------------------------------------------
        // Locals: %name
        // ----------------------------------------------------------------
        case State::IN_LOCAL:
            // `c` was `%`; scan_local reads the ident part.
            return scan_local(tok_line, tok_col, out);

        // ----------------------------------------------------------------
        // Integer / float literals
        // ----------------------------------------------------------------
        case State::IN_INT_LIT:
            out = scan_number(c, tok_line, tok_col);
            return true;

        // ----------------------------------------------------------------
        // `->` (Arrow) or error
        // ----------------------------------------------------------------
        case State::IN_MINUS: {
            // We saw `-`. The only valid next character is `>`.
            if (peek() == '>') {
                advance(); // consume `>`
                out = Token(TokenKind::ARROW, "->", tok_line, tok_col);
                return true;
            }
            std::ostringstream oss;
            oss << "Lexical error at " << tok_line << ":" << tok_col
                << " — unexpected `-` (ResIR has no standalone minus;"
                   " did you mean `->`?)";
            error_msg_ = oss.str();
            return false;
        }

        // ----------------------------------------------------------------
        // `::` (ColonColon) or `:` (Colon)
        // ----------------------------------------------------------------
        case State::IN_COLON:
            if (peek() == ':') {
                advance(); // consume second `:`
                out = Token(TokenKind::COLON_COLON, "::", tok_line, tok_col);
            } else {
                out = Token(TokenKind::COLON, ":", tok_line, tok_col);
            }
            return true;

        // ----------------------------------------------------------------
        // `/` — must be followed by `/` to form a comment; otherwise error.
        // If we get here the skip_whitespace_and_comments() somehow missed it
        // (shouldn't happen in practice), so we handle it gracefully.
        // ----------------------------------------------------------------
        case State::IN_SLASH:
            if (peek() == '/') {
                // It's a line comment. Skip to end of line, then recurse.
                while (peek() != '\n' && peek() != '\0') advance();
                return next_token(out); // tail recursion: get the real next token
            }
            {
                std::ostringstream oss;
                oss << "Lexical error at " << tok_line << ":" << tok_col
                    << " — lone `/`; did you mean `//` (comment)?";
                error_msg_ = oss.str();
                return false;
            }

        // ----------------------------------------------------------------
        // Single-character punctuation
        // ----------------------------------------------------------------
        case State::DONE: {
            TokenKind kind;
            switch (c) {
                case '{': kind = TokenKind::LBRACE;    break;
                case '}': kind = TokenKind::RBRACE;    break;
                case '(': kind = TokenKind::LPAREN;    break;
                case ')': kind = TokenKind::RPAREN;    break;
                case ';': kind = TokenKind::SEMICOLON; break;
                case ',': kind = TokenKind::COMMA;     break;
                case '<': kind = TokenKind::LT;        break;
                case '>': kind = TokenKind::GT;        break;
                case '=': kind = TokenKind::EQ;        break;
                default: {
                    std::ostringstream oss;
                    oss << "Lexical error at " << tok_line << ":" << tok_col
                        << " — unhandled punctuation `" << c << "`";
                    error_msg_ = oss.str();
                    return false;
                }
            }
            out = Token(kind, std::string(1, c), tok_line, tok_col);
            return true;
        }

        // ----------------------------------------------------------------
        // IN_LINE_COMMENT can only reach here if skip_whitespace missed it.
        // Skip to newline and recurse.
        // ----------------------------------------------------------------
        case State::IN_LINE_COMMENT:
            while (peek() != '\n' && peek() != '\0') advance();
            return next_token(out);

        // ----------------------------------------------------------------
        // Error — unrecognised character
        // ----------------------------------------------------------------
        case State::ERR:
        default: {
            std::ostringstream oss;
            oss << "Lexical error at " << tok_line << ":" << tok_col
                << " — unexpected character `" << c
                << "` (0x" << std::hex << static_cast<int>((unsigned char)c) << ")";
            error_msg_ = oss.str();
            return false;
        }
    }
}

// =============================================================================
// Section 7 – tokenise(): scan the entire source
// =============================================================================

bool Lexer::tokenise(std::vector<Token>& out) {
    out.clear();

    while (true) {
        Token t;
        if (!next_token(t)) {
            // next_token already set error_msg_
            return false;
        }
        out.push_back(t);
        if (t.kind == TokenKind::END_OF_FILE) break;
    }

    return true;
}
