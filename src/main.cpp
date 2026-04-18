// =============================================================================
// main.cpp  —  Entry point for the ResIR compiler
// =============================================================================
//
// What this file does:
//   1. Reads a .resir source file given as a command-line argument.
//   2. Passes the source text to the Lexer.
//   3. Prints every token to stdout (useful for debugging Task 1).
//   4. Reports any lexical errors and exits with a non-zero code.
//
// How to compile and run (see also the Makefile):
//   make
//   ./resir tests/basic_math.resir
// =============================================================================

#include "lexer.hpp"   // The Lexer class
#include "token.hpp"   // TokenKind, Token, token_kind_name()

#include <iostream>    // std::cout, std::cerr
#include <fstream>     // std::ifstream — for reading files
#include <sstream>     // std::ostringstream — for reading a whole file at once
#include <vector>      // std::vector

// ---------------------------------------------------------------------------
// read_file() — load an entire file into a std::string
// ---------------------------------------------------------------------------
// std::ifstream opens a file for reading.
// std::istreambuf_iterator reads character-by-character into a string.
// Returns an empty string if the file could not be opened.
static std::string read_file(const std::string& path) {
    std::ifstream file(path);           // open the file
    if (!file.is_open()) return "";     // return empty string on failure

    // Construct a string from the full contents of `file`.
    // The trick: istreambuf_iterator<char>(file) reads raw bytes;
    //            the second argument is the "end" sentinel.
    return std::string(
        std::istreambuf_iterator<char>(file),
        std::istreambuf_iterator<char>()
    );
}

// ---------------------------------------------------------------------------
// print_token() — display one token in a human-readable format
// ---------------------------------------------------------------------------
static void print_token(const Token& t) {
    // Print: LINE:COL  KIND  "text"
    std::cout << t.line << ":" << t.col
              << "\t" << token_kind_name(t.kind)
              << "\t\"" << t.text << "\"";

    // For literals, also print the parsed value.
    if (t.kind == TokenKind::INT_LIT) {
        std::cout << "\t(value=" << t.int_val << ")";
    } else if (t.kind == TokenKind::FLOAT_LIT) {
        std::cout << "\t(value=" << t.float_val << ")";
    }
    std::cout << "\n";
}

// ---------------------------------------------------------------------------
// main()
// ---------------------------------------------------------------------------
int main(int argc, char* argv[]) {
    // argv[0] = program name, argv[1] = first argument (the source file).
    if (argc < 2) {
        std::cerr << "Usage: resir <source.resir>\n";
        return 1; // non-zero = error
    }

    std::string path = argv[1];
    std::string source = read_file(path);

    if (source.empty()) {
        // Two possible causes: file not found, or genuinely empty file.
        // We check if the file exists by trying to open it again.
        std::ifstream check(path);
        if (!check.is_open()) {
            std::cerr << "Error: cannot open file `" << path << "`\n";
            return 1;
        }
        // File exists but is empty — that's fine, just no tokens.
        std::cout << "File is empty.\n";
        return 0;
    }

    // Run the lexer.
    Lexer lexer(source);
    std::vector<Token> tokens;

    if (!lexer.tokenise(tokens)) {
        // tokenise() returns false on the first lexical error.
        std::cerr << lexer.get_error() << "\n";
        return 1;
    }

    // Print all tokens (for Task 1 testing).
    std::cout << "Tokens for: " << path << "\n";
    std::cout << "LINE:COL\tKIND\t\t\t\tTEXT\n";
    std::cout << std::string(60, '-') << "\n";

    for (const Token& t : tokens) {
        print_token(t);
    }

    std::cout << "\nTotal tokens: " << tokens.size() << "\n";
    return 0;
}
