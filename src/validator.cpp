// =============================================================================
// validator.cpp  —  Semantic validation and type-checking for ResIR (Task 3)
// =============================================================================
//
// OVERVIEW OF THE VALIDATION PIPELINE
// =====================================
// validate() calls four phases in order:
//
//   Phase 1: build_symbol_table()
//     Read every function parameter and every local declaration.
//     Insert them into sym_ (the symbol table: name → type).
//     Error if any name appears twice.
//
//   Phase 2: collect_block_labels()
//     Walk every Block, record its label in block_labels_.
//     Error if any label appears more than once.
//
//   Phase 3: check_entry_label()
//     Make sure fn_.entry_label actually names a block we collected.
//
//   Phase 4: validate_all_blocks()
//     For each Block:
//       For each Statement:
//         - Check all operand locals exist in sym_.
//         - Call infer_rhs_type() to determine the result type.
//         - If there is a destination local, record its type in sym_.
//       For the Terminator:
//         - Check jump/cjump targets exist in block_labels_.
//         - Check cjump condition is bool.
//         - Check return value type matches the declared return type.
//
// TYPE INFERENCE
// =====================================
// Each Rhs opcode has a well-defined output type:
//
//   COPY:       output type = type of source local
//   CONST int:  output type = i32     (ResIR integer literals are i32)
//   CONST float:output type = f64
//   CONST bool: output type = bool
//   CONST null: output type = ptr<i32> (conservative; only used for ptr inits)
//   CAST:       output type = the declared target type (always valid)
//   UN neg:     output type = same numeric type as operand
//   UN not:     output type = bool (operand must be bool)
//   BIN arith:  output type = same numeric type as both operands
//   BIN compare:output type = bool (operands must be same numeric or bool type)
//   BIN logic:  output type = bool (operands must both be bool)
//   ADDR_OF:    output type = ptr<T>  where T = type of source
//   LOAD:       output type = T       where operand : ptr<T>
//   STORE:      no output (void operation — no dest expected)
//   MEMBER_PTR: output type = ptr<field_type>  (validated against extern type)
//   CALL:       output type = type of destination local as declared
//               (we trust the programmer; cross-function sigs not in scope)
// =============================================================================

#include "validator.hpp"
#include "cfg.hpp"

#include <sstream>    // std::ostringstream — for building error strings
#include <cassert>

// =============================================================================
// Constructor
// =============================================================================

Validator::Validator(const Program& prog)
    : prog_(prog)
    , fn_(prog.function)   // convenient shorthand
{}

// =============================================================================
// Public entry point
// =============================================================================

bool Validator::validate() {
    // Run each phase; stop on the first error.
    if (!build_symbol_table())   return false;
    if (!collect_block_labels()) return false;
    if (!check_entry_label())    return false;
    if (!validate_all_blocks())  return false;
    return true;
}

// =============================================================================
// Error helper
// =============================================================================

// sem_error() — record a semantic error and return false.
// The `line` argument is optional (0 means "no specific line known").
bool Validator::sem_error(const std::string& msg, int line) {
    std::ostringstream oss;
    oss << "Semantic error";
    if (line > 0) oss << " at line " << line;
    oss << ": " << msg;
    error_ = oss.str();
    return false;
}

// =============================================================================
// Phase 1 — Build the symbol table
// =============================================================================
//
// We pre-populate sym_ from TWO sources:
//   a) Function parameters:   e.g.  %a : i32   becomes  sym_["a"] = I32
//   b) Local declarations:    e.g.  %zero : i32 becomes sym_["zero"] = I32
//
// Why do this BEFORE walking the blocks?
//   ResIR locals are function-scoped and mutable — a local can be used before
//   it is assigned (it just has an undefined value, which the spec allows).
//   So we treat ALL declared locals as "in scope" for the entire function body.
//   We still track inferred types as we go so that assignments narrow the type.
//
// Why check for duplicates here?
//   If the same name appears twice in locals, the semantics are ambiguous.

bool Validator::build_symbol_table() {
    sym_.clear();

    // --- a) Function parameters ---
    for (const Param& p : fn_.params) {
        if (sym_.count(p.name)) {
            return sem_error("parameter `%" + p.name +
                             "` declared more than once");
        }
        // sym_[name] = copy of the type.
        // We call copy_type() because ResIRType uses unique_ptr internally
        // and cannot be simply copied with `=`.
        sym_[p.name] = copy_type(p.type);
    }

    // --- b) Local declarations ---
    for (const LocalDecl& ld : fn_.locals) {
        if (sym_.count(ld.name)) {
            // Could be a clash with a parameter OR another local.
            return sem_error("local `%" + ld.name +
                             "` declared more than once");
        }
        sym_[ld.name] = copy_type(ld.type);
    }

    return true;
}

