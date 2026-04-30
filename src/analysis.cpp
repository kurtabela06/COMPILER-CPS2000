// =============================================================================
// analysis.cpp  —  Fact Extractor implementation (Task 5)
// =============================================================================
//
// OVERVIEW
// ─────────
// This file implements the FactExtractor class declared in analysis.hpp.
//
// The job is simple: read the in-memory CFG (built by the parser in Task 2
// and validated in Task 3) and write three text files that Soufflé can read:
//
//   datalog/facts/edge.facts  — control-flow edges between blocks
//   datalog/facts/def.facts   — which variables are WRITTEN in which block
//   datalog/facts/use.facts   — which variables are READ   in which block
//
// WORKED EXAMPLE — abs_diff
// ──────────────────────────
// Given:
//   bb0: %zero=const 0; %d=bin sub %a,%b; %is_neg=bin lt %d,%zero; cjump %is_neg,bb1,bb2
//   bb1: %r=un neg %d; return %r
//   bb2: return %d
//
//   edge.facts:
//     bb0   bb1
//     bb0   bb2
//
//   def.facts:
//     bb0   zero
//     bb0   d
//     bb0   is_neg
//     bb1   r
//
//   use.facts:
//     bb0   a         ← %a used in bin sub
//     bb0   b         ← %b used in bin sub
//     bb0   d         ← %d used in bin lt
//     bb0   zero      ← %zero used in bin lt
//     bb0   is_neg    ← %is_neg used in cjump condition
//     bb1   d         ← %d used in un neg
//     bb1   r         ← %r used in return
//     bb2   d         ← %d used in return
// =============================================================================

#include "analysis.hpp"

#include <fstream>    // std::ofstream — write files to disk
#include <iostream>   // std::cerr — print errors to the terminal
#include <sys/stat.h> // mkdir — create directories (POSIX)
#include <errno.h>    // errno — check why mkdir failed

// =============================================================================
// Constructor
// =============================================================================
FactExtractor::FactExtractor(const Program& prog)
    : fn_(prog.function)
{}

// =============================================================================
// extract() — the main entry point
// =============================================================================
bool FactExtractor::extract(const std::string& out_dir) {
    // --- Step 1: make sure the output directory exists ---
    if (!ensure_dir(out_dir)) {
        error_ = "Cannot create output directory: " + out_dir;
        return false;
    }

    // --- Step 2: walk the CFG and populate edges_, defs_, uses_ ---
    collect_facts();

    // --- Step 3: write the three .facts files ---
    if (!write_edge_facts(out_dir + "edge.facts")) return false;
    if (!write_def_facts (out_dir + "def.facts"))  return false;
    if (!write_use_facts (out_dir + "use.facts"))  return false;

    return true;
}

// =============================================================================
// collect_facts() — walk every block and dispatch to collect_block()
// =============================================================================
void FactExtractor::collect_facts() {
    // Clear any facts from a previous call (important if extract() is called
    // more than once on the same FactExtractor object).
    edges_.clear();
    defs_.clear();
    uses_.clear();

    for (const Block& blk : fn_.blocks) {
        collect_block(blk);
    }
}

// =============================================================================
// collect_block() — extract all facts from one basic block
// =============================================================================
void FactExtractor::collect_block(const Block& blk) {
    const std::string& lbl = blk.label;

    // -----------------------------------------------------------------------
    // 1. EDGE facts — from the terminator
    // -----------------------------------------------------------------------
    //
    // A basic block has exactly one terminator.  We add an edge for each
    // successor label that it mentions:
    //   jump  L        → edge(lbl, L)
    //   cjump C, L1, L2 → edge(lbl, L1) and edge(lbl, L2)
    //   return          → no edge (return leaves the function)
    //
    const Terminator& t = blk.term;
    if (t.kind == TermKind::JUMP) {
        // Unconditional jump: one successor
        edges_.push_back({lbl, t.label1});
    } else if (t.kind == TermKind::CJUMP) {
        // Conditional jump: two successors (true-branch and false-branch)
        edges_.push_back({lbl, t.label1});
        edges_.push_back({lbl, t.label2});
    }
    // RETURN has no successors within the function — no edge.

    // -----------------------------------------------------------------------
    // 2. DEF facts — every statement that writes a variable
    // -----------------------------------------------------------------------
    //
    // A statement with `has_dest = true` defines (writes to) `dest`.
    // STORE has no destination — it writes through a pointer, but the
    // pointer variable itself is only USED here, not defined.
    //
    for (const Statement& s : blk.stmts) {
        if (s.has_dest) {
            // s.dest is already without '%' (the parser strips it).
            defs_.push_back({lbl, s.dest});
        }
    }
    // Note: the terminator never defines a variable.

    // -----------------------------------------------------------------------
    // 3. USE facts — every variable READ in the block
    // -----------------------------------------------------------------------
    //
    // We collect uses from both the statements and the terminator.

    // Uses from statements
    for (const Statement& s : blk.stmts) {
        collect_rhs_uses(lbl, s.rhs);
    }

    // Uses from the terminator (cjump reads the condition; return reads
    // the return value)
    collect_term_uses(lbl, t);
}

