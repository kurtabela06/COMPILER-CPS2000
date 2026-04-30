# Task 5 Guide — Datalog-based Live Variable Analysis

## What This Task Does

Task 5 adds a **program analysis** pass to the compiler.  Instead of generating
code (Task 4), this pass asks a *semantic question* about the program:

> **Which variables are still "live" (needed) at each point in the function?**

A variable is **live** at a point if its current value might be read by some
instruction that executes later.  If a variable is **never** read after it was
last written, that assignment is **dead** — wasted work.

This task is implemented in two layers:

```
ResIR source
    │
    ▼  (Lexer + Parser + Validator — Tasks 1–3)
In-memory CFG
    │
    ▼  FactExtractor (C++ — analysis.cpp)
datalog/facts/edge.facts    ← control-flow edges
datalog/facts/def.facts     ← variable definitions
datalog/facts/use.facts     ← variable uses
    │
    ▼  Soufflé Datalog engine (datalog/analysis.dl)
output/live_out.csv ← variables live at exit of each block
output/live_in.csv  ← variables live at entry of each block
output/dead_defs.csv← assignments whose result is never used
```

---

## Why Is Live Variable Analysis Useful?

### Dead Code Elimination
If a variable is assigned but never live-out of its block (nobody ever reads
the value), that assignment can be deleted.  Less code = faster program.

### Register Allocation
CPUs have a fixed number of registers.  The compiler needs to decide which
variables to keep in registers and which to "spill" to memory.  A variable
that is no longer live can have its register freed immediately — no need to
spill it.

### Example

```resir
bb0:
    %zero   = const 0;      ← if zero is never live-out, this is dead
    %d      = bin sub %a, %b;
    %is_neg = bin lt %d, %zero;
    cjump %is_neg, bb1, bb2;
```

After running the analysis, `dead_defs.csv` will show `bb0  zero` because
`zero` is only ever used *inside* `bb0` itself — no successor block reads it.
An optimiser could delete `%zero = const 0` and replace the `bin lt` with a
comparison against the literal 0.

---

## Installing Soufflé on macOS

```bash
brew install souffle-lang/souffle/souffle
souffle --version   # verify it works
```

For other platforms see: https://souffle-lang.github.io/install

---

## Quick Start

```bash
# 1. Build the compiler
make

# 2. Generate Datalog fact files from a ResIR program
make analyze FILE=tests/basic_math.resir
# Writes: datalog/facts/edge.facts  datalog/facts/def.facts  datalog/facts/use.facts

# 3. Run the full analysis (generate facts + run Soufflé)
make souffle FILE=tests/basic_math.resir
# Writes: output/live_out.csv  output/live_in.csv  output/dead_defs.csv

# 4. Read the results
cat output/live_out.csv
cat output/dead_defs.csv
```

---

## How the C++ Compiler "Talks" to Datalog

The key insight is that Soufflé cannot read C++ structs.  We need a
**text-based protocol** between the two systems.

### The Three Fact Files

Soufflé reads **tab-separated value (TSV) files**, one tuple per line.
Our C++ `FactExtractor` writes these from the in-memory CFG:

**`datalog/facts/edge.facts`** — control-flow edges
```
bb0    bb1
bb0    bb2
```
*Meaning: there is a jump from `bb0` to `bb1` and from `bb0` to `bb2`.*

**`datalog/facts/def.facts`** — variable definitions (writes)
```
bb0    zero
bb0    d
bb0    is_neg
bb1    r
```
*Meaning: `zero`, `d`, and `is_neg` are assigned in `bb0`; `r` in `bb1`.*

**`datalog/facts/use.facts`** — variable uses (reads)
```
bb0    a
bb0    b
bb0    d
bb0    zero
bb0    is_neg
bb1    d
bb1    r
bb2    d
```
*Meaning: `a` and `b` are read in `bb0` (as operands to `bin sub`), etc.*

### What Counts as a "Use"?