// =============================================================================
// Phase 2 — Collect block labels and check uniqueness
// =============================================================================

bool Validator::collect_block_labels() {
    block_labels_.clear();

    for (const Block& blk : fn_.blocks) {
        if (block_labels_.count(blk.label)) {
            return sem_error("block label `" + blk.label +
                             "` is defined more than once", blk.line);
        }
        block_labels_[blk.label] = true;
    }

    return true;
}

// =============================================================================
// Phase 3 — Verify the entry label exists
// =============================================================================

bool Validator::check_entry_label() {
    if (!block_labels_.count(fn_.entry_label)) {
        return sem_error("entry label `" + fn_.entry_label +
                         "` does not name any block in function `" +
                         fn_.path + "`");
    }
    return true;
}

// =============================================================================
// Phase 4 — Walk every block and validate statements + terminators
// =============================================================================

bool Validator::validate_all_blocks() {
    for (const Block& blk : fn_.blocks) {
        if (!validate_block(blk)) return false;
    }
    return true;
}

bool Validator::validate_block(const Block& blk) {
    // Validate each statement in order.
    for (const Statement& s : blk.stmts) {
        if (!validate_stmt(s, blk.label)) return false;
    }
    // Validate the terminator.
    return validate_terminator(blk.term, blk.label);
}

// =============================================================================
// Statement validation
// =============================================================================
//
// For each statement we:
//   1. Call infer_rhs_type() — this checks operand declarations AND types.
//   2. If the statement has a destination local (%dest), store the inferred
//      type into sym_ so later statements can use it.

bool Validator::validate_stmt(const Statement& s, const std::string& block_label) {
    // Infer the result type of the RHS.
    ResIRType result_type;
    if (!infer_rhs_type(s.rhs, block_label, s.line, result_type)) {
        return false;
    }

    // STORE has no destination; make sure the IR agrees.
    if (s.rhs.kind == RhsKind::STORE && s.has_dest) {
        return sem_error(
            "in block `" + block_label + "`: `store` cannot have a destination",
            s.line);
    }

    // If there IS a destination local, record (or confirm) its type.
    if (s.has_dest) {
        // Check the destination was actually declared (INVARIANT: every local
        // that receives an assignment must be in the locals/params table).
        if (!sym_.count(s.dest)) {
            return sem_error(
                "in block `" + block_label + "`: destination local `%" +
                s.dest + "` was not declared in `locals`", s.line);
        }

        // If the local already has a type (from the declaration or a prior
        // assignment), and this new assignment would produce a DIFFERENT type,
        // that is a type error.
        //
        // We use the DECLARED type (from Phase 1) as the authoritative type
        // and check that the inferred RHS type is compatible.
        const ResIRType& declared = sym_.at(s.dest);

        // Special rule for CALL: we don't know the callee's return type, so
        // we trust the declared type and skip the mismatch check.
        if (s.rhs.kind != RhsKind::CALL && declared != result_type) {
            return sem_error(
                "in block `" + block_label + "`: type mismatch for `%" +
                s.dest + "` — declared as `" + declared.to_string() +
                "` but assigned expression has type `" +
                result_type.to_string() + "`", s.line);
        }
    }

    return true;
}

// =============================================================================
// Terminator validation
// =============================================================================

