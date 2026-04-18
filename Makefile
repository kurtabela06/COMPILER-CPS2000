# =============================================================================
# Makefile — Build the ResIR compiler (Tasks 1 + 2 + 3)
# =============================================================================
#
# Usage:
#   make                      build ./resir
#   make test                 run ALL unit tests (lexer + parser + validator)
#   make test_lexer           run only lexer tests
#   make test_parser          run only parser tests
#   make test_validator       run only validator tests
#   make run FILE=<f>         validate + print CFG for FILE
#   make tokens FILE=<f>      print token stream only
#   make parse FILE=<f>       parse + print CFG (skip validate)
#   make clean                remove all built binaries
#
# Flags:
#   -std=c++17   C++17 features (make_unique, if-init, structured bindings…)
#   -Wall        all standard warnings
#   -Wextra      extra warnings
#   -Iinclude    find headers in include/
# =============================================================================

CXX      = g++
CXXFLAGS = -std=c++17 -Wall -Wextra -Iinclude

# ---------------------------------------------------------------------------
# Source groups
# ---------------------------------------------------------------------------
# SHARED: compiled into every binary that needs the full pipeline
SHARED_SRCS = src/lexer.cpp src/token.cpp \
              src/parser.cpp src/cfg.cpp \
              src/validator.cpp

MAIN_SRCS           = src/main.cpp           $(SHARED_SRCS)
LEXER_TEST_SRCS     = src/test_lexer.cpp     src/lexer.cpp src/token.cpp
PARSER_TEST_SRCS    = src/test_parser.cpp    src/lexer.cpp src/token.cpp \
                      src/parser.cpp src/cfg.cpp
VALIDATOR_TEST_SRCS = src/test_validator.cpp $(SHARED_SRCS)

# ---------------------------------------------------------------------------
# Binary names
# ---------------------------------------------------------------------------
TARGET              = resir
LEXER_TEST_TARGET   = resir_lexer_test
PARSER_TEST_TARGET  = resir_parser_test
VALIDATOR_TEST_TARGET = resir_validator_test

# ---------------------------------------------------------------------------
# Default: build the main compiler
# ---------------------------------------------------------------------------
$(TARGET): $(MAIN_SRCS)
	$(CXX) $(CXXFLAGS) -o $@ $^

# ---------------------------------------------------------------------------
# All tests
# ---------------------------------------------------------------------------
test: $(LEXER_TEST_TARGET) $(PARSER_TEST_TARGET) $(VALIDATOR_TEST_TARGET)
	@echo "\n--- Lexer tests ---"
	./$(LEXER_TEST_TARGET)
	@echo "\n--- Parser tests ---"
	./$(PARSER_TEST_TARGET)
	@echo "\n--- Validator tests ---"
	./$(VALIDATOR_TEST_TARGET)

# ---------------------------------------------------------------------------
# Individual test targets
# ---------------------------------------------------------------------------
test_lexer: $(LEXER_TEST_TARGET)
	./$(LEXER_TEST_TARGET)

$(LEXER_TEST_TARGET): $(LEXER_TEST_SRCS)
	$(CXX) $(CXXFLAGS) -o $@ $^

test_parser: $(PARSER_TEST_TARGET)
	./$(PARSER_TEST_TARGET)

$(PARSER_TEST_TARGET): $(PARSER_TEST_SRCS)
	$(CXX) $(CXXFLAGS) -o $@ $^

test_validator: $(VALIDATOR_TEST_TARGET)
	./$(VALIDATOR_TEST_TARGET)

$(VALIDATOR_TEST_TARGET): $(VALIDATOR_TEST_SRCS)
	$(CXX) $(CXXFLAGS) -o $@ $^

# ---------------------------------------------------------------------------
# Convenience run targets
# ---------------------------------------------------------------------------
run: $(TARGET)
	./$(TARGET) $(FILE)

tokens: $(TARGET)
	./$(TARGET) --tokens $(FILE)

parse: $(TARGET)
	./$(TARGET) --parse $(FILE)

# ---------------------------------------------------------------------------
# Clean
# ---------------------------------------------------------------------------
clean:
	rm -f $(TARGET) $(LEXER_TEST_TARGET) \
	      $(PARSER_TEST_TARGET) $(VALIDATOR_TEST_TARGET)

.PHONY: test test_lexer test_parser test_validator run tokens parse clean
