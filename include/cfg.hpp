// =============================================================================
// cfg.hpp  —  In-memory Control Flow Graph (CFG) representation for ResIR
// =============================================================================
//
// What is a CFG?
//   A Control Flow Graph is how a compiler stores a parsed function in memory.
//   Instead of keeping raw text, we store structured data:
//     - The function name, parameters, and return type
//     - A table of all local variables and their types
//     - A list of Basic Blocks, each containing statements and one terminator
//
//   Think of each Basic Block as one "paragraph" of straight-line code.
//   Branches (cjump) and jumps between paragraphs are explicit.
//
// How this file is organised:
//   1. ResIRType      — represents a ResIR type (i32, ptr<bool>, Custom::Foo…)
//   2. Literal        — a constant value (integer, float, true/false/null)
//   3. Rhs            — the right-hand side of an assignment (all opcodes)
//   4. Statement      — one statement: optional destination + Rhs
//   5. Terminator     — the final instruction of a block (jump/cjump/return)
//   6. Block          — a basic block: label + statements + terminator
//   7. Param          — a function parameter (%name : type)
//   8. ExternField    — one field in an extern type declaration
//   9. ExternType     — a full extern type block
//  10. Function       — the root object containing everything
//  11. Program        — the top-level container (extern types + function)
// =============================================================================

#pragma once
#include <string>    // std::string
#include <vector>    // std::vector — growable list
#include <memory>    // std::unique_ptr — owning pointer (explained below)

// =============================================================================
// 1. ResIRType
// =============================================================================
//
// What is a type in ResIR?
//   Type ::= PrimType | Path | 'ptr' '<' Type '>'
//
// Examples:
//   i32                      → TypeKind::I32
//   bool                     → TypeKind::BOOL
//   Custom::Struct::Point    → TypeKind::PATH, path="Custom::Struct::Point"
//   ptr<i32>                 → TypeKind::PTR,  inner points to I32 type
//   ptr<Custom::Struct::Point> → TypeKind::PTR, inner points to PATH type

enum class TypeKind {
    BOOL,   // `bool`
    I32,    // `i32`
    I64,    // `i64`
    U32,    // `u32`
    F64,    // `f64`
    PTR,    // `ptr<...>` — inner type stored in `inner`
    PATH,   // A user-defined path like `Custom::Struct::Point`
};

// Forward declaration: ResIRType refers to itself (for ptr<Type>).
// A "forward declaration" tells the compiler "this struct exists"
// before we define it fully — needed because ResIRType contains a
// pointer to itself.
struct ResIRType;

struct ResIRType {
    TypeKind kind;

    // For TypeKind::PATH: the full path as a single string, e.g.
    //   "Custom::Struct::Point"
    std::string path;

    // For TypeKind::PTR: the type being pointed to.
    //
    // std::unique_ptr<T> is an "owning pointer":
    //   - It holds a pointer to a heap-allocated T.
    //   - When the unique_ptr is destroyed, it automatically frees the memory.
    //   - Only ONE unique_ptr can own a given object at a time (hence "unique").
    //   - This is safer than a raw pointer (T*) because there are no leaks.
    //
    // We must use a pointer here (not a plain ResIRType member) because
    // a struct cannot contain itself directly — that would be infinitely large.
    std::unique_ptr<ResIRType> inner; // only valid when kind == PTR

    // -----------------------------------------------------------------------
    // Constructors — make it easy to build type values
    // -----------------------------------------------------------------------

    // Build a primitive type (BOOL, I32, I64, U32, F64)
    explicit ResIRType(TypeKind k) : kind(k) {}

    // Build a PATH type from a string
    ResIRType(TypeKind k, std::string p) : kind(k), path(std::move(p)) {}

    // Build a PTR type wrapping another type.
    // std::move() transfers ownership of `i` into this struct's `inner` field.
    ResIRType(TypeKind k, std::unique_ptr<ResIRType> i)
        : kind(k), inner(std::move(i)) {}

