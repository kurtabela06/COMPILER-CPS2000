# Task 4 Guide — C Code Generation

## What this task does

The code generator is the "payoff" phase of the compiler.  After the Lexer
(Task 1) tokenised text, the Parser (Task 2) built an in-memory CFG, and the
Validator (Task 3) proved it is correct, the code generator translates that
CFG into a plain `.c` file that `gcc` can compile and run.

---

## Quick Start

```bash
# Build the compiler
make

# Generate C from a ResIR file (prints to stdout)
./resir --codegen tests/basic_math.resir

# Write the generated C to a file
./resir -o generated.c tests/basic_math.resir

# Then compile and run the generated C
gcc -std=c11 -Wall -Wextra -pedantic generated.c -o my_program
./my_program
```

### Why those gcc flags?

| Flag          | Meaning                                                    |
|---------------|------------------------------------------------------------|
| `-std=c11`    | Use the C11 standard (required: our output uses `bool`)    |
| `-Wall`       | Enable most common warnings                                |
| `-Wextra`     | Enable extra warnings (catches unused variables etc.)      |
| `-pedantic`   | Reject any non-standard extensions                         |

If the generated C compiles cleanly under all four flags, it is correct standard C.

---

## The "Label and Goto" Strategy

### Why not use if/while?

A ResIR function is made of *basic blocks* — short straight-line sequences
of code that end in an explicit branch (`jump`, `cjump`, or `return`).
There is no implicit fall-through between blocks.

The cleanest mapping to C is **label + goto**:

```
ResIR block                          Generated C
────────────────────────────────     ──────────────────────────────────
bb0:                             →   bb0:
  %d = bin sub %a, %b;           →       d = a - b;
  %is_neg = bin lt %d, %zero;    →       is_neg = d < zero;
  cjump %is_neg, bb1, bb2;       →       if (is_neg) goto bb1; else goto bb2;

bb1:                             →   bb1:
  %r = un neg %d;                →       r = -d;
  return %r;                     →       return r;
```

The alternative (converting blocks back into structured `if`/`while`) is
called **structuralisation** — it is a non-trivial algorithm.  Label+goto
gives us an exact 1-to-1 translation with no additional analysis required.

---

## Type Mapping

| ResIR type        | C type           | Header needed      |
|-------------------|------------------|--------------------|
| `bool`            | `bool`           | `<stdbool.h>`      |
| `i32`             | `int32_t`        | `<stdint.h>`       |
| `i64`             | `int64_t`        | `<stdint.h>`       |
| `u32`             | `uint32_t`       | `<stdint.h>`       |
| `f64`             | `double`         | *(built-in)*       |
| `ptr<T>`          | `<C(T)>*`        | *(recursive)*      |
| `Path::Name`      | `Path_Name`      | `"resir_structs.h"`|

### Nested pointer example

```
ResIR:  ptr<ptr<i32>>
Step 1: type_to_c(ptr<i32>) + "*"
Step 2: (type_to_c(i32) + "*") + "*"
Step 3: "int32_t" + "*" + "*"
Result: int32_t**
```

The `type_to_c()` function in `codegen.cpp` handles this recursively.

---

## Path Mangling

C identifiers cannot contain `::`.  Every `::` is replaced with `_`:

```
Math::abs_diff          →  Math_abs_diff
Custom::Struct::Point   →  Custom_Struct_Point
Geom::point_x           →  Geom_point_x
```

This is done by `path_to_c()` in `codegen.cpp`.

---

## Opcode Translation Table

