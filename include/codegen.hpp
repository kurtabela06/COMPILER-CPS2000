// =============================================================================
// codegen.hpp  —  C Code Generator for ResIR (Task 4)
// =============================================================================
//
// What does this pass do?
//   The validator (Task 3) proved the CFG is semantically correct.
//   The code generator (Task 4) TRANSLATES that CFG into a C source file.
//
//   "Translating" means: for every piece of the ResIR CFG we produce
//   a matching piece of C text.  Example:
//
//     ResIR block                      Generated C
//     ─────────────────────────────    ────────────────────────────────
//     bb0:                         →   bb0:
//       %d = bin sub %a, %b;       →       int32_t d = a - b;
//       cjump %is_neg, bb1, bb2;   →       if (is_neg) goto bb1; else goto bb2;
//
// WHY use "goto"?
//   ResIR is a "basic block" IR.  Each block ends in a jump, conditional
//   jump, or return.  These are explicit branches — there are no implicit
//   fall-throughs.  The cleanest way to preserve this structure in C is
//   "label and goto":
//
//     bb0:   ... statements ...   goto bb1;
//     bb1:   ... statements ...   if (c) goto bb2; else goto bb3;
//     bb2:   ... statements ...   return r;
//
//   An alternative would be structured if/while, but that requires
//   "structuralisation" (a complex algorithm).  goto is beginner-friendly
//   and maps directly 1-to-1 to the CFG.
//
// How to use:
//   CodeGen gen(prog);
//   std::string c_code = gen.emit();   // returns the full .c text
//   // — or —
//   gen.emit_to_file("generated.c");   // writes directly to a file
// =============================================================================

#pragma once
#include "cfg.hpp"    // Program, Function, Block, Statement, Rhs, ResIRType …

#include <string>
#include <sstream>    // std::ostringstream — an in-memory "file" we can write to

// =============================================================================
// CodeGen class
// =============================================================================
class CodeGen {
public:
    // Constructor: takes the fully-validated program.
    // We keep a const reference — the code generator only READS the CFG.
    explicit CodeGen(const Program& prog);

    // emit() — run the generator and return the entire C source as a string.
    // Call this after a successful Validator::validate().
    std::string emit();

    // emit_to_file() — convenience wrapper: calls emit() and writes to `path`.
    // Returns true on success, false if the file could not be opened.
    bool emit_to_file(const std::string& path);

private:
    // -----------------------------------------------------------------------
    // Data
    // -----------------------------------------------------------------------

    const Program&  prog_;   // the validated program
    const Function& fn_;     // shorthand for prog_.function

    // out_ is our "virtual file".
    // std::ostringstream works like std::cout but into an in-memory string.
    // Every time we call  out_ << "some text\n"  we are "emitting" that text
    // into the generated C file.
    std::ostringstream out_;

    // -----------------------------------------------------------------------
    // Top-level emitters
    // -----------------------------------------------------------------------

    // emit_headers()    — #include <stdint.h>, #include <stdbool.h>, …
    void emit_headers();

    // emit_extern_struct_decls() — forward-declare each extern type as a C struct
    void emit_extern_struct_decls();

    // emit_function()   — the whole C function (signature + body)
    void emit_function();

    // emit_locals()     — declare every local variable at the top of the body
    void emit_locals();

    // emit_block()      — one basic block (label + statements + terminator)
    void emit_block(const Block& blk);

    // emit_stmt()       — one statement (assignment or void call / store)
    void emit_stmt(const Statement& s);

    // emit_rhs()        — the right-hand side of a statement
    std::string emit_rhs(const Rhs& rhs);

    // emit_terminator() — jump / cjump / return
    void emit_terminator(const Terminator& t);

    // -----------------------------------------------------------------------
    // Type helpers
    // -----------------------------------------------------------------------

    // type_to_c()  — convert a ResIRType to its C equivalent string.
    //
    // Mapping table (from project spec):
    //   bool        →  bool            (from <stdbool.h>)
    //   i32         →  int32_t         (from <stdint.h>)
    //   i64         →  int64_t         (from <stdint.h>)
    //   u32         →  uint32_t        (from <stdint.h>)
    //   f64         →  double          (C standard type)
    //   ptr<T>      →  <C(T)>*         (pointer to the inner type)
    //   Path        →  path_to_c()     (e.g.  Custom::Struct::Point → Custom_Struct_Point)
    //
    // Nested pointer example:
    //   ptr<ptr<i32>>  →  int32_t**
    //   We recurse: type_to_c(ptr<i32>) + "*"  →  "int32_t*" + "*"  →  "int32_t**"
    std::string type_to_c(const ResIRType& t) const;

    // -----------------------------------------------------------------------
    // Path / name helpers
    // -----------------------------------------------------------------------

    // path_to_c() — replace "::" with "_" in a ResIR path.
    //   "Math::abs_diff"             →  "Math_abs_diff"
    //   "Custom::Struct::Point"      →  "Custom_Struct_Point"
    //
    // WHY replace?  C identifiers cannot contain "::".  Using "_" is the
    // conventional mapping when lowering namespaced names.
    static std::string path_to_c(const std::string& path);

    // local_name() — strip the "%" prefix from a ResIR local name.
    // The parser already strips "%" when storing locals, so this is a no-op
    // for names from the CFG.  Kept as a named helper for clarity.
    static std::string local_name(const std::string& name);

    // binop_str() — map a BinOp enum to its C operator token.
    static std::string binop_str(BinOp op);

    // unop_str()  — map a UnOp enum to its C operator token.
    static std::string unop_str(UnOp op);
};