bool Validator::validate_terminator(const Terminator& t,
                                     const std::string& block_label) {
    switch (t.kind) {

        // -------------------------------------------------------------------
        // JUMP:  just verify the target label exists
        // -------------------------------------------------------------------
        case TermKind::JUMP:
            if (!block_labels_.count(t.label1)) {
                return sem_error(
                    "in block `" + block_label + "`: jump target `" +
                    t.label1 + "` does not exist", t.line);
            }
            return true;

        // -------------------------------------------------------------------
        // CJUMP:  verify both labels exist AND that the condition is bool
        // -------------------------------------------------------------------
        case TermKind::CJUMP: {
            // Check the condition local is declared.
            ResIRType cond_type;
            if (!lookup(t.cond, t.line,
                        "cjump condition in block `" + block_label + "`",
                        cond_type)) {
                return false;
            }
            // The condition MUST be bool — ResIR requires this explicitly.
            if (!is_bool(cond_type)) {
                return sem_error(
                    "in block `" + block_label + "`: `cjump` condition `%" +
                    t.cond + "` has type `" + cond_type.to_string() +
                    "` but must be `bool`", t.line);
            }
            // Verify the true-branch label.
            if (!block_labels_.count(t.label1)) {
                return sem_error(
                    "in block `" + block_label + "`: cjump true-target `" +
                    t.label1 + "` does not exist", t.line);
            }
            // Verify the false-branch label.
            if (!block_labels_.count(t.label2)) {
                return sem_error(
                    "in block `" + block_label + "`: cjump false-target `" +
                    t.label2 + "` does not exist", t.line);
            }
            return true;
        }

        // -------------------------------------------------------------------
        // RETURN:  check the value (if any) matches the declared return type
        // -------------------------------------------------------------------
        case TermKind::RETURN: {
            if (fn_.returns_void) {
                // Void function — no return value allowed.
                if (t.has_ret_val) {
                    return sem_error(
                        "in block `" + block_label +
                        "`: `return` with a value in a void function", t.line);
                }
            } else {
                // Non-void function — a return value is REQUIRED.
                if (!t.has_ret_val) {
                    return sem_error(
                        "in block `" + block_label +
                        "`: `return` with no value in a non-void function "
                        "(expected `" + fn_.return_type.to_string() + "`)",
                        t.line);
                }
                // Look up the returned local's type.
                ResIRType ret_type;
                if (!lookup(t.ret_local, t.line,
                            "return value in block `" + block_label + "`",
                            ret_type)) {
                    return false;
                }
                // It must match the declared return type.
                if (ret_type != fn_.return_type) {
                    return sem_error(
                        "in block `" + block_label + "`: return type mismatch"
                        " — function declared to return `" +
                        fn_.return_type.to_string() +
                        "` but `%" + t.ret_local +
                        "` has type `" + ret_type.to_string() + "`", t.line);
                }
            }
            return true;
        }
    }
    return true; // unreachable but silences compiler warning
}

// =============================================================================
// RHS type inference  (the core of the type checker)
// =============================================================================
//
// For each opcode we:
//   a) Look up each operand's type in sym_  (undeclared locals → error).
//   b) Check the operand types are compatible with the opcode.
//   c) Set out_type to the inferred result type.