    // Default constructor (needed for containers like std::vector).
    ResIRType() : kind(TypeKind::I32) {}

    // to_string() — produce a human-readable representation of the type.
    // Useful for error messages and debug printing.
    std::string to_string() const {
        switch (kind) {
            case TypeKind::BOOL: return "bool";
            case TypeKind::I32:  return "i32";
            case TypeKind::I64:  return "i64";
            case TypeKind::U32:  return "u32";
            case TypeKind::F64:  return "f64";
            case TypeKind::PATH: return path;
            case TypeKind::PTR:
                return "ptr<" + (inner ? inner->to_string() : "?") + ">";
        }
        return "?";
    }

    // operator== — needed for type-checking in Task 3.
    // Two types are equal if they have the same structure.
    bool operator==(const ResIRType& o) const {
        if (kind != o.kind) return false;
        if (kind == TypeKind::PATH) return path == o.path;
        if (kind == TypeKind::PTR) {
            if (!inner || !o.inner) return !inner && !o.inner;
            return *inner == *o.inner;
        }
        return true; // primitive types only need matching kind
    }
    bool operator!=(const ResIRType& o) const { return !(*this == o); }
};

// =============================================================================
// 2. Literal
// =============================================================================
// A compile-time constant value appearing after `const`.
//
// Examples:
//   const 42       → LiteralKind::INT,   int_val=42
//   const 3.14     → LiteralKind::FLOAT, float_val=3.14
//   const true     → LiteralKind::BOOL,  bool_val=true
//   const null     → LiteralKind::NULL_LIT

enum class LiteralKind { INT, FLOAT, BOOL, NULL_LIT };

struct Literal {
    LiteralKind kind;
    int64_t  int_val   = 0;
    double   float_val = 0.0;
    bool     bool_val  = false;
};

// =============================================================================
// 3. Rhs (Right-Hand Side)
// =============================================================================
// The right-hand side of a statement.  Covers all ResIR opcodes.
//
// EBNF:
//   Rhs ::= Local
//         | 'const' Literal
//         | 'cast' Local 'to' Type
//         | 'un' UnOp Local
//         | 'bin' BinOp Local ',' Local
//         | 'addr_of' Local
//         | 'member_ptr' Local ',' Ident
//         | 'load' Local
//         | 'store' Local ',' Local
//         | 'call' Path '(' [Args] ')'

enum class RhsKind {
    COPY,        // %dst = %src               (just a local copy)
    CONST,       // %dst = const <literal>
    CAST,        // %dst = cast %src to <type>
    UN,          // %dst = un <op> %src
    BIN,         // %dst = bin <op> %lhs, %rhs
    ADDR_OF,     // %dst = addr_of %src
    MEMBER_PTR,  // %dst = member_ptr %ptr, field
    LOAD,        // %dst = load %ptr
    STORE,       // store %ptr, %val           (no destination)
    CALL,        // %dst = call Path(args...)
};

// UnOp and BinOp — the operator kinds for `un` and `bin` instructions.
enum class UnOp  { NEG, NOT };
enum class BinOp { ADD, SUB, MUL, DIV, MOD, EQ, NE, LT, LE, GT, GE, AND, OR };

struct Rhs {
    RhsKind kind;

    // Operand locals — which fields are used depends on `kind`:
    //   COPY:       op1 = source local
    //   UN:         op1 = operand
    //   BIN:        op1 = left,  op2 = right
    //   ADDR_OF:    op1 = source local
    //   MEMBER_PTR: op1 = pointer local,  field = field name
    //   LOAD:       op1 = pointer local
    //   STORE:      op1 = pointer local,  op2 = value local
    //   CAST:       op1 = source local,   cast_type = target type
    //   CALL:       call_path = function path, call_args = argument list
    //   CONST:      literal   = the constant value
    std::string op1;           // first operand (local name, without %)
    std::string op2;           // second operand (local name, without %)
    std::string field;         // for MEMBER_PTR: the field name

    UnOp  un_op  = UnOp::NEG;  // for UN
    BinOp bin_op = BinOp::ADD; // for BIN

