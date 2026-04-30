# Task 1 – Table-Driven Lexer: Complete Guide

## What is a Lexer?

A **lexer** (also called a *scanner* or *tokeniser*) reads raw source text
character by character and groups those characters into meaningful units called
**tokens**.

Think of it like reading a sentence: before you can understand grammar you
must first split the text into words.  The lexer does exactly that — but for
a programming language.

```
Input text:  "function Math::abs_diff(%a: i32) -> i32 {"
                                        ↓ (lexer)
Tokens:      [KW_FUNCTION] [IDENT "Math"] [::] [IDENT "abs_diff"]
             [LPAREN] [LOCAL "%a"] [COLON] [KW_I32] [RPAREN]
             [ARROW] [KW_I32] [LBRACE]
```

---

## How to Compile

You only need `g++` (which comes with Xcode Command Line Tools on macOS).

```bash
# From the project root directory:
make          # builds the main binary: ./resir
make test     # builds AND runs all unit tests
make clean    # deletes the built binaries
```

---

## How to Run

```bash
# Lex a ResIR file and print every token:
./resir tests/basic_math.resir

# You can also lex any other .resir file:
./resir tests/struct_access.resir
```

The output looks like this:

```
LINE:COL   KIND             TEXT
------------------------------------------------------------
2:1        function         "function"
2:10       <identifier>     "Math"
2:14       ::               "::"
2:16       <identifier>     "abs_diff"
...
```

Each column is:
| Column   | Meaning |
|----------|---------|
| LINE:COL | Where the token starts in the source file (1-based) |
| KIND     | What type of token it is (keyword, local, literal…) |
| TEXT     | The exact characters from the source |

---

## What is a Table-Driven Lexer?

A **table-driven lexer** encodes all its logic in a 2-D array called the
*transition table*.  Think of a state machine:

- **States** = "what are we currently reading?"
- **Input classes** = "what category of character just arrived?"
- **Table cell** = "given this state and this character class, what is the
  next state?"

Instead of a giant chain of `if`/`else` statements, one tight loop handles
everything:

```
next_state = TRANSITION[current_state][character_class]
```

This makes the lexer easy to audit (just look at the table) and easy to
extend (just add a new row or column).

---

## State Machine Diagram

Below is an ASCII art diagram of the DFA implemented in this lexer.
Each box is a **state**.  Arrows show transitions labelled by **input class**.
Double-bordered boxes (`[[ ]]`) are **accepting** (token complete).

```
                        ┌─────────────────────────────────────────────────┐
                        │                                                 │
        ALPHA           │            DIGIT                                │
   ┌────────────┐   ┌───▼────────┐           ┌────────────────┐          │
   │            │   │            │   DIGIT   │                │          │
   │  IN_IDENT  ├──►│ IN_IDENT   │   DOT►    │  IN_FLOAT_FRAC │          │
   │  (keyword/ │   │            │           │                │          │
   │  label/    │   └─────┬──────┘           └───────┬────────┘          │
   │  ident)    │         │ other                     │ other             │
   └─────┬──────┘         ▼                           ▼                  │
         │           [[ DONE ]]                  [[ DONE ]]              │
         │                                                                │
         │  PERCENT                                                       │
    ┌────▼───────┐                                                        │
    │  IN_LOCAL  │  ALPHA/DIGIT/_                                         │
    │  (%name)   ├──────────────────► IN_LOCAL                           │
    └─────┬──────┘                                                        │
          │ other                                                          │
          ▼                                                                │
     [[ DONE ]]                                                           │
                                                                          │
         MINUS                    GT                                      │
   ┌─────────────┐          [[ DONE: Arrow ]]                             │
   │  IN_MINUS   │              ▲                                         │
   │   (saw `-`) ├──────────────┘                                         │
   └─────┬───────┘                                                        │
         │ other                                                           │
         ▼                                                                 │
      [[ ERROR ]]                                                          │
                                                                          │
         COLON                    COLON                                   │
   ┌─────────────┐          [[ DONE: `::` ]]                              │
   │  IN_COLON   ├──────────────────────────                              │
   │   (saw `:`) │                                                        │
   └─────┬───────┘                                                        │
         │ other                                                           │
    [[ DONE: `:` ]]                                                        │
                                                                          │
         SLASH                    SLASH                                   │
   ┌─────────────┐          ┌──────────────────┐                          │
   │  IN_SLASH   ├──────────► IN_LINE_COMMENT  ├─── NEWLINE ──────────────┘
   │   (saw `/`) │          │  (skip rest of   │
   └─────┬───────┘          │   line)          │
         │ other             └──────────────────┘
      [[ ERROR ]]

         PUNCT
    [[ DONE: single-char token ]]

         SPACE / NEWLINE
         (ignored, back to START)
```

**Key**: `[[ DONE ]]` means the lexer emits a token and resets to `START`.
`[[ ERROR ]]` means the lexer reports a lexical error with line:col.

---

## File Map

```
COMPILER-CPS2000/
├── include/
│   ├── token.hpp       Token struct and TokenKind enum
│   └── lexer.hpp       Lexer class declaration (table + methods)
├── src/
│   ├── token.cpp       token_kind_name() for error messages
│   ├── lexer.cpp       All lexer logic + transition table definition
│   ├── main.cpp        Entry point (reads file, runs lexer, prints tokens)
│   └── test_lexer.cpp  23 unit tests
├── tests/
│   ├── basic_math.resir       abs_diff example from the spec
│   └── struct_access.resir    member_ptr example from the spec
└── Makefile
```

---

## Understanding the Code: Key Concepts

### `classify(char c)` — the "column picker"

Every character is mapped to an `InputClass` enum value.  This is the
column index into the transition table.

```cpp
// Example: 'a'–'z', 'A'–'Z', '_' all map to ALPHA (column 0)
if ((c >= 'A' && c <= 'Z') || ...) return InputClass::ALPHA;
```

### The transition table — the heart of the design

```cpp
// TRANSITION_[state][class] = next_state
const Lexer::State Lexer::TRANSITION_[...][...] = {
    /* START */
    { S::IN_IDENT, S::IN_INT_LIT, S::IN_LOCAL, ... },
    /* IN_IDENT */
    { S::IN_IDENT, S::IN_IDENT,   S::DONE, ... },
    ...
};
```

One array lookup replaces all the branching logic.

### `scan_ident()` — keyword/label/identifier detection

After the DFA reads a full word, we check:
1. Does it match `bb[0-9]+`? → `LABEL`
2. Is it in the keyword map? → `KW_*`
3. Otherwise → `IDENT`

### Error messages

Every error includes the exact line and column:

```
Lexical error at 3:5 — unexpected character `@` (0x40)
```

---

## Running the Tests

```bash
make test
```

Expected output:

```
==============================
 ResIR Lexer — Unit Tests
==============================
...
 Results: 23 passed, 0 failed
==============================
```

Each test function in `src/test_lexer.cpp` has a descriptive name and a
`CHECK(name, condition)` call so failures are immediately identifiable.
