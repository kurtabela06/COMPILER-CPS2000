// =============================================================================
// lexer.hpp  —  Public interface of the table-driven ResIR lexer
// =============================================================================
//
// How to use the lexer:
//   1. Create a Lexer with the source text.
//   2. Call tokenise() to get back all tokens at once.
//   3. If tokenise() returns false, call get_error() to see what went wrong.
//
// Example:
//   Lexer lex(source_string);
//   std::vector<Token> tokens;
//   if (!lex.tokenise(tokens)) {
//       std::cerr << lex.get_error() << "\n";
//   }
// =============================================================================

#pragma once
#include "token.hpp"
#include <vector>
#include <string>

// ---------------------------------------------------------------------------
// Lexer — the scanner class
// ---------------------------------------------------------------------------
class Lexer {
public:
    // Constructor: give it the entire source file as a string.
    // We store a copy so the Lexer owns its input.
    explicit Lexer(std::string source);

    // Tokenise the full source.
    //   - Fills `out` with every token (including a final END_OF_FILE).
    //   - Returns true on success, false if a lexical error was encountered.
    //   - On failure, call get_error() to retrieve the error message.
    bool tokenise(std::vector<Token>& out);

    // Return the last error message (only meaningful after tokenise()==false).
    const std::string& get_error() const { return error_msg_; }

public:
    // -----------------------------------------------------------------------
    // DFA states
    // -----------------------------------------------------------------------
    // Each state represents "what are we in the middle of reading right now?"
    // The table TRANSITION[state][class] tells us where to go next.
    // These are public so lexer.cpp can reference them as Lexer::State::START etc.
    enum class State {
        START,           // Between tokens; waiting for the first character
        IN_IDENT,        // Inside an identifier or keyword
        IN_LOCAL,        // After `%`; reading the ident part of a local
        IN_INT_LIT,      // Reading decimal digits of an integer literal
        IN_FLOAT_FRAC,   // Reading digits after the `.` in a float
        IN_MINUS,        // Just saw `-`; waiting for `>` to make `->`
        IN_COLON,        // Just saw `:`; waiting for `:` to make `::`
        IN_SLASH,        // Just saw `/`; waiting for `/` to make `//` comment
        IN_LINE_COMMENT, // Inside a `//` comment; skip until newline
        DONE,            // Accepting state — token is complete
        ERR,             // Error state — unrecognised character
        NUM_STATES       // Sentinel: total number of states (used to size the array)
    };

    // -----------------------------------------------------------------------
    // Input classes
    // -----------------------------------------------------------------------
    // Instead of branching on all 128 ASCII values, we collapse characters
    // into a small set of classes.  The table has one column per class.
    // Public for the same reason as State above.
    enum class InputClass {
        ALPHA  = 0,   // A-Z, a-z, _
        DIGIT  = 1,   // 0-9
        PERCENT= 2,   // %
        DOT    = 3,   // .
        MINUS  = 4,   // -
        GT     = 5,   // >
        COLON  = 6,   // :
        SLASH  = 7,   // /
        PUNCT  = 8,   // { } ( ) ; , < = (each complete in one char)
        SPACE  = 9,   // space, tab, carriage-return
        NEWLINE= 10,  // \n
        OTHER  = 11,  // anything unrecognised
        NUM_CLASSES   // Sentinel
    };

    // The 2-D transition table: TRANSITION_[state][class] = next_state
    // Defined as a static member so it's shared across all Lexer instances.
    static const State TRANSITION_
        [static_cast<int>(State::NUM_STATES)]
        [static_cast<int>(InputClass::NUM_CLASSES)];

private:
    // -----------------------------------------------------------------------
    // Instance data
    // -----------------------------------------------------------------------
    std::string src_;    // The full source text
    size_t      pos_;    // Current read position (index into src_)
    int         line_;   // Current 1-based line number
    int         col_;    // Current 1-based column number
    std::string error_msg_; // Filled in on error

    // -----------------------------------------------------------------------
    // Helper methods
    // -----------------------------------------------------------------------

    // Peek at the current character without consuming it.
    // Returns '\0' at end-of-file.
    char peek() const;

    // Peek one position ahead (for two-character tokens like `//`, `->`, `::`).
    char peek_next() const;

    // Consume the current character, advance pos_, and update line_/col_.
    char advance();

    // Skip whitespace and line comments.
    void skip_whitespace_and_comments();

    // Classify a character into an InputClass (the column index).
    static InputClass classify(char c);

    // Scan the rest of an identifier (or keyword / label / path).
    // `first` is the character already consumed that started this token.
    Token scan_ident(char first, int tok_line, int tok_col);

    // Scan the ident portion of a `%name` local.
    // Returns false and sets error_msg_ on failure.
    bool scan_local(int tok_line, int tok_col, Token& out);

    // Scan a numeric literal (integer or float).
    Token scan_number(char first, int tok_line, int tok_col);

    // Build the next single token (called repeatedly by tokenise()).
    // Returns false on lexical error.
    bool next_token(Token& out);

    // Helper: check whether a string matches the label pattern bb[0-9]+
    static bool is_label(const std::string& s);

    // Helper: look up whether a string is a reserved keyword.
    // Returns TokenKind::IDENT if it is not a keyword.
    static TokenKind keyword_lookup(const std::string& s);
};