bool Validator::infer_rhs_type(const Rhs& rhs,
                                const std::string& block_label,
                                int line,
                                ResIRType& out_type) {
    // Helper lambda: look up a local and fill a ResIRType.
    // We capture `this`, `block_label`, and `line` from the enclosing scope.
    //
    // A "lambda" is an inline anonymous function.  Syntax:
    //   [captures](params) -> return_type { body }
    // The `[&]` means "capture everything by reference".
    auto get = [&](const std::string& name, ResIRType& t) -> bool {
        return lookup(name, line, "in block `" + block_label + "`", t);
    };

    switch (rhs.kind) {

        // -------------------------------------------------------------------
        // COPY:  %dst = %src
        // Result type = type of %src
        // -------------------------------------------------------------------
        case RhsKind::COPY: {
            ResIRType src_type;
            if (!get(rhs.op1, src_type)) return false;
            out_type = copy_type(src_type);
            return true;
        }

        // -------------------------------------------------------------------
        // CONST:  %dst = const <literal>
        // Result type depends on the literal kind:
        //   integer → i32    (ResIR integer literals are always i32)
        //   float   → f64
        //   bool    → bool
        //   null    → ptr<i32>  (null is a typed null pointer; we use ptr<i32>
        //                        as a conservative "generic" pointer type)
        // -------------------------------------------------------------------
        case RhsKind::CONST: {
            switch (rhs.literal.kind) {
                case LiteralKind::INT:      out_type = ResIRType(TypeKind::I32);   break;
                case LiteralKind::FLOAT:    out_type = ResIRType(TypeKind::F64);   break;
                case LiteralKind::BOOL:     out_type = ResIRType(TypeKind::BOOL);  break;
                case LiteralKind::NULL_LIT:
                    // null is a null pointer.  We give it type ptr<i32> as a
                    // placeholder; the declared type of the destination local
                    // is the actual type (checked separately in validate_stmt).
                    out_type = ResIRType(TypeKind::PTR,
                                std::make_unique<ResIRType>(TypeKind::I32));
                    break;
            }
            return true;
        }

        // -------------------------------------------------------------------
        // CAST:  %dst = cast %src to <type>
        // The cast is always valid by design (ResIR trusts the programmer).
        // Result type = the declared target type, whatever it is.
        // -------------------------------------------------------------------
        case RhsKind::CAST: {
            // We still verify the source local is declared.
            ResIRType src_type;
            if (!get(rhs.op1, src_type)) return false;
            // Result is exactly the declared cast target — a deep copy.
            out_type = copy_type(rhs.cast_type);
            return true;
        }

        // -------------------------------------------------------------------
        // UN:  %dst = un <op> %src
        //
        // neg: operand must be numeric → result is the SAME numeric type
        // not: operand must be bool   → result is bool
        // -------------------------------------------------------------------
        case RhsKind::UN: {
            ResIRType operand_type;
            if (!get(rhs.op1, operand_type)) return false;

            if (rhs.un_op == UnOp::NEG) {
                if (!is_numeric(operand_type)) {
                    return sem_error(
                        "in block `" + block_label + "`: `un neg` requires a "
                        "numeric operand but `%" + rhs.op1 + "` has type `" +
                        operand_type.to_string() + "`", line);
                }
                // Result type is the same numeric type as the operand.
                out_type = copy_type(operand_type);
            } else { // UnOp::NOT
                if (!is_bool(operand_type)) {
                    return sem_error(
                        "in block `" + block_label + "`: `un not` requires a "
                        "bool operand but `%" + rhs.op1 + "` has type `" +
                        operand_type.to_string() + "`", line);
                }
                out_type = ResIRType(TypeKind::BOOL);
            }
            return true;
        }

        // -------------------------------------------------------------------
        // BIN:  %dst = bin <op> %lhs, %rhs
        //
        // We split binary operations into three families:
        //
        //   ARITHMETIC (add/sub/mul/div/mod):
        //     Both operands must be the SAME numeric type.
        //     Result is that same numeric type.
        //
        //   COMPARISON (eq/ne/lt/le/gt/ge):
        //     Both operands must be the same numeric type OR both bool.
        //     Result is always bool.
        //
        //   LOGICAL (and/or):
        //     Both operands must be bool.
        //     Result is bool.
        // -------------------------------------------------------------------
        case RhsKind::BIN: {
            ResIRType lhs_type, rhs_type;
            if (!get(rhs.op1, lhs_type)) return false;
            if (!get(rhs.op2, rhs_type)) return false;

            switch (rhs.bin_op) {

                // ---- Arithmetic ----
                case BinOp::ADD: case BinOp::SUB:
                case BinOp::MUL: case BinOp::DIV: case BinOp::MOD: {
                    if (!is_numeric(lhs_type) || !same_numeric(lhs_type, rhs_type)) {
                        return sem_error(
                            "in block `" + block_label + "`: arithmetic `bin` "
                            "requires two operands of the same numeric type, "
                            "but got `" + lhs_type.to_string() +
                            "` and `" + rhs_type.to_string() + "`", line);
                    }
                    out_type = copy_type(lhs_type); // same as rhs_type
                    return true;
                }

                // ---- Comparison ----
                case BinOp::EQ: case BinOp::NE:
                case BinOp::LT: case BinOp::LE:
                case BinOp::GT: case BinOp::GE: {
                    // Both must be the same numeric type OR both bool.
                    bool numeric_ok = is_numeric(lhs_type) && same_numeric(lhs_type, rhs_type);
                    bool bool_ok    = is_bool(lhs_type) && is_bool(rhs_type);
                    if (!numeric_ok && !bool_ok) {
                        return sem_error(
                            "in block `" + block_label + "`: comparison `bin` "
                            "requires operands of the same numeric or bool type, "
                            "but got `" + lhs_type.to_string() +
                            "` and `" + rhs_type.to_string() + "`", line);
                    }
                    out_type = ResIRType(TypeKind::BOOL); // comparisons produce bool
                    return true;
                }

                // ---- Logical ----
                case BinOp::AND: case BinOp::OR: {
                    if (!is_bool(lhs_type) || !is_bool(rhs_type)) {
                        return sem_error(
                            "in block `" + block_label + "`: logical `bin` "
                            "(and/or) requires bool operands, "
                            "but got `" + lhs_type.to_string() +
                            "` and `" + rhs_type.to_string() + "`", line);
                    }
                    out_type = ResIRType(TypeKind::BOOL);
                    return true;
                }
            }
            return true; // all cases covered above
        }

        // -------------------------------------------------------------------
        // ADDR_OF:  %dst = addr_of %src
        // If %src : T, then result : ptr<T>
        // -------------------------------------------------------------------
        case RhsKind::ADDR_OF: {
            ResIRType src_type;
            if (!get(rhs.op1, src_type)) return false;
            // Build ptr<src_type> dynamically.
            out_type = ResIRType(TypeKind::PTR,
                                 std::make_unique<ResIRType>(copy_type(src_type)));
            return true;
        }

        // -------------------------------------------------------------------
        // LOAD:  %dst = load %ptr
        //
        // %ptr must have type ptr<T> for some T.
        // Result type is T (we "dereference" the pointer type).
        // -------------------------------------------------------------------
        case RhsKind::LOAD: {
            ResIRType ptr_type;
            if (!get(rhs.op1, ptr_type)) return false;

            if (ptr_type.kind != TypeKind::PTR || !ptr_type.inner) {
                return sem_error(
                    "in block `" + block_label + "`: `load` requires a pointer "
                    "operand but `%" + rhs.op1 + "` has type `" +
                    ptr_type.to_string() + "`", line);
            }
            // The result is the INNER type (what the pointer points to).
            out_type = copy_type(*ptr_type.inner);
            return true;
        }

        // -------------------------------------------------------------------
        // STORE:  store %ptr, %val
        //
        // %ptr must be ptr<T> and %val must be T.
        // There is NO result (store is a side-effect-only instruction).
        // We return a dummy I32 type in out_type; validate_stmt checks
        // that store has no destination.
        // -------------------------------------------------------------------
        case RhsKind::STORE: {
            ResIRType ptr_type, val_type;
            if (!get(rhs.op1, ptr_type)) return false;
            if (!get(rhs.op2, val_type)) return false;

            if (ptr_type.kind != TypeKind::PTR || !ptr_type.inner) {
                return sem_error(
                    "in block `" + block_label + "`: `store` first operand "
                    "`%" + rhs.op1 + "` must be a pointer but has type `" +
                    ptr_type.to_string() + "`", line);
            }
            // The value being stored must match what the pointer points to.
            if (*ptr_type.inner != val_type) {
                return sem_error(
                    "in block `" + block_label + "`: `store` type mismatch — "
                    "pointer `%" + rhs.op1 + "` points to `" +
                    ptr_type.inner->to_string() +
                    "` but value `%" + rhs.op2 + "` has type `" +
                    val_type.to_string() + "`", line);
            }
            out_type = ResIRType(TypeKind::I32); // dummy — store has no result
            return true;
        }

        // -------------------------------------------------------------------
        // MEMBER_PTR:  %dst = member_ptr %ptr, field
        //
        // This is the most complex check.  Steps:
        //   a) %ptr must be ptr<Path> for some Path.
        //   b) Path must be declared in an `extern type` block.
        //   c) `field` must exist in that extern type.
        //   d) Result type is ptr<FieldType>.
        // -------------------------------------------------------------------
        case RhsKind::MEMBER_PTR: {
            ResIRType ptr_type;
            if (!get(rhs.op1, ptr_type)) return false;

            // a) The operand must be a pointer.
            if (ptr_type.kind != TypeKind::PTR || !ptr_type.inner) {
                return sem_error(
                    "in block `" + block_label + "`: `member_ptr` first "
                    "operand `%" + rhs.op1 + "` must be a pointer but has "
                    "type `" + ptr_type.to_string() + "`", line);
            }

            // The inner type (what the pointer points to) must be a PATH
            // (user-defined struct type).
            const ResIRType& inner = *ptr_type.inner;
            if (inner.kind != TypeKind::PATH) {
                return sem_error(
                    "in block `" + block_label + "`: `member_ptr` operand "
                    "`%" + rhs.op1 + "` must be ptr<StructType> but the "
                    "inner type is `" + inner.to_string() +
                    "` (not a struct path)", line);
            }

            // b) Look up the extern type declaration.
            const ExternType* et = find_extern_type(inner.path);
            if (!et) {
                return sem_error(
                    "in block `" + block_label + "`: `member_ptr` type `" +
                    inner.path + "` was not declared with `extern type`", line);
            }

            // c) Check the field exists.
            const ExternField* ef = find_field(*et, rhs.field);
            if (!ef) {
                return sem_error(
                    "in block `" + block_label + "`: struct `" + inner.path +
                    "` has no field named `" + rhs.field + "`", line);
            }

            // d) Result type is ptr<field_type>.
            out_type = ResIRType(TypeKind::PTR,
                                 std::make_unique<ResIRType>(copy_type(ef->type)));
            return true;
        }

        // -------------------------------------------------------------------
        // CALL:  %dst = call Path(args…)
        //
        // We do NOT check argument types — the callee's signature is not
        // available in a single-file compilation unit (spec note: "its
        // signature is assumed to already be in scope").  We trust the
        // declared type of the destination local (already in sym_).
        // We DO verify that every argument local is declared.
        // -------------------------------------------------------------------
        case RhsKind::CALL: {
            for (const std::string& arg : rhs.call_args) {
                ResIRType arg_type;
                if (!get(arg, arg_type)) return false;
            }
            // Result type = whatever the destination local was declared as.
            // This is handled in validate_stmt (we skip the mismatch check
            // for CALL).  We output I32 as a placeholder here; it's ignored.
            out_type = ResIRType(TypeKind::I32); // placeholder
            return true;
        }
    }

    // Unreachable: all RhsKind cases are covered.
    return sem_error("internal error: unhandled Rhs kind", line);
}

