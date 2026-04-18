# Task 2 – Hand-Written Parser: Complete Guide

## What is a Parser?

The **lexer** (Task 1) turned raw text into a flat list of tokens.  The
**parser** reads that list and checks that the tokens follow the grammar rules.
If they do, it builds an in-memory data structure — the **Control Flow Graph
(CFG)** — that every later compiler pass uses.

```
Source text
    ↓  [Lexer - Task 1]
Token list:  [KW_FUNCTION] [IDENT "Math"] [::] [IDENT "abs_diff"] …
    ↓  [Parser - Task 2]
Program struct:
  Function {
    path = "Math::abs_diff"
    params = [ {name="a", type=I32}, {name="b", type=I32} ]
    return_type = I32
    blocks = [
      Block { label="bb0", stmts=[…], term=CJUMP }
      Block { label="bb1", stmts=[…], term=RETURN }
      Block { label="bb2", stmts=[…], term=RETURN }
    ]
  }
```

---

## How to Compile and Run

```bash
make                              # build ./resir
./resir tests/basic_math.resir    # parse and print the CFG
./resir --tokens tests/basic_math.resir  # token dump only (Task 1 mode)

make test                         # run ALL 75 tests (lexer + parser)
make test_parser                  # run only parser tests (75 → 52 parser tests)
```

---

## What is Recursive Descent Parsing?

A **recursive descent parser** has one function per grammar rule.  Each
function reads just the tokens it needs, then calls sub-functions for
nested rules.

The ResIR grammar is **predictive**: looking at the next token is always
enough to know which rule to follow.  No backtracking is ever needed.

### The Call Tree

```
parse()
└─ parse_program()
    ├─ parse_extern_type()    ← called 0 or more times while next = `extern`
    │   └─ parse_field()      ← called per field inside the braces
    │       └─ parse_type()
    └─ parse_function()       ← called exactly once
        ├─ parse_params()     ← called once for the parameter list
        │   └─ parse_type()   ← called once per parameter
        ├─ parse_type()       ← return type
        ├─ locals section     ← inline: parse_local + parse_type per local
        └─ parse_block()      ← called once per basic block
            ├─ parse_stmt()   ← called until terminator is found
            │   └─ parse_rhs()  ← dispatches on the keyword (bin/un/call/…)
            └─ parse_term()   ← handles jump / cjump / return
```

### The `peek` / `advance` / `expect` pattern

Every parser function uses three helpers:

| Helper | What it does |
|--------|-------------|
| `peek()` | Look at the current token **without consuming** it |
| `advance()` | **Consume** the current token and return it |
| `expect(K)` | Consume if kind matches, **error** otherwise |

Example from `parse_function`:

```cpp
// We are at: `function Math::abs_diff(%a: i32 …`
// peek() == KW_FUNCTION
if (!expect(TokenKind::KW_FUNCTION, tok)) return false; // consume `function`
if (!parse_path(out.path)) return false;                 // consume `Math::abs_diff`
if (!expect(TokenKind::LPAREN, tok)) return false;       // consume `(`
```

---

## The Control Flow Graph (CFG) in Memory

All types are defined in [include/cfg.hpp](include/cfg.hpp).

### Type hierarchy

```
Program
├── extern_types : vector<ExternType>
│     ExternType { path, fields: vector<ExternField> }
│     ExternField { name, type: ResIRType }
└── function : Function
      ├── path       : string   ("Math::abs_diff")
      ├── params     : vector<Param>
      ├── return_type: ResIRType
      ├── locals     : vector<LocalDecl>
      ├── entry_label: string   ("bb0")
      └── blocks     : vector<Block>
            Block {
              label : string
              stmts : vector<Statement>
              term  : Terminator
            }
```

### ResIRType — recursive type representation

```cpp
struct ResIRType {
    TypeKind kind;          // BOOL, I32, I64, U32, F64, PTR, or PATH
    string   path;          // only for PATH (e.g. "Custom::Struct::Point")
    unique_ptr<ResIRType> inner;  // only for PTR — points to the inner type
};
```

`ptr<ptr<i32>>` is stored as:

```
ResIRType { kind=PTR, inner →
  ResIRType { kind=PTR, inner →
    ResIRType { kind=I32 }
  }
}
```

`std::unique_ptr` is used here because a struct cannot contain itself
directly (that would be infinite in size).  A pointer-to-heap is the
standard C++ solution.

### Statement and Rhs

```cpp
struct Statement {
    bool        has_dest;  // is there a %dest = … ?
    string      dest;      // destination local name (no %)
    Rhs         rhs;       // the right-hand side (all opcodes)
};

struct Rhs {
    RhsKind kind;          // COPY, CONST, CAST, UN, BIN, …
    string  op1, op2;      // operand locals (no %)
    BinOp   bin_op;        // for BIN
    UnOp    un_op;         // for UN
    ResIRType cast_type;   // for CAST
    Literal   literal;     // for CONST
    string    call_path;   // for CALL
    vector<string> call_args; // for CALL
};
```

### Terminator

Every block ends with exactly one of:

```cpp
enum class TermKind { JUMP, CJUMP, RETURN };

struct Terminator {
    TermKind kind;
    string label1;    // JUMP: target; CJUMP: true-branch
    string label2;    // CJUMP: false-branch
    string cond;      // CJUMP: condition local
    string ret_local; // RETURN: returned local (empty if void)
};
```

---

## How Parsing Decides What to Do Next

The grammar is designed so that **one token of lookahead** is always enough.

| Next token | Parser action |
|------------|--------------|
| `extern`   | `parse_extern_type()` |
| `function` | `parse_function()` |
| `%local =` (LOCAL followed by EQ) | statement with destination |
| `const` | `RhsKind::CONST` |
| `bin`   | `RhsKind::BIN` |
| `un`    | `RhsKind::UN` |
| `call`  | `RhsKind::CALL` |
| `jump`  | `TermKind::JUMP` |
| `cjump` | `TermKind::CJUMP` |
| `return`| `TermKind::RETURN` |
| `ptr`   | pointer type (recurse into `<…>`) |
| `bool/i32/i64/u32/f64` | primitive type |
| `IDENT` (in type position) | PATH type |

---

## Error Messages

Every error includes the exact source location:

```
Syntax error at 5:3 — expected `;` but found `}`
Syntax error at 12:1 — expected a terminator (jump/cjump/return) to end block `bb0`
Syntax error at 3:8 — expected a binary operator (add/sub/mul/…)
```

---

## File Map

```
COMPILER-CPS2000/
├── include/
│   ├── cfg.hpp         All CFG node types (Program, Function, Block, …)
│   └── parser.hpp      Parser class declaration
├── src/
│   ├── cfg.cpp         print_program() debug printer
│   ├── parser.cpp      All parsing logic (one function per grammar rule)
│   └── test_parser.cpp 52 unit tests for the parser
```

---

## Running the Tests

```bash
make test_parser
```

Expected:

```
==============================
 ResIR Parser — Unit Tests
==============================
…
 Results: 52 passed, 0 failed
==============================
```

Tests cover: minimal void function, abs_diff (full spec example), extern type
with member_ptr/load, call opcode, jump, nested `ptr<ptr<bool>>`, cast, and
three syntax-error scenarios.
