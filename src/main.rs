// =============================================================================
// main.rs — Entry point for the ResIR compiler
// =============================================================================
//
// The compiler pipeline will eventually look like this:
//
//   source text
//       │
//       ▼  Task 1: lexer.rs
//   Vec<TokenWithPos>
//       │
//       ▼  Task 2: parser.rs  (not yet implemented)
//   Program (AST / CFG)
//       │
//       ▼  Task 3: sema.rs    (not yet implemented)
//   Validated Program
//       │
//       ▼  Task 4: codegen.rs (not yet implemented)
//   C source text
//
// For now, `main` reads a file, runs the lexer, and pretty-prints the tokens
// so you can verify the lexer is working correctly before moving on.
// =============================================================================

// Tell Rust about our other source files.
// `mod foo;` tells the compiler to look for `src/foo.rs` and compile it as a
// module named `foo`.
pub mod lexer;
pub mod ast;
pub mod parser;
pub mod sema;
pub mod codegen;
pub mod analysis;

use std::env;
use std::fs;
use std::process;

fn main() {
    // `env::args()` returns an iterator over the command-line arguments.
    // `.collect::<Vec<_>>()` collects them into a Vec so we can index them.
    let args: Vec<String> = env::args().collect();

    if args.len() < 2 {
        // `eprintln!` prints to stderr (good practice for error messages).
        eprintln!("Usage: {} <input.resir>", args[0]);
        eprintln!("Example: {} tests/basic_math.resir", args[0]);
        process::exit(1);
    }

    let filename = &args[1];

    // `fs::read_to_string` reads the entire file into a `String`.
    // `.unwrap_or_else` lets us provide a custom error handler instead of
    // panicking with a generic message.
    let source = fs::read_to_string(filename).unwrap_or_else(|err| {
        eprintln!("Error reading `{}`: {}", filename, err);
        process::exit(1);
    });

    // Run the lexer.
    match lexer::tokenise(source) {
        Ok(tokens) => {
            println!("=== Tokens for `{}` ===", filename);
            for twp in &tokens {
                // `{:?}` uses the Debug trait to print the token value.
                println!("  [{:>4}:{:<4}]  {:?}", twp.line, twp.col, twp.token);
            }
            println!("=== {} tokens total ===", tokens.len());
        }
        Err(err) => {
            // `LexError` implements `Display`, so we can print it directly.
            eprintln!("{}", err);
            process::exit(1);
        }
    }
}
