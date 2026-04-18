// =============================================================================
// main.cpp  —  Entry point for the ResIR compiler (Tasks 1 + 2)
// =============================================================================
//
// Pipeline so far:
//   1. Read the .resir source file from the command line.
//   2. Run the LEXER  → produces a flat list of Tokens.
//   3. Run the PARSER → consumes the Tokens and builds a Program (CFG).
//   4. Pretty-print the Program to stdout so you can inspect the parse result.
//
// Usage:
//   ./resir tests/basic_math.resir
//   ./resir --tokens tests/basic_math.resir   (only print tokens, skip parse)
// =============================================================================

#include "lexer.hpp"    // Lexer class
#include "token.hpp"    // TokenKind, Token, token_kind_name()
#include "parser.hpp"   // Parser class
#include "cfg.hpp"      // Program, print_program()

#include <iostream>     // std::cout, std::cerr
#include <fstream>      // std::ifstream
#include <vector>
#include <string>

// ---------------------------------------------------------------------------
// read_file() — load an entire file into a std::string
// ---------------------------------------------------------------------------
static std::string read_file(const std::string& path) {
    std::ifstream file(path);
    if (!file.is_open()) return "";
    return std::string(
        std::istreambuf_iterator<char>(file),
        std::istreambuf_iterator<char>()
    );
}

// ---------------------------------------------------------------------------
// print_token() — single-token display (used by --tokens mode)
// ---------------------------------------------------------------------------
static void print_token(const Token& t) {
    std::cout << t.line << ":" << t.col
              << "\t" << token_kind_name(t.kind)
              << "\t\"" << t.text << "\"";
    if (t.kind == TokenKind::INT_LIT)
        std::cout << "\t(value=" << t.int_val << ")";
    else if (t.kind == TokenKind::FLOAT_LIT)
        std::cout << "\t(value=" << t.float_val << ")";
    std::cout << "\n";
}

// ---------------------------------------------------------------------------
// main()
// ---------------------------------------------------------------------------
int main(int argc, char* argv[]) {
    if (argc < 2) {
        std::cerr << "Usage: resir [--tokens] <source.resir>\n";
        std::cerr << "  --tokens   print token stream only (skip parse)\n";
        return 1;
    }

    // Parse optional flags
    bool tokens_only = false;
    std::string path;
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--tokens") tokens_only = true;
        else                   path = arg;
    }

    if (path.empty()) {
        std::cerr << "Error: no source file given\n";
        return 1;
    }

    // -----------------------------------------------------------------------
    // Step 1: read the source file
    // -----------------------------------------------------------------------
    std::string source = read_file(path);
    if (source.empty()) {
        std::ifstream check(path);
        if (!check.is_open()) {
            std::cerr << "Error: cannot open file `" << path << "`\n";
            return 1;
        }
        std::cout << "File is empty.\n";
        return 0;
    }

    // -----------------------------------------------------------------------
    // Step 2: run the lexer
    // -----------------------------------------------------------------------
    Lexer lexer(source);
    std::vector<Token> tokens;
    if (!lexer.tokenise(tokens)) {
        std::cerr << lexer.get_error() << "\n";
        return 1;
    }

    // --tokens mode: just print tokens and exit
    if (tokens_only) {
        std::cout << "Tokens for: " << path << "\n";
        std::cout << "LINE:COL\tKIND\t\t\t\tTEXT\n";
        std::cout << std::string(60, '-') << "\n";
        for (const Token& t : tokens) print_token(t);
        std::cout << "\nTotal tokens: " << tokens.size() << "\n";
        return 0;
    }

    // -----------------------------------------------------------------------
    // Step 3: run the parser
    // -----------------------------------------------------------------------
    // We give the Parser the same token list the Lexer produced.
    // The Parser consumes that list and builds a Program struct in memory.
    Parser parser(tokens);
    Program prog;

    if (!parser.parse(prog)) {
        std::cerr << parser.get_error() << "\n";
        return 1;
    }

    // -----------------------------------------------------------------------
    // Step 4: pretty-print the parsed CFG
    // -----------------------------------------------------------------------
    std::cout << "// Parsed CFG for: " << path << "\n\n";
    print_program(prog);

    return 0;
}