// =============================================================================
// Type helpers
// =============================================================================

// lookup() — find a local in the symbol table.
// `ctx` is included in the error message for context (e.g. "in block `bb0`").
bool Validator::lookup(const std::string& name, int line,
                       const std::string& ctx, ResIRType& out) const {
    auto it = sym_.find(name);
    if (it == sym_.end()) {
        // const_cast is needed because sem_error modifies error_, but `this`
        // is const in this method.  We cast away const only for the error
        // string field.  This is safe because error_ is not part of the
        // logical "const" guarantee.
        const_cast<Validator*>(this)->error_ =
            "Semantic error: " + ctx + ": local `%" + name +
            "` used before declaration (not in `locals` or params)";
        if (line > 0) {
            const_cast<Validator*>(this)->error_ +=
                " at line " + std::to_string(line);
        }
        return false;
    }
    out = copy_type(it->second);
    return true;
}

// is_numeric() — true for i32, i64, u32, f64.
bool Validator::is_numeric(const ResIRType& t) {
    return t.kind == TypeKind::I32 ||
           t.kind == TypeKind::I64 ||
           t.kind == TypeKind::U32 ||
           t.kind == TypeKind::F64;
}

// is_bool() — true only for bool.
bool Validator::is_bool(const ResIRType& t) {
    return t.kind == TypeKind::BOOL;
}

