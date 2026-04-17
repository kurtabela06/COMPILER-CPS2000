# Complete Testing Guide for Task 1: Lexer

## Table of Contents
1. [Setup & Installation](#setup--installation)
2. [Running Unit Tests](#running-unit-tests)
3. [Running the Lexer Binary](#running-the-lexer-binary)
4. [Manual Testing](#manual-testing)
5. [Understanding Lexer Output](#understanding-lexer-output)
6. [Troubleshooting](#troubleshooting)

---

## Setup & Installation

### Prerequisites
Before you can test the lexer, you need to install Rust and Cargo. Follow these steps:

#### Option 1: Official Rust Installation (Recommended)
1. **On macOS**, open Terminal and run:
   ```bash
   curl --proto '=https' --tlsv1.2 -sSf https://sh.rustup.rs | sh
   ```

2. **Follow the on-screen prompts** and select option `1` for default installation

3. **Add Rust to your PATH** (the installer will prompt you). After installation, run:
   ```bash
   source $HOME/.cargo/env
   ```
   Or restart your terminal.

4. **Verify installation**:
   ```bash
   rustc --version
   cargo --version
   ```
   You should see version numbers for both.

#### Option 2: Using Homebrew (if already installed)
```bash
brew install rust
```

### Verify Your Setup
Navigate to your project directory and verify Cargo can find the project:
```bash
cd /Users/kurtabela/Library/CloudStorage/GoogleDrive-kurtabela06@gmail.com/My\ Drive/School/University/Year\ 2/Semester\ 2/Compiler\ Theory\ \&\ Practice\ \(CPS2000\)/Project/COMPILER-CPS2000
cargo --version
```

---

## Running Unit Tests

### What Are Unit Tests?
The lexer has built-in unit tests that verify individual components work correctly. These tests are defined in `src/lexer.rs` inside a `#[cfg(test)]` module.

### Running All Tests
```bash
cd COMPILER-CPS2000
cargo test
```

**Expected Output:**
```
running 18 tests
test tests::test_keywords ... ok
test tests::test_primitive_type_keywords ... ok
test tests::test_boolean_literals ... ok
test tests::test_plain_ident ... ok
test tests::test_path ... ok
test tests::test_two_segment_path ... ok
test tests::test_local ... ok
test tests::test_label ... ok
test tests::test_integer_literal ... ok
test tests::test_float_literal ... ok
test tests::test_arrow ... ok
test tests::test_colon_colon ... ok
test tests::test_single_colon ... ok
test tests::test_punctuation_mix ... ok
test tests::test_line_comment_skipped ... ok
test tests::test_lone_minus_is_error ... ok
test tests::test_invalid_local_is_error ... ok
test tests::test_abs_diff_snippet ... ok

test result: ok. 18 passed; 0 failed; 0 ignored
```

### Running Specific Tests
If you want to run only one test:
```bash
cargo test test_keywords
```

### Running Tests with Output
To see println! output from tests:
```bash
cargo test -- --nocapture
```

### Running Tests in Verbose Mode
```bash
cargo test -- --nocapture --test-threads=1
```

---

## Running the Lexer Binary

The project includes a binary (`resir_compiler`) that reads a `.resir` file and prints out all the tokens it finds. This is the main way to test the lexer on real code.

### Building the Binary
```bash
cargo build
# Or for optimized release build:
cargo build --release
```

The binary will be created at:
- Debug: `target/debug/resir_compiler`
- Release: `target/release/resir_compiler`

### Running the Binary on Test Files

#### Test File 1: Basic Math Example
```bash
cargo run -- tests/basic_math.resir
```

**What to expect:**
This file contains a function definition with local variables, basic blocks, and operations. You should see tokens representing:
- Keywords: `function`, `locals`, `entry`, `const`, `bin`, `sub`, `lt`, `cjump`, `return`, type keywords
- Paths: `Math::abs_diff`
- Locals: `%a`, `%b`, `%zero`, `%d`, `%is_neg`, `%r`
- Labels: `bb0`, `bb1`, `bb2`
- Literals: `0` (integer)
- Punctuation: `{`, `}`, `(`, `)`, `:`, `;`, `->`, `,`
- Type keywords: `i32`, `bool`

#### Test File 2: Struct Access Example
```bash
cargo run -- tests/struct_access.resir
```

**Sample output format:**
```
=== Tokens for `tests/basic_math.resir` ===
  [1:1  ]  Kw(Function)
  [1:10 ]  Path(["Math", "abs_diff"])
  [1:24 ]  LParen
  [1:25 ]  Local("a")
  [1:27 ]  Colon
  [1:28 ]  Kw(I32)
  ...
=== N tokens total ===
```

---

## Manual Testing

### Test Simple Expressions
Create a temporary test file and run the lexer on it:

#### Example 1: Keywords
```bash
echo "function locals entry const" | cargo run -- /dev/stdin
```

#### Example 2: Identifiers and Paths
```bash
echo "myVar Custom::Struct::Point foo123" | cargo run -- /dev/stdin
```

#### Example 3: Local Variables
```bash
echo "%x %counter %is_valid" | cargo run -- /dev/stdin
```

#### Example 4: Numbers
```bash
echo "42 3.14 0 999 0.5" | cargo run -- /dev/stdin
```

#### Example 5: Comments
```bash
echo "i32 // this is a comment
i64" | cargo run -- /dev/stdin
```

### Creating Custom Test Files
1. Create a new `.resir` file in the `tests/` directory
2. Write ResIR code in it
3. Run the lexer on it:
   ```bash
   cargo run -- tests/your_test_file.resir
   ```

---

## Understanding Lexer Output

### Token Format
Each line of output shows:
```
[line:col ]  TokenType(details)
```

- **line**: Line number in the source file (1-based)
- **col**: Column number in the source file (1-based)
- **TokenType**: The Rust enum variant representing this token

### Common Token Types

#### Keywords
```
Kw(Function)          // keyword: function
Kw(Locals)            // keyword: locals
Kw(Entry)             // keyword: entry
Kw(I32)               // type keyword: i32
Kw(True)              // boolean literal: true
```

#### Identifiers & Paths
```
Ident("myVariable")           // regular identifier
Path(["Math", "abs_diff"])    // path (module-qualified name)
```

#### Locals (Variables with %)
```
Local("x")            // local variable %x
Local("is_valid")     // local variable %is_valid
```

#### Labels (Basic Blocks)
```
Label("bb0")          // basic block label: bb0
Label("bb12")         // basic block label: bb12
```

#### Literals
```
IntLit(42)            // integer literal
IntLit(0)             // integer literal
FloatLit(3.14)        // floating-point literal
```

#### Punctuation
```
LBrace                // {
RBrace                // }
LParen                // (
RParen                // )
Colon                 // :
Semicolon             // ;
Comma                 // ,
Arrow                 // ->
ColonColon            // ::
Lt                    // <
Gt                    // >
Eq                    // =
Eof                   // end of file marker
```

### Example Output Walkthrough
Given input: `function Math::add(%x: i32) -> i32 { return %x; }`

Expected tokens:
```
[1:1  ]  Kw(Function)
[1:10 ]  Path(["Math", "add"])
[1:18 ]  LParen
[1:19 ]  Local("x")
[1:21 ]  Colon
[1:22 ]  Kw(I32)
[1:26 ]  RParen
[1:27 ]  Arrow
[1:30 ]  Kw(I32)
[1:34 ]  LBrace
[1:36 ]  Kw(Return)
[1:43 ]  Local("x")
[1:45 ]  Semicolon
[1:46 ]  RBrace
[1:47 ]  Eof
```

---

## Advanced Testing Features

### Run Tests with Backtrace
When a test fails, get detailed error information:
```bash
RUST_BACKTRACE=1 cargo test
```

### Build Documentation
Generate HTML documentation from code comments:
```bash
cargo doc --open
```
This will open browser documentation for all your modules.

### Check Code Without Building
Quickly verify your code has no syntax errors:
```bash
cargo check
```

### Format Code
Format your code according to Rust standards:
```bash
cargo fmt
```

### Lint Code
Check for common mistakes and improvement opportunities:
```bash
cargo clippy
```

---

## Troubleshooting

### Problem: "cargo not found"
**Solution**: Ensure Rust is installed and your PATH is set up. Run:
```bash
source $HOME/.cargo/env
```
Then verify:
```bash
cargo --version
```

### Problem: "Compilation failed with errors"
**Solution**: Check that all files are present in the `src/` directory:
- `main.rs`
- `lexer.rs`
- `ast.rs`
- `parser.rs`
- `sema.rs`
- `codegen.rs`
- `analysis.rs`

### Problem: Tests are failing
**Solution**: 
1. Check the error message carefully - it will show which test failed
2. Run that specific test with output:
   ```bash
   cargo test test_name -- --nocapture
   ```
3. Review the expected vs. actual output

### Problem: "File not found" when running binary
**Solution**: Make sure you're in the project directory and the path is correct:
```bash
# Correct:
cargo run -- tests/basic_math.resir

# Also correct (if you've built):
./target/debug/resir_compiler tests/basic_math.resir

# Wrong (you're in wrong directory):
cargo run -- basic_math.resir
```

---

## Testing Checklist

Use this checklist to ensure your lexer is fully tested:

- [ ] Install Rust/Cargo
- [ ] Run all unit tests: `cargo test`
- [ ] Confirm 18/18 tests pass
- [ ] Run lexer on `tests/basic_math.resir`
- [ ] Run lexer on `tests/struct_access.resir`
- [ ] Manually test keywords tokenization
- [ ] Manually test identifier/path tokenization
- [ ] Manually test local variable tokenization
- [ ] Manually test label tokenization
- [ ] Manually test numeric literals (integers and floats)
- [ ] Manually test comments are properly skipped
- [ ] Manually test error cases (invalid locals, lone minus)
- [ ] Verify position information (line/col) is correct

---

## Quick Reference Commands

```bash
# Setup
rustup update
cargo build

# Testing
cargo test                    # Run all unit tests
cargo test test_keywords      # Run specific test
cargo test -- --nocapture    # Show println! output

# Running Binary
cargo run -- tests/basic_math.resir
cargo run -- tests/struct_access.resir

# Utilities
cargo check                   # Quick syntax check
cargo fmt                     # Format code
cargo clippy                  # Lint code
cargo doc --open             # View documentation
```

---

## Resources

- [The Rust Book](https://doc.rust-lang.org/book/)
- [Cargo Documentation](https://doc.rust-lang.org/cargo/)
- [Rust API Guidelines](https://rust-lang.github.io/api-guidelines/)
- ResIR Language Specification (from your course materials)
