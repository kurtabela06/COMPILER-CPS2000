// =============================================================================
// analysis.hpp  —  Fact Extractor for Datalog-based Live Variable Analysis
// =============================================================================
//
// WHAT IS THIS MODULE?
// ─────────────────────
// This is the "bridge" between your C++ compiler (Tasks 1-4) and the Soufflé
// Datalog engine (Task 5).
//
// The idea:
//   Your C++ compiler has a beautiful in-memory CFG (Control Flow Graph).
//   Soufflé Datalog cannot read C++ structs — it reads TEXT FILES.
//   This module translates the CFG into simple text files called "fact files".
//
// WHAT IS LIVE VARIABLE ANALYSIS?
// ─────────────────────────────────
// Imagine you're driving and need to decide what to pack.
//   - If you KNOW you'll stop at a petrol station, you don't need to pack
//     a full jerry can (it's "dead" — you won't need it in the future).
//   - If there's NO petrol station ahead, the jerry can is "live" — you
//     might need it.
//
// In a program, a variable is "live" at a point if its current value might
// be read by some future instruction.  If a variable is NEVER read after
// it's written, it's "dead" — we computed a value nobody ever uses.
//
// WHY IS IT USEFUL?
//   - Dead Code Elimination: assignments to dead variables can be deleted.
//   - Register Allocation: live variables compete for registers; dead ones
//     can be evicted early, freeing up space.
//
// THE THREE FACT RELATIONS
// ─────────────────────────
// We extract three kinds of facts from the CFG:
//
//   edge(from, to)    — there is a control-flow edge from block `from` to
//                       block `to` (jump or cjump).
//
//   def(block, var)   — variable `var` is DEFINED (written to) in `block`.
//                       Definitions: assignments, including the COPY opcode.
//
//   use(block, var)   — variable `var` is USED (read from) in `block`.
//                       Uses: any operand in an Rhs that is a local.
//
// FORMAT OF THE FACT FILES
// ─────────────────────────
// Soufflé reads tab-separated values (TSV), one tuple per line:
//
//   edge.facts:
//     bb0\tbb1
//     bb0\tbb2
//
//   def.facts:
//     bb0\tzero
//     bb0\td
//     bb0\tis_neg
//
//   use.facts:
//     bb0\ta
//     bb0\tb
//     bb0\td
//     bb0\tzero
//     bb1\td
//
// These files are written to a folder called "datalog/facts/".
//
// HOW TO USE
// ───────────
//   FactExtractor fx(prog);
//   if (!fx.extract("datalog/facts/")) {
//       std::cerr << fx.get_error() << "\n";
//   }
// =============================================================================

#pragma once
#include "cfg.hpp"   // Program, Function, Block, Statement, Rhs, Terminator

#include <string>
#include <vector>
#include <set>       // std::set — sorted unique collection (for deduplication)

// =============================================================================
// FactExtractor class
// =============================================================================
class FactExtractor {
public:
    // Constructor: takes the fully-validated program.
    // We keep a const reference — the extractor only READS the CFG.
    explicit FactExtractor(const Program& prog);

    // extract() — walk the CFG and write all three fact files to `out_dir`.
    //
    // `out_dir` should end with a slash, e.g. "facts/" or "/tmp/myanalysis/".
    // The method creates three files inside that directory:
    //   <out_dir>/edge.facts
    //   <out_dir>/def.facts
    //   <out_dir>/use.facts
    //
    // Returns true on success; false and sets error_ on file I/O failure.
    bool extract(const std::string& out_dir);

    // get_error() — the last error message (only meaningful after extract()
    // returns false).
    const std::string& get_error() const { return error_; }

    // -----------------------------------------------------------------------
    // Accessors for in-memory facts — useful for unit tests so that we can
    // check the extracted facts WITHOUT needing to read the files back.
    // -----------------------------------------------------------------------

    // Fact tuple types.
    // We use structs (rather than pairs) for readability.

    struct EdgeFact  { std::string from, to; };
    struct DefFact   { std::string block, var; };
    struct UseFact   { std::string block, var; };

    const std::vector<EdgeFact>& edges() const { return edges_; }
    const std::vector<DefFact>&  defs()  const { return defs_;  }
    const std::vector<UseFact>&  uses()  const { return uses_;  }

private:
    // -----------------------------------------------------------------------
    // Data
    // -----------------------------------------------------------------------

    const Function& fn_;      // the function we are analysing (from prog.function)

    // In-memory representation of the three relations.
    // These are populated by the collect_*() methods and then written to disk.
    std::vector<EdgeFact> edges_;
    std::vector<DefFact>  defs_;
    std::vector<UseFact>  uses_;

    std::string error_;  // set on the first I/O error

    // -----------------------------------------------------------------------
    // Collection methods — populate the in-memory vectors
    // -----------------------------------------------------------------------

    // Walk the CFG and fill edges_, defs_, uses_.
    void collect_facts();

    // Process one block: add its edge(s), def facts, and use facts.
    void collect_block(const Block& blk);

    // Extract use facts from one Rhs (all the locals that are READ).
    void collect_rhs_uses(const std::string& block_label, const Rhs& rhs);

    // Extract use facts from the terminator (e.g. the condition in cjump,
    // and the return value in return).
    void collect_term_uses(const std::string& block_label, const Terminator& t);

    // -----------------------------------------------------------------------
    // I/O helper
    // -----------------------------------------------------------------------

    // write_facts() — write a vector of two-column tuples to a TSV file.
    //
    // Template parameter: T must have fields `.first` and `.second` OR
    // be one of EdgeFact/DefFact/UseFact.
    // We provide one overload per type for clarity.
    bool write_edge_facts(const std::string& path);
    bool write_def_facts (const std::string& path);
    bool write_use_facts (const std::string& path);

    // Ensure a directory exists (creates it if needed using POSIX mkdir).
    // Returns true on success.
    static bool ensure_dir(const std::string& dir);
};