| Opcode | Uses |
|--------|------|
| `%d = %src` (COPY) | `src` |
| `%d = const 42` | *(none — it's a literal)* |
| `%d = cast %src to T` | `src` |
| `%d = un op %src` | `src` |
| `%d = bin op %a, %b` | `a`, `b` |
| `%d = addr_of %src` | `src` |
| `%d = member_ptr %p, field` | `p` *(field is a struct field, not a local)* |
| `%d = load %p` | `p` |
| `store %p, %v` | `p`, `v` *(no destination)* |
| `%d = call F(%a, %b)` | `a`, `b` |
| `cjump %c, L1, L2` | `c` |
| `return %r` | `r` |

---

## How to Read the Results

### `output/live_out.csv`

```
bb0    d
bb0    is_neg
bb1    r
```

Each row: `block  variable`.
Variable is live (needed) when control leaves that block.

*Read as: "when `bb0` finishes, the value of `d` is still needed by a successor."*

### `output/dead_defs.csv`

```
bb0    zero
```

*Read as: "the assignment `%zero = ...` in `bb0` computes a value that no
successor block ever reads — it is a candidate for dead-code elimination."*

Note: `zero` IS used inside `bb0` itself (by `bin lt %d, %zero`), but after
`bb0` exits, no successor block reads `zero`.  Whether the assignment is truly
removable depends on whether the intra-block use can be handled differently
(e.g., inline the literal 0 directly).

---

## The Liveness Equations (Explained Simply)

```
live_out(B) = ∪{ live_in(S) | S is a successor of B }
live_in(B)  = use(B)  ∪  (live_out(B) − def(B))
```

Think of it like this: imagine each block B is a house on a street.

- **`use(B)`** — the groceries the residents of house B will eat *today*.
  They are definitely needed when you arrive at B.

- **`def(B)`** — the groceries that residents of B will throw away and
  replace with new ones.  The old value is no longer needed after B.

- **`live_out(B)`** — the groceries that need to be delivered to the houses
  *after* B.  They are "live" on the way out.

- **`live_in(B)`** — everything house B needs: today's food (`use`) plus
  whatever needs to pass through B to reach a later house (`live_out − def`).

### Why Is It Backward?

Normal program execution goes forward (bb0 → bb1 → bb2).
Liveness asks "will this value be needed LATER?" — so we reason backward:
starting from the returns (where nothing is live) and propagating toward
the entry.

Soufflé handles this automatically via its **fixpoint computation**: it
applies the rules repeatedly until no new facts are produced.

---

## The Datalog Rules (Plain English)

```prolog
// Rule 1: X is live-out of B if some successor S needs X on entry.
live_out(B, X) :- edge(B, S), live_in(S, X).

// Rule 2a: X is live-in to B if B reads X directly.
live_in(B, X)  :- use(B, X).

// Rule 2b: X is live-in to B if X is live-out AND B doesn't overwrite it.
live_in(B, X)  :- live_out(B, X), !def(B, X).
```

The `!` in Rule 2b means "NOT" — it is read as "it is not the case that X
is defined in B."  This is how we model the "kill" step: if B overwrites X,
the old value no longer flows through.

---

## Worked Example: abs_diff

```resir
function Math::abs_diff(%a: i32, %b: i32) -> i32 {
    locals { %zero:i32; %d:i32; %is_neg:bool; %r:i32; }
    entry bb0;

    bb0:                                 ← reads a, b, d, zero, is_neg
        %zero   = const 0;               ← defines zero
        %d      = bin sub %a, %b;        ← defines d, reads a, b
        %is_neg = bin lt %d, %zero;      ← defines is_neg, reads d, zero
        cjump %is_neg, bb1, bb2;         ← reads is_neg

    bb1:                                 ← reads d, r
        %r = un neg %d;                  ← defines r, reads d
        return %r;                       ← reads r

    bb2:
        return %d;                       ← reads d
}
```

**Step-by-step fixpoint:**

| Iteration | New facts added |
|-----------|----------------|
| Start | `live_in(bb1, d)` from use, `live_in(bb1, r)` from use |
| | `live_in(bb2, d)` from use |
| | `live_in(bb0, a)`, `live_in(bb0, b)`, `live_in(bb0, d)`, `live_in(bb0, zero)`, `live_in(bb0, is_neg)` from use |
| Round 2 | `live_out(bb0, d)` via edge(bb0,bb1) + live_in(bb1,d) |
| | `live_out(bb0, r)` via edge(bb0,bb1) + live_in(bb1,r) — but r is def in bb1, not in bb0 |
| | `live_out(bb0, d)` via edge(bb0,bb2) + live_in(bb2,d) |
| Round 3 | No new facts — **fixpoint reached** |

**Final results:**

| Block | live_out |
|-------|----------|
| bb0 | `d`, `r` |
| bb1 | `r` |
| bb2 | *(empty)* |

**Dead assignments:**

| Block | Variable | Why |
|-------|----------|-----|
| bb0 | `zero` | Defined in bb0, not live-out (used only inside bb0) |
| bb0 | `is_neg` | Defined in bb0, not live-out |

---

## Key Files

| File | Role |
|------|------|
| [include/analysis.hpp](include/analysis.hpp) | `FactExtractor` class declaration |
| [src/analysis.cpp](src/analysis.cpp) | Fact extraction logic |
| [datalog/analysis.dl](datalog/analysis.dl) | Soufflé liveness rules (heavily commented) |
| [src/test_analysis.cpp](src/test_analysis.cpp) | 10 tests, ~35 checks |
| [src/main.cpp](src/main.cpp) | Added `--analyze` and `-f` flags |
| [Makefile](Makefile) | Added `analyze`, `souffle`, `test_analysis` targets |