| ResIR opcode                | Generated C                     |
|-----------------------------|---------------------------------|
| `%d = copy %s`              | `d = s;`                        |
| `%d = const 42`             | `d = 42;`                       |
| `%d = const 3.14`           | `d = 3.14;`                     |
| `%d = const true`           | `d = true;`                     |
| `%d = const null`           | `d = NULL;`                     |
| `%d = cast %s to i64`       | `d = (int64_t)s;`               |
| `%d = un neg %x`            | `d = -x;`                       |
| `%d = un not %x`            | `d = !x;`                       |
| `%d = bin add %a, %b`       | `d = a + b;`                    |
| `%d = bin sub %a, %b`       | `d = a - b;`                    |
| `%d = bin mul %a, %b`       | `d = a * b;`                    |
| `%d = bin div %a, %b`       | `d = a / b;`                    |
| `%d = bin mod %a, %b`       | `d = a % b;`                    |
| `%d = bin eq %a, %b`        | `d = a == b;`                   |
| `%d = bin ne %a, %b`        | `d = a != b;`                   |
| `%d = bin lt %a, %b`        | `d = a < b;`                    |
| `%d = bin le %a, %b`        | `d = a <= b;`                   |
| `%d = bin gt %a, %b`        | `d = a > b;`                    |
| `%d = bin ge %a, %b`        | `d = a >= b;`                   |
| `%d = bin and %a, %b`       | `d = a && b;`                   |
| `%d = bin or %a, %b`        | `d = a \|\| b;`                 |
| `%d = addr_of %x`           | `d = &x;`                       |
| `%d = member_ptr %p, field` | `d = &p->field;`                |
| `%d = load %p`              | `d = *p;`                       |
| `store %p, %v`              | `*p = v;`                       |
| `%d = call Path(%a, %b)`    | `d = Path_mangled(a, b);`       |
| `jump bb1`                  | `goto bb1;`                     |
| `cjump %c, bb1, bb2`        | `if (c) goto bb1; else goto bb2;` |
| `return %v`                 | `return v;`                     |
| `return`                    | `return;`                       |

---

## Structure of the Generated File

```c
// Generated by the ResIR compiler (Task 4)
#include <stdint.h>
#include <stdbool.h>
#include "resir_structs.h"

// Forward declarations for extern types
typedef struct Custom_Struct_Point Custom_Struct_Point;

int32_t Math_abs_diff(int32_t a, int32_t b) {
    // --- Local variable declarations ---
    int32_t zero;
    int32_t d;
    bool    is_neg;
    int32_t r;

    // Jump to the entry block of the function.
    goto bb0;

    bb0:
        zero = 0;
        d = a - b;
        is_neg = d < zero;
        if (is_neg) goto bb1; else goto bb2;

    bb1:
        r = -d;
        return r;

    bb2:
        return d;
}
```

### Why declare all locals at the top?

Placing all `goto` labels inside a function body creates a subtle C rule:
you cannot jump *over* a variable declaration.  Declaring all locals at the
very top of the function body (before any `goto` or label) avoids the
`"jump into scope of identifier"` error that GCC raises under `-Wextra`.

---

## Makefile Targets

```bash
make codegen FILE=tests/basic_math.resir
    # → ./resir --codegen tests/basic_math.resir
    # Prints the generated C to stdout.

make emit FILE=tests/basic_math.resir OUT=generated.c
    # → ./resir -o generated.c tests/basic_math.resir
    # Writes the generated C to generated.c.

make test_codegen
    # Builds and runs src/test_codegen.cpp (18 tests, ~70 checks).
```

---

## Running the Tests

```bash
make test_codegen
# Expected output:
#   === Task 4: Code Generation Tests ===
#   [PASS] type_mapping: i32 param  : contains "int32_t a"
#   [PASS] type_mapping: i64 param  : contains "int64_t b"
#   ...
#   Total: 70 checks, 70 pass, 0 fail
```

To run all four suites at once:

```bash
make test
```

---

## Key Files

| File                    | Role                                              |
|-------------------------|---------------------------------------------------|
| `include/codegen.hpp`   | `CodeGen` class declaration and inline docs       |
| `src/codegen.cpp`       | All emit functions + type/path helpers            |
| `src/test_codegen.cpp`  | 18 test cases covering every opcode               |
| `src/main.cpp`          | Updated pipeline: adds `--codegen` / `-o` flags   |
| `Makefile`              | Added `codegen`, `emit`, `test_codegen` targets   |
