// =============================================================================
// main.cpp  —  Entry point for the ResIR compiler (Tasks 1 + 2 + 3 + 4 + 5)
// =============================================================================
//
// Pipeline:
//   1. Read the .resir source file.
//   2. LEXER     → flat list of Tokens.
//   3. PARSER    → in-memory Program (CFG).
//   4. VALIDATOR → semantic checks + type checking.
//   5a. CODEGEN  → emit a .c file ready for gcc.
//   5b. ANALYSIS → extract Datalog facts for live variable analysis.
//
// Usage:
//   ./resir tests/basic_math.resir
//   ./resir --tokens  tests/basic_math.resir   (token dump only)
//   ./resir --parse   tests/basic_math.resir   (parse + print CFG, skip later)
//   ./resir --codegen tests/basic_math.resir   (validate + print generated C)
//   ./resir -o out.c  tests/basic_math.resir   (validate + write C to out.c)
//   ./resir --analyze tests/basic_math.resir   (validate + write Datalog facts)
//   ./resir --analyze -f mydir/ tests/basic_math.resir  (custom fact output dir)
// =============================================================================

#include "lexer.hpp"
#include "token.hpp"
#include "parser.hpp"
#include "cfg.hpp"
#include "validator.hpp"
#include "codegen.hpp"
#include "analysis.hpp"   // Task 5: fact extractor

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
        std::cerr << "Usage: resir [options] <source.resir>\n";
        std::cerr << "  --tokens       print token stream only\n";
        std::cerr << "  --parse        print parsed CFG, skip validation\n";
        std::cerr << "  --codegen      validate then print generated C to stdout\n";
        std::cerr << "  -o out.c       validate then write generated C to out.c\n";
        std::cerr << "  --analyze      validate then write Datalog facts to facts/\n";
        std::cerr << "  -f <dir>       fact output directory (default: facts/)\n";
        return 1;
    }

    bool        tokens_only  = false;
    bool        parse_only   = false;
    bool        codegen_only = false;
    bool        analyze_mode = false;  // --analyze: write Datalog fact files
    std::string output_file;           // -o <file>
    std::string facts_dir    = "facts/"; // -f <dir>: where to write .facts files
    std::string path;

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if      (arg == "--tokens")  tokens_only  = true;
        else if (arg == "--parse")   parse_only   = true;
        else if (arg == "--codegen") codegen_only = true;
        else if (arg == "--analyze") analyze_mode = true;
        else if (arg == "-o" && i + 1 < argc) output_file = argv[++i];
        else if (arg == "-f" && i + 1 < argc) facts_dir   = argv[++i];
        else                         path = arg;
    }
    if (path.empty()) {
        std::cerr << "Error: no source file given\n";
        return 1;
    }
    // Ensure facts_dir ends with a slash so we can append filenames directly.
    if (!facts_dir.empty() && facts_dir.back() != '/') facts_dir += '/';

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
    Validator validator(prog);
    if (!validator.validate()) {
        std::cerr << validator.get_error() << "\n";
        return 1;
    }

    // --- Step 5a: code generation ---
    CodeGen gen(prog);

    if (codegen_only) {
        std::cout << "// [OK] Generated C for: " << path << "\n\n";
        std::cout << gen.emit();
        return 0;
    }

    if (!output_file.empty()) {
        if (!gen.emit_to_file(output_file)) {
            std::cerr << "Error: cannot write to `" << output_file << "`\n";
            return 1;
        }
        std::cout << "// [OK] Validated + generated C written to: " << output_file << "\n";
        return 0;
    }

    // --- Step 5b: Datalog fact extraction ---
    // When --analyze is passed, we skip C generation and instead write the
    // three Datalog fact files that analysis.dl needs.
    if (analyze_mode) {
        FactExtractor fx(prog);
        if (!fx.extract(facts_dir)) {
            std::cerr << "Error writing facts: " << fx.get_error() << "\n";
            return 1;
        }
        std::cout << "// [OK] Fact files written to: " << facts_dir << "\n";
        std::cout << "//   " << facts_dir << "edge.facts\n";
        std::cout << "//   " << facts_dir << "def.facts\n";
        std::cout << "//   " << facts_dir << "use.facts\n";
        std::cout << "//\n";
        std::cout << "// Run Soufflé:\n";
        std::cout << "//   souffle -F " << facts_dir
                  << " -D output/ datalog/analysis.dl\n";
        return 0;
    }

    // Default: validate + print CFG
    std::cout << "// [OK] Validated CFG for: " << path << "\n\n";
    print_program(prog);

    return 0;
}
