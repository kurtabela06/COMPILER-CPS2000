// =============================================================================
// validator.hpp  —  Semantic validation and type-checking pass for ResIR
// =============================================================================
//
// What does this pass do?
//   The parser (Task 2) checked that the source TEXT followed the grammar.
//   The validator (Task 3) checks that the MEANING is correct:
//
//     1. VARIABLE INTEGRITY
//        Every local used in a statement must be declared in `locals` or params.
//
//     2. CONTROL FLOW INTEGRITY
//        - The entry label must refer to an actual block.
//        - Every jump/cjump target must refer to an actual block.
//        - Block labels must be unique (no two blocks with the same name).
//
//     3. TYPE CONSISTENCY
//        - Arithmetic binops (add/sub/…) need numeric operands of the same type.
//        - Comparison binops (eq/ne/lt/…) need compatible operands; produce bool.
//        - Logical binops (and/or) need bool operands; produce bool.
//        - neg requires a numeric operand; not requires bool.
//        - cjump condition must have type bool.
//        - return value (if any) must match the declared return type.
//        - load %p requires %p to have type ptr<T>; result type is T.
//        - store %p, %v requires %p : ptr<T> and %v : T.
//        - addr_of %x produces ptr<T> if %x : T.
//        - cast always succeeds (explicit conversion) — result is the target type.
//        - call — callee signature not in-scope; result typed as declared dest.
//          (We trust the programmer for cross-function calls per spec.)
//
//     4. POINTER & STRUCT RULES (member_ptr)
//        - Operand %p must have type ptr<Path> where Path is a known ExternType.
//        - The named field must exist in that ExternType.
//        - The result type is ptr<T> where T is the field's declared type.
//
// Symbol Table:
//   We maintain a  std::map<std::string, ResIRType>  called `sym_`  that maps
//   every local variable name (without %) to its known type.  This is populated
//   at the start from the function params and locals declarations, then enriched
//   as we encounter assignments (we infer the type of each assigned local from
//   the RHS).
//
// How to use:
//   Validator v(prog);
//   if (!v.validate()) {
//       std::cerr << v.get_error() << "\n";
//   }
// =============================================================================

#pragma once
#include "cfg.hpp"     // Program, Function, Block, Statement, ResIRType, …

#include <string>
#include <map>         // std::map<K,V> — ordered key-value store (symbol table)
#include <vector>

// ---------------------------------------------------------------------------
// Validator class
// ---------------------------------------------------------------------------
class Validator {
public:
    // Constructor: takes the fully-parsed program.
    // We keep a const reference — the validator only READS the CFG.
    explicit Validator(const Program& prog);

    // validate() — run all checks.
    //   Returns true if the program is well-formed.
    //   Returns false on the first semantic error; call get_error() for details.
    bool validate();

    // Return the last error message.
    const std::string& get_error() const { return error_; }

private:
    // -----------------------------------------------------------------------
    // Data
    // -----------------------------------------------------------------------

    const Program&  prog_;   // the full parsed program (extern types + function)
    const Function& fn_;     // shorthand for prog_.function

    // Symbol table: maps local name (without %) → its ResIR type.
    //
    // std::map<K,V> is a sorted dictionary.  We use it here because:
    //   - We need O(log n) lookup by local name.
    //   - The iteration order doesn't matter for correctness.
    //   - It's simpler to reason about than std::unordered_map for a beginner.
    //
    // Entries come from:
    //   1. Function parameters (populated once at the start of validate_function)
    //   2. Local declarations in the `locals` block (also populated up front)
    //   3. Inferred types for assigned locals — added as we visit each statement
    std::map<std::string, ResIRType> sym_;

    // Set of all block labels that exist in the function.
    // Used to verify that jump/cjump targets are valid.
    // std::map<label, true> — we use map rather than set just for consistency.
    std::map<std::string, bool> block_labels_;

    std::string error_;  // set on the first validation error

    // -----------------------------------------------------------------------
    // Error helpers
    // -----------------------------------------------------------------------

    // sem_error() — format a semantic error message and return false.
    // Always returns false so callers can write:  return sem_error("…")
    bool sem_error(const std::string& msg, int line = 0);

    // -----------------------------------------------------------------------
    // Top-level validation phases
    // -----------------------------------------------------------------------

    // Phase 1: build the symbol table from params + locals declarations.
    // Also checks that no local name is declared twice.
    bool build_symbol_table();

    // Phase 2: collect all block labels and check for duplicates.
    bool collect_block_labels();

    // Phase 3: verify the entry label exists.
    bool check_entry_label();

    // Phase 4: walk every block and validate it.
    bool validate_all_blocks();

    // -----------------------------------------------------------------------
    // Per-block / per-statement validation
    // -----------------------------------------------------------------------

    bool validate_block(const Block& blk);
    bool validate_stmt(const Statement& s, const std::string& block_label);
    bool validate_terminator(const Terminator& t, const std::string& block_label);

    // -----------------------------------------------------------------------
    // RHS type inference
    // -----------------------------------------------------------------------
    //
    // TYPE INFERENCE: given an Rhs, what is the type of the result?
    //
    // "Inference" here means: we look at the opcode and the types of the
    // operands, and deduce the output type.  For example:
    //   bin add %x %y  — if %x : i32 and %y : i32, result is i32
    //   bin lt  %x %y  — if %x : i32 and %y : i32, result is bool
    //   load %p        — if %p : ptr<i32>, result is i32
    //   un neg %x      — if %x : i32, result is i32
    //
    // infer_rhs_type() fills `out_type` with the inferred type and returns
    // true.  On a type mismatch it returns false and sets error_.
    bool infer_rhs_type(const Rhs& rhs, const std::string& block_label,
                        int line, ResIRType& out_type);

    // -----------------------------------------------------------------------
    // Type helpers
    // -----------------------------------------------------------------------

    // Look up a local in the symbol table; sets error_ and returns false if
    // the local is not declared.
    bool lookup(const std::string& name, int line,
                const std::string& ctx, ResIRType& out) const;

    // Return true if the type is numeric (i32 / i64 / u32 / f64).
    static bool is_numeric(const ResIRType& t);

    // Return true if the type is bool.
    static bool is_bool(const ResIRType& t);

    // Return true if two numeric types are the SAME primitive type.
    // ResIR does not allow mixing i32 and i64 in one binary op.
    static bool same_numeric(const ResIRType& a, const ResIRType& b);

    // Find an ExternType by its path string.  Returns nullptr if not found.
    const ExternType* find_extern_type(const std::string& path) const;

    // Find a field inside an ExternType by name.  Returns nullptr if absent.
    static const ExternField* find_field(const ExternType& et,
                                         const std::string& field_name);

    // Deep-copy a ResIRType (needed because unique_ptr cannot be copied).
    // We use this to store inferred pointer types in the symbol table.
    static ResIRType copy_type(const ResIRType& t);
};
