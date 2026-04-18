// =============================================================================
// main.cpp  —  Entry point for the ResIR compiler (Tasks 1 + 2 + 3)
// =============================================================================
//
// Pipeline:
//   1. Read the .resir source file.
//   2. LEXER     → flat list of Tokens.
//   3. PARSER    → in-memory Program (CFG).
//   4. VALIDATOR → semantic checks + type checking.
//   5. Pretty-print the validated CFG.
//
// Usage:
//   ./resir tests/basic_math.resir
//   ./resir --tokens tests/basic_math.resir   (token dump only)
//   ./resir --parse  tests/basic_math.resir   (parse + print CFG, skip validate)
// =============================================================================

#include "lexer.hpp"
#include "token.hpp"
#include "parser.hpp"
#include "cfg.hpp"
#include "validator.hpp"

#include <iostream>
#include <fstream>
#include <vector>
#include <string>

static std::string read_file(const std::string& path) {
    std::ifstream file(path);
    if (!file.is_open()) return "";
    return std::string(std::istreambuf_iterator<char>(file),
                       std::istreambuf_iterator<char>());
}

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

int main(int argc, char* argv[]) {
    if (argc < 2) {
        std::cerr << "Usage: resir [--tokens|--parse] <source.resir>\n";
        std::cerr << "  --tokens  print token stream only\n";
        std::cerr << "  --parse   print parsed CFG, skip validation\n";
        return 1;
    }

    bool tokens_only = false;
    bool parse_only  = false;
    std::string path;
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if      (arg == "--tokens") tokens_only = true;
        else if (arg == "--parse")  parse_only  = true;
        else                        path        = arg;
    }
    if (path.empty()) {
        std::cerr << "Error: no source file given\n";
        return 1;
    }

    // --- Step 1: read ---
    std::string source = read_file(path);
    if (source.empty()) {
        std::ifstream chk(path);
        if (!chk.is_open()) {
            std::cerr << "Error: cannot open `" << path << "`\n";
            return 1;
        }
        std::cout << "File is empty.\n";
        return 0;
    }

    // --- Step 2: lex ---
    Lexer lexer(source);
    std::vector<Token> tokens;
    if (!lexer.tokenise(tokens)) {
        std::cerr << lexer.get_error() << "\n";
        return 1;
    }
    if (tokens_only) {
        std::cout << "Tokens for: " << path << "\n";
        std::cout << std::string(60, '-') << "\n";
        for (const Token& t : tokens) print_token(t);
        std::cout << "\nTotal: " << tokens.size() << " tokens\n";
        return 0;
    }

    // --- Step 3: parse ---
    Parser parser(tokens);
    Program prog;
    if (!parser.parse(prog)) {
        std::cerr << parser.get_error() << "\n";
        return 1;
    }
    if (parse_only) {
        std::cout << "// Parsed CFG for: " << path << "\n\n";
        print_program(prog);
        return 0;
    }

    // --- Step 4: validate ---
    // The Validator walks the CFG produced by the Parser and checks:
    //   - All used locals are declared.
    //   - Jump/cjump targets exist.
    //   - Block labels are unique.
    //   - Types are consistent (operand types, return type, cjump bool, …).
    //   - member_ptr field and struct rules.
    Validator validator(prog);
    if (!validator.validate()) {
        std::cerr << validator.get_error() << "\n";
        return 1;
    }

    // --- Step 5: print validated CFG ---
    std::cout << "// [OK] Validated CFG for: " << path << "\n\n";
    print_program(prog);

    return 0;
}