// same_numeric() — true if both types are the exact same primitive numeric type.
// ResIR requires strict same-type arithmetic (no implicit promotions).
bool Validator::same_numeric(const ResIRType& a, const ResIRType& b) {
    if (!is_numeric(a) || !is_numeric(b)) return false;
    return a.kind == b.kind; // I32==I32, F64==F64, etc.
}

// find_extern_type() — look up an ExternType by path string in prog_.
const ExternType* Validator::find_extern_type(const std::string& path) const {
    for (const ExternType& et : prog_.extern_types) {
        if (et.path == path) return &et;
    }
    return nullptr;
}

// find_field() — look up a named field in an ExternType.
const ExternField* Validator::find_field(const ExternType& et,
                                          const std::string& field_name) {
    for (const ExternField& f : et.fields) {
        if (f.name == field_name) return &f;
    }
    return nullptr;
}

// =============================================================================
// copy_type() — deep-copy a ResIRType (unique_ptr cannot be copy-assigned)
// =============================================================================
//
// Why do we need this?
//   std::unique_ptr forbids copying because only one owner is allowed.
//   But we need to store types in the symbol table (std::map), which COPIES
//   its values.  So we do a manual recursive deep-copy:
//   - For PTR types, we allocate a new unique_ptr and copy the inner type.
//   - For everything else, a plain struct copy works.
ResIRType Validator::copy_type(const ResIRType& t) {
    if (t.kind == TypeKind::PTR) {
        // Recursively copy the inner type.
        auto inner_copy = std::make_unique<ResIRType>(
            t.inner ? copy_type(*t.inner) : ResIRType(TypeKind::I32)
        );
        return ResIRType(TypeKind::PTR, std::move(inner_copy));
    }
    // For non-pointer types (primitive or PATH), just copy the struct fields.
    ResIRType copy;
    copy.kind = t.kind;
    copy.path = t.path;
    // copy.inner stays nullptr for non-PTR types.
    return copy;
}
