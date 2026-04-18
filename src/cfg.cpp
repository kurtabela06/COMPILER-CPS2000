// =============================================================================
// cfg.cpp  —  Pretty-printer for the in-memory CFG / Program struct
// =============================================================================
//
// This file defines print_program(), which walks the parsed Program tree and
// prints it in a human-readable form.  It is purely for debugging Task 2 —
// you can run `./resir tests/basic_math.resir` and see the full CFG structure
// that the parser built.
// =============================================================================

#include "cfg.hpp"
#include <iostream>   // std::cout
#include <string>

// ---------------------------------------------------------------------------
// Helper: indented output
// ---------------------------------------------------------------------------
static void println(int indent, const std::string& s) {
    std::cout << std::string(static_cast<size_t>(indent * 2), ' ') << s << "\n";
}

// ---------------------------------------------------------------------------
// binop_name / unop_name — convert enum to string for display
// ---------------------------------------------------------------------------
static const char* binop_name(BinOp op) {
    switch (op) {
        case BinOp::ADD: return "add"; case BinOp::SUB: return "sub";
        case BinOp::MUL: return "mul"; case BinOp::DIV: return "div";
        case BinOp::MOD: return "mod"; case BinOp::EQ:  return "eq";
        case BinOp::NE:  return "ne";  case BinOp::LT:  return "lt";
        case BinOp::LE:  return "le";  case BinOp::GT:  return "gt";
        case BinOp::GE:  return "ge";  case BinOp::AND: return "and";
        case BinOp::OR:  return "or";
    }
    return "?";
}

static const char* unop_name(UnOp op) {
    return op == UnOp::NEG ? "neg" : "not";
}

// ---------------------------------------------------------------------------
// print_rhs — print one Rhs value
// ---------------------------------------------------------------------------
static void print_rhs(const Rhs& rhs) {
    switch (rhs.kind) {
        case RhsKind::COPY:
            std::cout << "%" << rhs.op1;
            break;
        case RhsKind::CONST: {
            std::cout << "const ";
            switch (rhs.literal.kind) {
                case LiteralKind::INT:      std::cout << rhs.literal.int_val;   break;
                case LiteralKind::FLOAT:    std::cout << rhs.literal.float_val; break;
                case LiteralKind::BOOL:     std::cout << (rhs.literal.bool_val ? "true" : "false"); break;
                case LiteralKind::NULL_LIT: std::cout << "null"; break;
            }
            break;
        }
        case RhsKind::CAST:
            std::cout << "cast %" << rhs.op1 << " to " << rhs.cast_type.to_string();
            break;
        case RhsKind::UN:
            std::cout << "un " << unop_name(rhs.un_op) << " %" << rhs.op1;
            break;
        case RhsKind::BIN:
            std::cout << "bin " << binop_name(rhs.bin_op)
                      << " %" << rhs.op1 << ", %" << rhs.op2;
            break;
        case RhsKind::ADDR_OF:
            std::cout << "addr_of %" << rhs.op1;
            break;
        case RhsKind::MEMBER_PTR:
            std::cout << "member_ptr %" << rhs.op1 << ", " << rhs.field;
            break;
        case RhsKind::LOAD:
            std::cout << "load %" << rhs.op1;
            break;
        case RhsKind::STORE:
            std::cout << "store %" << rhs.op1 << ", %" << rhs.op2;
            break;
        case RhsKind::CALL: {
            std::cout << "call " << rhs.call_path << "(";
            for (size_t i = 0; i < rhs.call_args.size(); ++i) {
                if (i > 0) std::cout << ", ";
                std::cout << "%" << rhs.call_args[i];
            }
            std::cout << ")";
            break;
        }
    }
}

// ---------------------------------------------------------------------------
// print_program — the main debug-print function
// ---------------------------------------------------------------------------
void print_program(const Program& prog) {
    // --- Extern types -------------------------------------------------------
    for (const ExternType& et : prog.extern_types) {
        std::cout << "extern type " << et.path << " {\n";
        for (const ExternField& f : et.fields) {
            println(1, f.name + " : " + f.type.to_string());
        }
        std::cout << "}\n\n";
    }

    // --- Function header ----------------------------------------------------
    const Function& fn = prog.function;
    std::cout << "function " << fn.path << "(";
    for (size_t i = 0; i < fn.params.size(); ++i) {
        if (i > 0) std::cout << ", ";
        std::cout << "%" << fn.params[i].name << ": " << fn.params[i].type.to_string();
    }
    std::cout << ") -> "
              << (fn.returns_void ? "void" : fn.return_type.to_string())
              << " {\n";

    // --- Locals -------------------------------------------------------------
    println(1, "locals {");
    for (const LocalDecl& loc : fn.locals) {
        println(2, "%" + loc.name + " : " + loc.type.to_string() + ";");
    }
    println(1, "}");
    println(1, "entry " + fn.entry_label + ";");
    std::cout << "\n";

    // --- Blocks -------------------------------------------------------------
    for (const Block& blk : fn.blocks) {
        std::cout << "  " << blk.label << ":\n";

        // Statements
        for (const Statement& s : blk.stmts) {
            std::cout << "    ";
            if (s.has_dest) std::cout << "%" << s.dest << " = ";
            print_rhs(s.rhs);
            std::cout << ";\n";
        }

        // Terminator
        std::cout << "    ";
        switch (blk.term.kind) {
            case TermKind::JUMP:
                std::cout << "jump " << blk.term.label1;
                break;
            case TermKind::CJUMP:
                std::cout << "cjump %" << blk.term.cond
                          << ", " << blk.term.label1
                          << ", " << blk.term.label2;
                break;
            case TermKind::RETURN:
                std::cout << "return";
                if (blk.term.has_ret_val)
                    std::cout << " %" << blk.term.ret_local;
                break;
        }
        std::cout << ";\n\n";
    }

    std::cout << "}\n";
}