// =============================================================================
// collect_rhs_uses() — find all locals READ by one Rhs
// =============================================================================
//
// For each opcode, we look at which fields of `Rhs` hold local names and
// add them as use facts.
//
// KEY DISTINCTION:
//   A "use" is when we READ a variable's value.
//   A "def"  is when we WRITE a variable (only the destination, recorded in
//            collect_block()).
//
// For example:  %d = bin sub %a, %b
//   - `d` is the DESTINATION → def fact (handled by collect_block())
//   - `a` and `b` are OPERANDS → use facts (handled here)
void FactExtractor::collect_rhs_uses(const std::string& block_label,
                                      const Rhs& rhs) {
    switch (rhs.kind) {

    case RhsKind::COPY:
        // %dst = %src  →  src is used
        uses_.push_back({block_label, rhs.op1});
        break;

    case RhsKind::CONST:
        // %dst = const <literal>  →  no locals used (it's a compile-time constant)
        break;

    case RhsKind::CAST:
        // %dst = cast %src to T  →  src is used
        uses_.push_back({block_label, rhs.op1});
        break;

    case RhsKind::UN:
        // %dst = un <op> %src  →  src is used
        uses_.push_back({block_label, rhs.op1});
        break;

    case RhsKind::BIN:
        // %dst = bin <op> %lhs, %rhs  →  both lhs and rhs are used
        uses_.push_back({block_label, rhs.op1});
        uses_.push_back({block_label, rhs.op2});
        break;

    case RhsKind::ADDR_OF:
        // %dst = addr_of %src  →  src is used (we take its address)
        uses_.push_back({block_label, rhs.op1});
        break;

    case RhsKind::MEMBER_PTR:
        // %dst = member_ptr %ptr, field  →  ptr is used
        // The field name is a struct field, not a local variable — no use fact.
        uses_.push_back({block_label, rhs.op1});
        break;

    case RhsKind::LOAD:
        // %dst = load %ptr  →  ptr is used (we dereference it)
        uses_.push_back({block_label, rhs.op1});
        break;

    case RhsKind::STORE:
        // store %ptr, %val  →  BOTH ptr AND val are used.
        // STORE has no destination — both operands are reads.
        uses_.push_back({block_label, rhs.op1}); // pointer
        uses_.push_back({block_label, rhs.op2}); // value to write
        break;

    case RhsKind::CALL:
        // %dst = call Path(%arg1, %arg2, …)  →  every argument is used
        for (const std::string& arg : rhs.call_args) {
            uses_.push_back({block_label, arg});
        }
        break;
    }
}

// =============================================================================
// collect_term_uses() — find all locals READ by a terminator
// =============================================================================
void FactExtractor::collect_term_uses(const std::string& block_label,
                                       const Terminator& t) {
    switch (t.kind) {

    case TermKind::JUMP:
        // jump L  →  no local variables are read (just a label)
        break;

    case TermKind::CJUMP:
        // cjump %cond, L1, L2  →  cond is used (it's the boolean we branch on)
        uses_.push_back({block_label, t.cond});
        break;

    case TermKind::RETURN:
        // return %val  →  val is used (if there is a return value)
        if (t.has_ret_val) {
            uses_.push_back({block_label, t.ret_local});
        }
        break;
    }
}

// =============================================================================
// write_edge_facts() — write edges_ to a TSV file
// =============================================================================
bool FactExtractor::write_edge_facts(const std::string& path) {
    std::ofstream f(path);
    if (!f.is_open()) {
        error_ = "Cannot open for writing: " + path;
        return false;
    }
    // Each line:  from\tto\n
    // Soufflé reads tab-separated values.  The newline marks the end of
    // each tuple (one tuple = one fact).
    for (const EdgeFact& e : edges_) {
        f << e.from << "\t" << e.to << "\n";
    }
    return true;
}

// =============================================================================
// write_def_facts() — write defs_ to a TSV file
// =============================================================================
bool FactExtractor::write_def_facts(const std::string& path) {
    std::ofstream f(path);
    if (!f.is_open()) {
        error_ = "Cannot open for writing: " + path;
        return false;
    }
    for (const DefFact& d : defs_) {
        f << d.block << "\t" << d.var << "\n";
    }
    return true;
}

// =============================================================================
// write_use_facts() — write uses_ to a TSV file
// =============================================================================
bool FactExtractor::write_use_facts(const std::string& path) {
    std::ofstream f(path);
    if (!f.is_open()) {
        error_ = "Cannot open for writing: " + path;
        return false;
    }
    for (const UseFact& u : uses_) {
        f << u.block << "\t" << u.var << "\n";
    }
    return true;
}

// =============================================================================
// ensure_dir() — create a directory if it does not already exist
// =============================================================================
// POSIX mkdir() takes a path and permission bits.
// 0755 = owner can read/write/execute; group and others can read/execute.
// (Standard Unix directory permissions.)
//
// We return true if:
//   - mkdir() succeeded (directory was created), OR
//   - mkdir() failed with EEXIST (directory already exists — that's fine)
bool FactExtractor::ensure_dir(const std::string& dir) {
    // Remove a trailing slash for mkdir (some systems reject "path/")
    std::string d = dir;
    if (!d.empty() && d.back() == '/') d.pop_back();
    if (d.empty()) return true; // empty = current directory, always exists

    int result = mkdir(d.c_str(), 0755);
    if (result == 0) return true;       // created successfully
    if (errno == EEXIST) return true;   // already exists — that's fine
    return false;                        // some other error (permissions, etc.)
}
