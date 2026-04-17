# Lexer Testing - Quick Reference Card

## One-Time Setup (First Time Only)

```bash
# Install Rust (one time)
curl --proto '=https' --tlsv1.2 -sSf https://sh.rustup.rs | sh -s -- -y

# Load Rust into current shell
source $HOME/.cargo/env
```

---

## Daily Testing Commands

### Run All Unit Tests (Recommended for validation)
```bash
cd COMPILER-CPS2000
cargo test
```
**Expected**: 18 tests pass ✅

### Run Lexer on Test Files

**Option A: Using cargo run (recommended)**
```bash
# Test file 1: Basic math example
cargo run -- tests/basic_math.resir

# Test file 2: Struct access example  
cargo run -- tests/struct_access.resir
```

**Option B: Using compiled binary**
```bash
# Build first (if not already built)
cargo build

# Then run
./target/debug/resir_compiler tests/basic_math.resir
./target/debug/resir_compiler tests/struct_access.resir
```

### Run Specific Unit Test
```bash
cargo test test_keywords        # Test keywords
cargo test test_plain_ident     # Test identifiers
cargo test test_path            # Test paths
cargo test test_local           # Test locals
cargo test test_label           # Test labels
cargo test test_integer_literal # Test integer literals
cargo test test_float_literal   # Test float literals
```

### Test With Output Visible
```bash
cargo test -- --nocapture
```

---

## Test Quick Facts

| Test Category | Count | Status |
|---|---|---|
| Unit Tests | 18 | ✅ All Pass |
| Test Files | 2 | ✅ Both Pass |
| Keywords Tested | 27+ | ✅ Complete |
| Token Types | 18 | ✅ Covered |

---

## Test File Details

### basic_math.resir
- Contains: Function definition with operations
- Tokens: 84
- Features: Keywords, paths, locals, labels, operations, types

### struct_access.resir  
- Contains: Type definition and struct member access
- Tokens: 62
- Features: External types, generics (ptr<T>), struct operations

---

## What Each Test Checks

### Unit Tests (18 total)
1. **Keywords** - All ResIR keywords recognized
2. **Type Keywords** - i32, i64, u32, f64, bool
3. **Booleans** - true, false, null
4. **Identifiers** - Variable names recognized
5. **Paths** - Qualified names (Math::add)
6. **Two-Segment Paths** - Specific path test
7. **Locals** - Variables starting with % 
8. **Labels** - Basic block labels (bb0, bb1, etc.)
9. **Integers** - Numeric literals
10. **Floats** - Decimal number literals
11. **Arrows** - The -> token
12. **ColonColon** - The :: token
13. **Colon** - The : token
14. **Punctuation** - All {} () ; , < > =
15. **Comments** - // comments skipped
16. **Lone Minus Error** - Rejects bare -
17. **Invalid Local Error** - Rejects %123
18. **Integration** - Full code snippet

---

## Troubleshooting

### Command not found: cargo
```bash
source $HOME/.cargo/env
```

### Tests fail to compile
```bash
cargo clean
cargo test
```

### Binary won't run
```bash
cargo build                         # Build first
./target/debug/resir_compiler FILE  # Then run
```

### File not found
```bash
# Wrong ❌
cargo run -- basic_math.resir

# Right ✅
cargo run -- tests/basic_math.resir
```

---

## Project Structure
```
COMPILER-CPS2000/
├── src/
│   ├── lexer.rs          ← Task 1 Implementation
│   ├── main.rs           ← Binary entry point
│   ├── ast.rs
│   ├── parser.rs
│   ├── sema.rs
│   ├── codegen.rs
│   └── analysis.rs
├── tests/
│   ├── basic_math.resir  ← Test file 1
│   ├── struct_access.resir ← Test file 2
│   └── expected_output/
├── Cargo.toml            ← Project config
└── docs/
    └── report.md
```

---

## Success Criteria ✅

Your lexer is working correctly when:
- ✅ All 18 unit tests pass
- ✅ basic_math.resir produces 84 tokens
- ✅ struct_access.resir produces 62 tokens
- ✅ Line/column information is correct
- ✅ Keywords are recognized
- ✅ Comments are skipped
- ✅ Errors are reported with position

---

## For Documentation Generation
```bash
cargo doc --open  # View generated HTML docs
```

---

## Next Steps

Once Task 1 (Lexer) is validated:
1. Move on to Task 2: Parser (consumes lexer output)
2. Parser will build AST from tokens
3. Semantic analysis follows parsing
4. Code generation last

---

**All tests created:** April 17, 2026  
**Lexer Status:** ✅ COMPLETE AND TESTED  
**Ready for:** Task 2: Parser
