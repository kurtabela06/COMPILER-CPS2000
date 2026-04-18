# =============================================================================
# Makefile — Build the ResIR compiler
# =============================================================================
#
# Usage:
#   make          — build the main compiler binary  (./resir)
#   make test     — build and run the unit-test binary (./resir_test)
#   make clean    — remove all built files
#   make run FILE=tests/basic_math.resir  — compile and run on a test file
#
# Internals:
#   CXX    — the C++ compiler (g++ by default)
#   CXXFLAGS — compiler flags:
#     -std=c++17   use C++17 features (std::string_view, structured bindings…)
#     -Wall        enable all standard warnings
#     -Wextra      enable extra warnings
#     -Iinclude    tell the compiler to look in `include/` for header files
# =============================================================================

CXX      = g++
CXXFLAGS = -std=c++17 -Wall -Wextra -Iinclude

# Source files for the main binary (everything EXCEPT the test file)
MAIN_SRCS = src/main.cpp src/lexer.cpp src/token.cpp

# Source files for the test binary
TEST_SRCS = src/test_lexer.cpp src/lexer.cpp src/token.cpp

# Output binary names
TARGET      = resir
TEST_TARGET = resir_test

# ---------------------------------------------------------------------------
# Default target: build the main compiler
# ---------------------------------------------------------------------------
# $@ = the target name (resir)
# $^ = all prerequisites (the source files)
$(TARGET): $(MAIN_SRCS)
	$(CXX) $(CXXFLAGS) -o $@ $^

# ---------------------------------------------------------------------------
# test target: build and immediately run the unit tests
# ---------------------------------------------------------------------------
test: $(TEST_TARGET)
	./$(TEST_TARGET)

$(TEST_TARGET): $(TEST_SRCS)
	$(CXX) $(CXXFLAGS) -o $@ $^

# ---------------------------------------------------------------------------
# run target: compile then lex a specific file
# Usage: make run FILE=tests/basic_math.resir
# ---------------------------------------------------------------------------
run: $(TARGET)
	./$(TARGET) $(FILE)

# ---------------------------------------------------------------------------
# clean: remove built binaries
# ---------------------------------------------------------------------------
clean:
	rm -f $(TARGET) $(TEST_TARGET)

# `.PHONY` tells make these are not file names (prevents conflicts with
# files named "test", "clean", or "run").
.PHONY: test clean run
