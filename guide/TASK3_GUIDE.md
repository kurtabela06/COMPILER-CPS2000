# Task 3 – Semantic Validation & Type Checking: Complete Guide

## What Does the Validator Do?

The **parser** (Task 2) checked that the source text followed the grammar.
The **validator** (Task 3) checks that the *meaning* is correct.
It answers questions like:

- Is every local variable that gets used actually declared?
- Does every `jump` point to a real block?
- Is the `cjump` condition actually a `bool`?
- Does `load %p` have `%p` typed as a pointer?
- Does `return %x` give back the right type?

If any check fails, compilation stops with a message like:

```
Semantic error at line 14: in block `bb0`: type mismatch for `%r`
  — declared as `i32` but assigned expression has type `bool`
```

---

## How to Run

```bash
make                              # builds ./resir
./resir tests/basic_math.resir    # full pipeline: lex → parse → validate → print

./resir --parse tests/basic_math.resir   # skip validation (Task 2 mode)
./resir --tokens tests/basic_math.resir  # token dump only (Task 1 mode)

make test_validator               # run 32 validator-specific unit tests
make test                         # run all 130 tests (23 + 75 + 32)
```

---

## The Four Validation Phases

The validator runs four phases in order. It stops at the first error.

```
Phase 1: build_symbol_table()
  Read every param and local declaration.
  Insert name → type into the symbol table (sym_).
  Error if any name appears twice.

Phase 2: collect_block_labels()
  Walk every Block, record its label.
  Error if any label appears more than once.

Phase 3: check_entry_label()
  Look up fn_.entry_label in the collected label set.
  Error if it does not exist.

Phase 4: validate_all_blocks()
  For each Block:
    For each Statement → infer_rhs_type() + dest-type check
    For the Terminator → target existence + bool cond + return type
```

---

## The Invariants (Rules) the Validator Checks

### 1. Variable Integrity
| Rule | Example violation |
|------|-------------------|
| Every local used in an Rhs must be in `sym_` | `bin add %a, %ghost` where `%ghost` was never declared |
| Every destination local must be in `sym_` | `%mystery = const 1` when `%mystery` is not in `locals` |
| No local or param name may appear twice | `%x : i32; %x : bool;` in the locals block |

### 2. Control Flow Integrity
| Rule | Example violation |
|------|-------------------|
| Entry label must name an existing block | `entry bb5;` when no `bb5:` exists |
| Every `jump` target must exist | `jump bb99;` when `bb99:` is absent |
| Every `cjump` true- and false-branch must exist | `cjump %c, bb55, bb0;` when `bb55` is absent |
| Block labels must be unique | Two blocks both named `bb0:` |

### 3. Type Consistency
| Instruction | Rule |
|-------------|------|
| `un neg %x` | `%x` must be numeric (i32/i64/u32/f64) |
| `un not %x` | `%x` must be `bool` |
| `bin add/sub/mul/div/mod %a, %b` | Both must be the **same** numeric type |
| `bin eq/ne/lt/le/gt/ge %a, %b` | Both must be the same numeric type **or** both `bool`; result is `bool` |
| `bin and/or %a, %b` | Both must be `bool`; result is `bool` |
| `cjump %c, …` | `%c` must be `bool` |
| `return %x` | `%x`'s type must equal the function's declared return type |
| `return` (void) | Function must be declared `-> void` |
| `return %x` in a void function | Error — void functions may not return a value |

### 4. Pointer & Struct Rules
| Instruction | Rule |
|-------------|------|
| `load %p` | `%p` must be `ptr<T>` for some T; result type is `T` |
| `store %p, %v` | `%p : ptr<T>` and `%v : T` must match |
| `addr_of %x` | `%x : T`; result is `ptr<T>` |
| `member_ptr %p, f` | `%p` must be `ptr<Path>` where `Path` is a known `extern type`; `f` must be a declared field; result is `ptr<field_type>` |

---

## How Type Inference Works

"Type inference" means: *given an instruction, what type does it produce?*

The validator uses a **symbol table** — a `std::map<string, ResIRType>` called
`sym_` — that maps every local name to its type.

```
Before walking any block:
  sym_ = { "a" → I32, "b" → I32, "zero" → I32, "d" → I32,
            "is_neg" → BOOL, "r" → I32 }   ← from params + locals

Walking bb0:
  stmt: %zero = const 0
    → infer CONST int → I32
    → declared type of "zero" is I32 ✓

  stmt: %d = bin sub %a, %b
    → look up "a" → I32, "b" → I32
    → both same numeric type → result I32
    → declared type of "d" is I32 ✓

  stmt: %is_neg = bin lt %d, %zero
    → look up "d" → I32, "zero" → I32
    → comparison → result BOOL
    → declared type of "is_neg" is BOOL ✓
```

The type of each RHS is **derived** from the operand types; it is never
just assumed to be correct.

---

## How the Validator Handles Recursive Types (e.g. `ptr<ptr<i32>>`)

`ResIRType` is a tree structure using `std::unique_ptr<ResIRType>` for the
inner pointer type.  Comparison and copying both walk the tree recursively.

```
ptr<ptr<i32>> is represented as:
  ResIRType { kind=PTR, inner →
    ResIRType { kind=PTR, inner →
      ResIRType { kind=I32 }
    }
  }
```

When the validator stores types in the symbol table, it calls `copy_type()`
which walks this tree and allocates fresh `unique_ptr` nodes at each level.
This is necessary because `std::map` copies its values, but `unique_ptr`
forbids copying.

When comparing two types (e.g. checking `return %x` against the return
type), `operator==` in `ResIRType` recurses into `*inner == *other.inner`.

---

## Interpreting Semantic Error Messages

Every message follows this pattern:

```
Semantic error [at line N]: <context>: <description>
```

| Part | Example |
|------|---------|
| `at line N` | `at line 14` — where in the source file the bad instruction is |
| context | `in block \`bb0\`` — which basic block contains the problem |
| description | `type mismatch for \`%r\` — declared as \`i32\` but got \`bool\`` |

### Common messages and their meaning

```
local `%x` used before declaration (not in `locals` or params)
  → You used %x in an expression but forgot to declare it in the locals block.

block label `bb0` is defined more than once
  → Two blocks share the same name.

entry label `bb5` does not name any block
  → The `entry bb5;` line points to a block that doesn't exist.

jump target `bb99` does not exist
  → A `jump bb99;` points to a block that isn't in the function.

`cjump` condition `%x` has type `i32` but must be `bool`
  → cjump requires a bool condition. Use `bin lt %x, %zero` to get one.

return type mismatch — function declared to return `i32` but `%r` has type `bool`
  → The local you are returning has the wrong type.

`load` requires a pointer operand but `%x` has type `i32`
  → load dereferences a pointer. You passed a plain value instead.

struct `Custom::Struct::Point` has no field named `z`
  → You used `member_ptr %p, z` but the extern type only declares `x` and `y`.
```

---

## File Map

```
COMPILER-CPS2000/
├── include/
│   └── validator.hpp   Validator class declaration + invariant documentation
├── src/
│   ├── validator.cpp   Four-phase validation + full type inference for every Rhs
│   └── test_validator.cpp  32 unit tests (10 valid programs + 22 error cases)
```

---

## Running the Tests

```bash
make test_validator
```

Expected:

```
==============================
 ResIR Validator — Unit Tests
==============================
…
 Results: 32 passed, 0 failed
==============================
```

The tests cover: all 10 valid program shapes (arithmetic, structs, pointers,
casts, calls, store, logical ops) and 22 deliberate error cases across all
four invariant categories.
