# =============================================================================
# Makefile — Build the ResIR compiler (Tasks 1 + 2 + 3 + 4 + 5)
# =============================================================================
#
# Usage:
#   make                          build ./resir
#   make test                     run ALL unit tests (all 5 suites)
#   make test_lexer               run only lexer tests
#   make test_parser              run only parser tests
#   make test_validator           run only validator tests
#   make test_codegen             run only codegen tests
#   make test_analysis            run only fact-extractor tests
#   make run FILE=<f>             validate + print CFG for FILE
#   make tokens FILE=<f>          print token stream only
#   make parse FILE=<f>           parse + print CFG (skip validate)
#   make codegen FILE=<f>         validate + print generated C to stdout
#   make emit FILE=<f> OUT=<c>    validate + write generated C to OUT
#   make analyze FILE=<f>         validate + write Datalog facts to datalog/facts/
#   make souffle FILE=<f>         analyze + run souffle (requires souffle installed)
#   make clean                    remove all built binaries and generated files
#
# Compiler flags:
#   -std=c++17   C++17 features (make_unique, if-init, structured bindings…)
#   -Wall        all standard warnings
#   -Wextra      extra warnings
#   -Iinclude    find headers in include/
# =============================================================================

CXX      = g++
CXXFLAGS = -std=c++17 -Wall -Wextra -Iinclude

# Default facts directory (can be overridden: make analyze FILE=x FACTS_DIR=my/)
FACTS_DIR = datalog/facts/
# Default Soufflé output directory
SOUFFLE_OUT = output/
# Soufflé Datalog script
DL_SCRIPT = datalog/analysis.dl

# ---------------------------------------------------------------------------
# Source groups
# ---------------------------------------------------------------------------
# SHARED: compiled into every binary that needs the full pipeline
SHARED_SRCS = src/lexer.cpp src/token.cpp \
              src/parser.cpp src/cfg.cpp \
              src/validator.cpp src/codegen.cpp src/analysis.cpp

MAIN_SRCS             = src/main.cpp             $(SHARED_SRCS)
LEXER_TEST_SRCS       = src/test_lexer.cpp       src/lexer.cpp src/token.cpp
PARSER_TEST_SRCS      = src/test_parser.cpp      src/lexer.cpp src/token.cpp \
                        src/parser.cpp src/cfg.cpp
VALIDATOR_TEST_SRCS   = src/test_validator.cpp   src/lexer.cpp src/token.cpp \
                        src/parser.cpp src/cfg.cpp src/validator.cpp
CODEGEN_TEST_SRCS     = src/test_codegen.cpp     $(SHARED_SRCS)
ANALYSIS_TEST_SRCS    = src/test_analysis.cpp    $(SHARED_SRCS)

# ---------------------------------------------------------------------------
# Binary names
# ---------------------------------------------------------------------------
TARGET                  = resir
LEXER_TEST_TARGET       = resir_lexer_test
PARSER_TEST_TARGET      = resir_parser_test
VALIDATOR_TEST_TARGET   = resir_validator_test
CODEGEN_TEST_TARGET     = resir_codegen_test
ANALYSIS_TEST_TARGET    = resir_analysis_test

# ---------------------------------------------------------------------------
# Default: build the main compiler
# ---------------------------------------------------------------------------
$(TARGET): $(MAIN_SRCS)
	$(CXX) $(CXXFLAGS) -o $@ $^

# ---------------------------------------------------------------------------
# All tests
# ---------------------------------------------------------------------------
test: $(LEXER_TEST_TARGET) $(PARSER_TEST_TARGET) $(VALIDATOR_TEST_TARGET) \
      $(CODEGEN_TEST_TARGET) $(ANALYSIS_TEST_TARGET)
	@echo "\n--- Lexer tests ---"
	./$(LEXER_TEST_TARGET)
	@echo "\n--- Parser tests ---"
	./$(PARSER_TEST_TARGET)
	@echo "\n--- Validator tests ---"
	./$(VALIDATOR_TEST_TARGET)
	@echo "\n--- Codegen tests ---"
	./$(CODEGEN_TEST_TARGET)
	@echo "\n--- Analysis (fact extractor) tests ---"
	./$(ANALYSIS_TEST_TARGET)

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

