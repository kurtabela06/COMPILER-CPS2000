# =============================================================================
# Makefile — Build the ResIR compiler (Tasks 1 + 2)
# =============================================================================
#
# Usage:
#   make                  — build the main compiler binary  (./resir)
#   make test             — build and run ALL unit tests (lexer + parser)
#   make test_lexer       — build and run only the lexer tests
#   make test_parser      — build and run only the parser tests
#   make run FILE=tests/basic_math.resir   — parse a .resir file and print CFG
#   make tokens FILE=tests/basic_math.resir — print only the token stream
#   make clean            — remove all built files
#
# Compiler flags:
#   -std=c++17  enables C++17 features (std::make_unique, structured bindings…)
#   -Wall       all standard warnings
#   -Wextra     extra warnings
#   -Iinclude   look for header files in the include/ directory
# =============================================================================

CXX      = g++
CXXFLAGS = -std=c++17 -Wall -Wextra -Iinclude

# ---------------------------------------------------------------------------
# Shared source files (used by both the main binary and the tests)
# ---------------------------------------------------------------------------
SHARED_SRCS = src/lexer.cpp src/token.cpp src/parser.cpp src/cfg.cpp

# Main compiler binary sources
MAIN_SRCS = src/main.cpp $(SHARED_SRCS)

# Lexer-only test sources
LEXER_TEST_SRCS = src/test_lexer.cpp src/lexer.cpp src/token.cpp

# Parser test sources (needs the full pipeline)
PARSER_TEST_SRCS = src/test_parser.cpp $(SHARED_SRCS)

# Output binary names
TARGET             = resir
LEXER_TEST_TARGET  = resir_lexer_test
PARSER_TEST_TARGET = resir_parser_test

# ---------------------------------------------------------------------------
# Default target: build the main compiler
# ---------------------------------------------------------------------------
$(TARGET): $(MAIN_SRCS)
	$(CXX) $(CXXFLAGS) -o $@ $^

# ---------------------------------------------------------------------------
# Run ALL tests
# ---------------------------------------------------------------------------
test: $(LEXER_TEST_TARGET) $(PARSER_TEST_TARGET)
	@echo "\n--- Lexer tests ---"
	./$(LEXER_TEST_TARGET)
	@echo "\n--- Parser tests ---"
	./$(PARSER_TEST_TARGET)

# ---------------------------------------------------------------------------
# Lexer tests only
# ---------------------------------------------------------------------------
test_lexer: $(LEXER_TEST_TARGET)
	./$(LEXER_TEST_TARGET)

$(LEXER_TEST_TARGET): $(LEXER_TEST_SRCS)
	$(CXX) $(CXXFLAGS) -o $@ $^

# ---------------------------------------------------------------------------
# Parser tests only
# ---------------------------------------------------------------------------
test_parser: $(PARSER_TEST_TARGET)
	./$(PARSER_TEST_TARGET)

$(PARSER_TEST_TARGET): $(PARSER_TEST_SRCS)
	$(CXX) $(CXXFLAGS) -o $@ $^

# ---------------------------------------------------------------------------
# run: parse a .resir file and print the CFG
# Usage: make run FILE=tests/basic_math.resir
# ---------------------------------------------------------------------------
run: $(TARGET)
	./$(TARGET) $(FILE)

# ---------------------------------------------------------------------------
# tokens: print only the token stream (skip parsing)
# Usage: make tokens FILE=tests/basic_math.resir
# ---------------------------------------------------------------------------
tokens: $(TARGET)
	./$(TARGET) --tokens $(FILE)

# ---------------------------------------------------------------------------
# clean: remove all built binaries
# ---------------------------------------------------------------------------
clean:
	rm -f $(TARGET) $(LEXER_TEST_TARGET) $(PARSER_TEST_TARGET)

.PHONY: test test_lexer test_parser run tokens clean