    ResIRType cast_type;       // for CAST: the target type

    Literal   literal;         // for CONST

    std::string        call_path; // for CALL: the function path (:: joined)
    std::vector<std::string> call_args; // for CALL: argument local names
};

// =============================================================================
// 4. Statement
// =============================================================================
// One statement inside a basic block.
//
// EBNF:  Stmt ::= [ Local '=' ] Rhs
//
// If there is a destination local, `dest` is set and `has_dest` is true.
// `store` and `call` (void return) produce statements with no destination.

struct Statement {
    bool        has_dest = false; // is there a destination local?
    std::string dest;             // destination local name (without %)
    Rhs         rhs;              // the right-hand side
    int         line = 0;         // source line (for error messages)
};

// =============================================================================
// 5. Terminator
// =============================================================================
// The last instruction of every basic block.  Exactly one of:
//   jump  <label>
//   cjump <cond_local>, <true_label>, <false_label>
//   return [<local>]
//
// EBNF:
//   Term ::= 'jump' Label
//           | 'cjump' Local ',' Label ',' Label
//           | 'return' [ Local ]

enum class TermKind { JUMP, CJUMP, RETURN };

struct Terminator {
    TermKind kind;

    std::string label1;       // JUMP: target; CJUMP: true-branch target
    std::string label2;       // CJUMP: false-branch target
    std::string cond;         // CJUMP: condition local name
    std::string ret_local;    // RETURN: returned local (empty = void return)
    bool        has_ret_val = false; // RETURN: is there a value to return?
    int         line        = 0;    // source line
};

// =============================================================================
// 6. Block
// =============================================================================
// One basic block: a label, a list of statements, and one terminator.
//
// EBNF:  Block ::= Label ':' { Stmt ';' } Term ';'

struct Block {
    std::string          label;       // e.g. "bb0", "bb1"
    std::vector<Statement> stmts;    // zero or more statements
    Terminator           term;        // exactly one terminator
    int                  line = 0;   // source line of the label
};

// =============================================================================
// 7. Param
// =============================================================================
// A function parameter:  %name : type
//
// EBNF:  Param ::= Local ':' Type

struct Param {
    std::string name;   // local name (without %)
    ResIRType   type;
};

// =============================================================================
// 8 & 9. ExternType
// =============================================================================
// An `extern type` declaration listing the fields of a custom struct.
//
// EBNF:
//   ExternType ::= 'extern' 'type' Path '{' { Field ';' } '}'
//   Field      ::= Ident ':' Type

struct ExternField {
    std::string name;  // field name
    ResIRType   type;  // field type
};

struct ExternType {
    std::string              path;   // e.g. "Custom::Struct::Point"
    std::vector<ExternField> fields;
};

// =============================================================================
// 10. Function
// =============================================================================
// The parsed function — the root of everything the parser produces.
//
// EBNF:
//   Function ::= 'function' Path '(' [Params] ')' '->' RetType '{'
//                  'locals' '{' { Local ':' Type ';' } '}'
//                  'entry' Label ';'
//                  { Block }
//                '}'

struct LocalDecl {
    std::string name;  // local name (without %)
    ResIRType   type;
};

struct Function {
    std::string            path;         // function name path (:: joined)
    std::vector<Param>     params;       // parameter list
    bool                   returns_void; // true if return type is `void`
    ResIRType              return_type;  // only valid if !returns_void
    std::vector<LocalDecl> locals;       // locals table
    std::string            entry_label;  // which block is the entry point
    std::vector<Block>     blocks;       // all basic blocks, in order
};

// =============================================================================
// 11. Program
// =============================================================================
// The top-level container: zero or more extern types, then one function.
//
// EBNF:  Program ::= { ExternType } Function

struct Program {
    std::vector<ExternType> extern_types;
    Function                function;
};

// =============================================================================
// Helper: pretty-print a Program to stdout (for debugging Task 2)
// =============================================================================
// Declared here, defined in cfg.cpp.
void print_program(const Program& prog);