test_codegen: $(CODEGEN_TEST_TARGET)
	./$(CODEGEN_TEST_TARGET)

$(CODEGEN_TEST_TARGET): $(CODEGEN_TEST_SRCS)
	$(CXX) $(CXXFLAGS) -o $@ $^

test_analysis: $(ANALYSIS_TEST_TARGET)
	./$(ANALYSIS_TEST_TARGET)

$(ANALYSIS_TEST_TARGET): $(ANALYSIS_TEST_SRCS)
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

codegen: $(TARGET)
	./$(TARGET) --codegen $(FILE)

emit: $(TARGET)
	./$(TARGET) -o $(OUT) $(FILE)

# analyze: run the compiler in --analyze mode to generate fact files
analyze: $(TARGET)
	./$(TARGET) --analyze -f $(FACTS_DIR) $(FILE)

# ---------------------------------------------------------------------------
# souffle target — analyze then run Soufflé
# ---------------------------------------------------------------------------
# This target:
#   1. Generates fact files (via make analyze).
#   2. Creates the output directory.
#   3. Runs Soufflé.  If souffle is not installed, prints a helpful message.
#
# Usage:
#   make souffle FILE=tests/basic_math.resir
#   make souffle FILE=tests/struct_access.resir
#
# After this runs:
#   output/live_in.csv      — variables live on entry to each block
#   output/live_out.csv     — variables live on exit from each block
#   output/dead_defs.csv    — assignments whose result is never used
# ---------------------------------------------------------------------------
souffle: analyze
	@mkdir -p $(SOUFFLE_OUT)
	@if command -v souffle >/dev/null 2>&1; then \
	    echo "Running Soufflé..."; \
	    souffle -F $(FACTS_DIR) -D $(SOUFFLE_OUT) $(DL_SCRIPT); \
	    echo ""; \
	    echo "=== Live Variable Analysis Results ==="; \
	    echo ""; \
	    echo "--- live_out.csv (variables live on exit from each block) ---"; \
	    cat $(SOUFFLE_OUT)live_out.csv 2>/dev/null || echo "(no output)"; \
	    echo ""; \
	    echo "--- dead_defs.csv (assignments whose result is never used) ---"; \
	    cat $(SOUFFLE_OUT)dead_defs.csv 2>/dev/null || echo "(no dead assignments found)"; \
	else \
	    echo ""; \
	    echo "ERROR: Soufflé is not installed."; \
	    echo ""; \
	    echo "To install on macOS (Homebrew):"; \
	    echo "   brew install souffle-lang/souffle/souffle"; \
	    echo ""; \
	    echo "The fact files have been written to $(FACTS_DIR):"; \
	    ls $(FACTS_DIR)*.facts 2>/dev/null || true; \
	    echo ""; \
	    echo "Once souffle is installed, run:"; \
	    echo "   souffle -F $(FACTS_DIR) -D $(SOUFFLE_OUT) $(DL_SCRIPT)"; \
	fi

# ---------------------------------------------------------------------------
# Clean
# ---------------------------------------------------------------------------
clean:
	rm -f $(TARGET) $(LEXER_TEST_TARGET) \
	      $(PARSER_TEST_TARGET) $(VALIDATOR_TEST_TARGET) \
	      $(CODEGEN_TEST_TARGET) $(ANALYSIS_TEST_TARGET) \
	      generated.c
	rm -f $(FACTS_DIR)*.facts facts/*.facts || true
	rm -rf output/

.PHONY: test test_lexer test_parser test_validator test_codegen test_analysis \
        run tokens parse codegen emit analyze souffle clean
