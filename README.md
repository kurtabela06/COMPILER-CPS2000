# COMPILER-CPS2000

ResIR compiler for **CPS2000 — Compiler Theory & Practice**.

A complete six-task compiler implementation:

| Task | Weight | What it does | Where |
|------|--------|--------------|-------|
| 1. Lexer | 15% | Table-driven DFA tokeniser | `src/lexer.cpp` |
| 2. Parser | 20% | Recursive-descent parser → CFG | `src/parser.cpp` |
| 3. Validator | 15% | Semantic + type checking | `src/validator.cpp` |
| 4. Code Generator | 25% | ResIR → C (label-and-goto) | `src/codegen.cpp` |
| 5. Datalog Analysis | 15% | Soufflé live-variable analysis | `src/analysis.cpp` + `datalog/analysis.dl` |
| 6. E-graphs Paper | 10% | 2-page research paper | `docs/E-Graphs Paper.pdf` |

Implementation language: **C++17** (Tasks 1–5) + **Soufflé Datalog** (Task 5).
No external libraries.

---

## Quick Start

```bash
make                 # builds build/resir
make test            # runs all 5 unit-test suites (245 checks)
```

Run the compiler on the supplied test inputs:

```bash
# Validate + print the parsed CFG
build/resir tests/basic_math.resir

# Validate + emit C (label-and-goto)
make codegen FILE=tests/basic_math.resir
make emit    FILE=tests/basic_math.resir OUT=generated.c
gcc -std=c11 -Wall -Wextra -pedantic generated.c -c

# Validate + run live-variable analysis
make analyze FILE=tests/basic_math.resir   # writes datalog/facts/*.facts
make souffle FILE=tests/basic_math.resir   # runs Soufflé end-to-end
```

If Soufflé is not installed, `make souffle` prints install instructions
(`brew install souffle-lang/souffle/souffle` on macOS).

---

## Submission Layout

See [`file_structure.txt`](file_structure.txt) for the full annotated tree.

Key directories:

- [`include/`](include/), [`src/`](src/) — C++ headers and implementations
- [`datalog/`](datalog/) — Soufflé `.dl` script and generated `facts/`
- [`output/`](output/) — Soufflé output (`live_in.csv`, `live_out.csv`, `dead_defs.csv`)
- [`tests/`](tests/) — `.resir` source files from the project specification
- [`build/`](build/) — compiled binaries (created by `make`, removed by `make clean`)
- [`guide/`](guide/) — per-task tutorial guides
- [`docs/`](docs/) — final report and e-graphs paper

The Report and the E-Graphs Paper are inside [`docs/`](docs/).

---

## Documentation

Each task has a dedicated tutorial guide in [`guide/`](guide/):

- [Task 1 — Lexer](guide/TASK1_GUIDE.md)
- [Task 2 — Parser](guide/TASK2_GUIDE.md)
- [Task 3 — Validator](guide/TASK3_GUIDE.md)
- [Task 4 — C Code Generation](guide/TASK4_GUIDE.md)
- [Task 5 — Datalog Live Variable Analysis](guide/TASK5_GUIDE.md)

The full implementation report is in [`docs/Report.pdf`](docs/Report.pdf).
