// =============================================================================
// lexer.rs — Task 1: Table-Driven Lexer for the ResIR language
// =============================================================================
//
// What is a lexer?
//   A lexer (also called a "scanner" or "tokeniser") reads raw source text
//   character-by-character and groups those characters into meaningful units
//   called TOKENS.  Think of it like splitting a sentence into words before
//   you try to understand its grammar.
//
// Why table-driven?
//   A table-driven lexer encodes all transition logic in a 2-D array:
//       next_state = TABLE[current_state][input_class]
//   One tight loop does all the work; no forest of if/else chains.
//   This makes it easy to reason about, audit, and extend.
//
// High-level flow:
//   1.  Classify the next character into an InputClass.
//   2.  Look up the next State in the TRANSITION table.
//   3.  If we have reached an accepting state, emit a Token.
//   4.  Repeat until end-of-file.
// =============================================================================

// ---------------------------------------------------------------------------
// Section 1 – Token types
// ---------------------------------------------------------------------------
//
// In Rust, `enum` (enumeration) lets us define a type that can be one of
// several named variants.  Each variant can optionally carry data inside it.
// For example, `Token::IntLit(42)` is an integer literal token carrying the
// value 42.
//
// `#[derive(Debug, Clone, PartialEq)]` automatically generates:
//   - Debug   → lets us print the token with {:?}
//   - Clone   → lets us duplicate a token value
//   - PartialEq → lets us compare two tokens with ==
// ---------------------------------------------------------------------------

/// Every keyword in the ResIR language gets its own variant here.
/// This makes pattern-matching in the parser exhaustive and type-safe.
#[derive(Debug, Clone, PartialEq)]
pub enum Keyword {
    Function,   // `function`
    Locals,     // `locals`
    Entry,      // `entry`
    Const,      // `const`
    Cast,       // `cast`
    Un,         // `un`
    Bin,        // `bin`
    AddrOf,     // `addr_of`
    MemberPtr,  // `member_ptr`
    Load,       // `load`
    Store,      // `store`
    Call,       // `call`
    Jump,       // `jump`
    Cjump,      // `cjump`
    Return,     // `return`
    Void,       // `void`
    Ptr,        // `ptr`
    To,         // `to`
    Extern,     // `extern`
    Type,       // `type`
    // Primitive type keywords
    Bool,       // `bool`
    I32,        // `i32`
    I64,        // `i64`
    U32,        // `u32`
    F64,        // `f64`
    // Boolean literal keywords
    True,       // `true`
    False,      // `false`
    // Null pointer keyword
    Null,       // `null`
}

/// A `Token` is the unit the lexer emits.  The parser will receive a
/// `Vec<TokenWithPos>` (see below) and process tokens one at a time.
#[derive(Debug, Clone, PartialEq)]
pub enum Token {
    // --- Keywords -----------------------------------------------------------
    Kw(Keyword),

    // --- Identifiers & paths ------------------------------------------------
    // A plain identifier: [A-Za-z_][A-Za-z0-9_]*
    // Paths such as `Custom::Struct::Point` are tokenised as a single
    // `Path(Vec<String>)` where each segment is one identifier.
    Ident(String),
    Path(Vec<String>), // e.g. ["Custom", "Struct", "Point"]

    // --- Locals: %ident -----------------------------------------------------
    Local(String), // the ident portion WITHOUT the leading %

    // --- Labels: bb[0-9]+ ---------------------------------------------------
    Label(String), // the full text e.g. "bb0", "bb12"

    // --- Literals -----------------------------------------------------------
    IntLit(i64),
    FloatLit(f64),
    // true/false are Kw(Keyword::True) / Kw(Keyword::False)
    // null  is     Kw(Keyword::Null)

    // --- Punctuation --------------------------------------------------------
    LBrace,    // {
    RBrace,    // }
    LParen,    // (
    RParen,    // )
    Colon,     // :
    Semicolon, // ;
    Comma,     // ,
    Arrow,     // ->
    Lt,        // <
    Gt,        // >
    Eq,        // =
    ColonColon,// ::

    // --- End of file --------------------------------------------------------
    Eof,
}

/// A token together with its source position.
/// Storing position lets us produce helpful error messages later.
#[derive(Debug, Clone)]
pub struct TokenWithPos {
    pub token: Token,
    pub line:  usize, // 1-based line number
    pub col:   usize, // 1-based column number
}

// ---------------------------------------------------------------------------
// Section 2 – Lexer states
// ---------------------------------------------------------------------------
//
// Each state in the DFA corresponds to "what are we in the middle of
// scanning right now?"
//
// `#[derive(Debug, Clone, Copy, PartialEq, Eq)]` on an enum with no data
// fields makes it very cheap to copy and compare — useful for the tight
// transition loop.
// ---------------------------------------------------------------------------

/// Every row in the transition table is one of these states.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
enum State {
    Start,         // Initial / between tokens
    InIdent,       // Reading an identifier or keyword: [A-Za-z_][A-Za-z0-9_]*
    InLocal,       // Just seen `%`, now reading the ident part
    InIntLit,      // Reading decimal digits for an integer literal
    InFloatFrac,   // Reading digits after the decimal point
    InMinus,       // Just seen `-`, might become `->`
    InColon,       // Just seen `:`, might become `::`
    InLineComment, // Inside a `//` comment; skip until newline
    Done,          // Accepting: a complete token has been accumulated
    Error,         // Unrecognised character
}

// ---------------------------------------------------------------------------
// Section 3 – Input classes
// ---------------------------------------------------------------------------
//
// Instead of branching on every possible ASCII value (128 cases!), we map
// characters into a small set of classes.  The transition table has one
// column per class, so it stays compact.
// ---------------------------------------------------------------------------

/// How many input classes there are — used to size the 2-D array.
const NUM_CLASSES: usize = 13;

/// An `InputClass` is a column index into `TRANSITION`.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
#[repr(usize)] // store as a plain integer for fast array indexing
enum InputClass {
    Alpha    = 0,  // A-Z, a-z, _  (start of ident)
    Digit    = 1,  // 0-9
    Percent  = 2,  // %  (start of local)
    Dot      = 3,  // .  (decimal point in float)
    Minus    = 4,  // -  (start of `->`  or negative literal)
    Gt       = 5,  // >
    Colon    = 6,  // :  (start of `::` or standalone `:`)
    Slash    = 7,  // /  (start of `//` comment)
    Punct    = 8,  // { } ( ) ; , < = (single-char punctuation)
    Space    = 9,  // space, tab, carriage return
    Newline  = 10, // \n  (same as space but increments line counter)
    Hash     = 11, // #  (reserved, treated as error for now)
    Other    = 12, // anything else → Error
}

/// Map a byte to its `InputClass`.  This is O(1) per character.
fn classify(c: u8) -> InputClass {
    match c {
        // Letters and underscore start identifiers
        b'A'..=b'Z' | b'a'..=b'z' | b'_' => InputClass::Alpha,
        b'0'..=b'9'                        => InputClass::Digit,
        b'%'                               => InputClass::Percent,
        b'.'                               => InputClass::Dot,
        b'-'                               => InputClass::Minus,
        b'>'                               => InputClass::Gt,
        b':'                               => InputClass::Colon,
        b'/'                               => InputClass::Slash,
        b'{' | b'}' | b'(' | b')' | b';'
        | b',' | b'<' | b'=' => InputClass::Punct,
        b' ' | b'\t' | b'\r'               => InputClass::Space,
        b'\n'                              => InputClass::Newline,
        b'#'                               => InputClass::Hash,
        _                                  => InputClass::Other,
    }
}

// ---------------------------------------------------------------------------
// Section 4 – Transition table
// ---------------------------------------------------------------------------
//
// `TRANSITION[state][class]` gives the next state.
//
// Rust 2-D arrays are written as `[[T; COLS]; ROWS]`.
// We access them as `TRANSITION[state as usize][class as usize]`.
//
// Legend of states (rows):
//   0 = Start        1 = InIdent       2 = InLocal
//   3 = InIntLit     4 = InFloatFrac   5 = InMinus
//   6 = InColon      7 = InLineComment 8 = Done
//   9 = Error
//
// Legend of classes (columns):
//   0=Alpha 1=Digit 2=Percent 3=Dot 4=Minus 5=Gt 6=Colon
//   7=Slash 8=Punct 9=Space  10=Newline 11=Hash 12=Other
// ---------------------------------------------------------------------------

/// The raw DFA transition table.
/// Row = current state, Column = input class, Value = next state.
#[rustfmt::skip]
const TRANSITION: [[State; NUM_CLASSES]; 10] = {
    use State::*;
    // Rows ordered by State discriminant (0..=9).
    // Columns ordered by InputClass discriminant (0..=12):
    //   0=Alpha 1=Digit 2=Percent 3=Dot 4=Minus 5=Gt 6=Colon
    //   7=Slash 8=Punct 9=Space 10=Newline 11=Hash 12=Other
    [
        // State::Start (0)
        //  Alpha     Digit     Percent   Dot       Minus     Gt        Colon
        [  InIdent,  InIntLit, InLocal,  Error,    InMinus,  Done,     InColon,
        //  Slash          Punct   Space   Newline  Hash    Other
           InLineComment,  Done,   Start,  Start,   Error,  Error  ],

        // State::InIdent (1) — keep reading alphanumeric / underscore.
        // When we see `:` we stop (Done) — the path `::` logic is handled
        // inside `scan_ident_or_path` by peeking ahead, not via the table.
        //  Alpha     Digit     Percent   Dot    Minus   Gt     Colon
        [  InIdent,  InIdent,  Done,     Done,  Done,   Done,  Done,
        //  Slash   Punct   Space   Newline  Hash   Other
           Done,    Done,   Done,   Done,    Done,  Done  ],

        // State::InLocal (2) — after `%`, read ident chars
        //  Alpha     Digit     Percent   Dot       Minus     Gt        Colon
        [  InLocal,  InLocal,  Done,     Done,     Done,     Done,     Done,
        //  Slash     Punct     Space     Newline   Hash      Other
           Done,     Done,     Done,     Done,     Done,     Done  ],

        // State::InIntLit (3) — reading decimal digits
        //  Alpha     Digit     Percent   Dot       Minus     Gt        Colon
        [  Done,     InIntLit, Done,     InFloatFrac, Done,  Done,     Done,
        //  Slash     Punct     Space     Newline   Hash      Other
           Done,     Done,     Done,     Done,     Done,     Done  ],

        // State::InFloatFrac (4) — digits after decimal point
        //  Alpha     Digit     Percent   Dot       Minus     Gt        Colon
        [  Done,     InFloatFrac, Done,  Error,    Done,     Done,     Done,
        //  Slash     Punct     Space     Newline   Hash      Other
           Done,     Done,     Done,     Done,     Done,     Done  ],

        // State::InMinus (5) — just seen `-`
        //  Alpha     Digit     Percent   Dot       Minus     Gt        Colon
        [  Error,    Error,    Error,    Error,    Error,    Done,     Error,
        //  Slash     Punct     Space     Newline   Hash      Other
           Error,    Error,    Error,    Error,    Error,    Error  ],

        // State::InColon (6) — just seen `:`
        //  Alpha     Digit     Percent   Dot       Minus     Gt        Colon
        [  Done,     Done,     Done,     Done,     Done,     Done,     Done,
        //  Slash     Punct     Space     Newline   Hash      Other
           Done,     Done,     Done,     Done,     Done,     Done  ],

        // State::InLineComment (7) — inside `//` comment
        //  Alpha     Digit     Percent   Dot       Minus     Gt        Colon
        [  InLineComment, InLineComment, InLineComment, InLineComment,
           InLineComment, InLineComment, InLineComment,
        //  Slash       Punct          Space          Newline
           InLineComment, InLineComment, InLineComment, Start,
        //  Hash              Other
           InLineComment, InLineComment ],

        // State::Done (8) — terminal / accepting (never used as a source row)
        [  Error; NUM_CLASSES],

        // State::Error (9) — terminal / error (never used as a source row)
        [  Error; NUM_CLASSES],
    ]
};

// ---------------------------------------------------------------------------
// Section 5 – Reserved-word lookup
// ---------------------------------------------------------------------------
//
// After we finish scanning an identifier we check whether it is actually a
// keyword.  Using a match on &str is idiomatic Rust and the compiler turns
// it into an efficient jump table internally.
// ---------------------------------------------------------------------------

/// Try to convert a scanned identifier string into a `Keyword`.
/// Returns `None` if the string is a user-defined identifier.
fn str_to_keyword(s: &str) -> Option<Keyword> {
    match s {
        "function"   => Some(Keyword::Function),
        "locals"     => Some(Keyword::Locals),
        "entry"      => Some(Keyword::Entry),
        "const"      => Some(Keyword::Const),
        "cast"       => Some(Keyword::Cast),
        "un"         => Some(Keyword::Un),
        "bin"        => Some(Keyword::Bin),
        "addr_of"    => Some(Keyword::AddrOf),
        "member_ptr" => Some(Keyword::MemberPtr),
        "load"       => Some(Keyword::Load),
        "store"      => Some(Keyword::Store),
        "call"       => Some(Keyword::Call),
        "jump"       => Some(Keyword::Jump),
        "cjump"      => Some(Keyword::Cjump),
        "return"     => Some(Keyword::Return),
        "void"       => Some(Keyword::Void),
        "ptr"        => Some(Keyword::Ptr),
        "to"         => Some(Keyword::To),
        "extern"     => Some(Keyword::Extern),
        "type"       => Some(Keyword::Type),
        "bool"       => Some(Keyword::Bool),
        "i32"        => Some(Keyword::I32),
        "i64"        => Some(Keyword::I64),
        "u32"        => Some(Keyword::U32),
        "f64"        => Some(Keyword::F64),
        "true"       => Some(Keyword::True),
        "false"      => Some(Keyword::False),
        "null"       => Some(Keyword::Null),
        _            => None,
    }
}

// ---------------------------------------------------------------------------
// Section 6 – Lexer error type
// ---------------------------------------------------------------------------

/// Describes a lexical error.  We store the bad character plus its location
/// so the user can find it immediately.
#[derive(Debug, Clone)]
pub struct LexError {
    pub message: String,
    pub line:    usize,
    pub col:     usize,
}

impl std::fmt::Display for LexError {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        write!(f, "Lexical error at {}:{} — {}", self.line, self.col, self.message)
    }
}

// ---------------------------------------------------------------------------
// Section 7 – The Lexer struct and its main method
// ---------------------------------------------------------------------------

/// The `Lexer` holds everything needed to scan a single source string.
pub struct Lexer {
    /// The source text as a sequence of bytes.
    /// We work with bytes (u8) rather than Unicode chars because ResIR is
    /// defined over ASCII.  `as_bytes()` gives a &[u8] view — no allocation.
    source: Vec<u8>,

    /// Our current position in `source`.
    pos: usize,

    /// Current 1-based line number (incremented on every '\n').
    line: usize,

    /// Current 1-based column number (reset to 1 after every '\n').
    col: usize,
}

impl Lexer {
    /// Create a new `Lexer` from a source string.
    ///
    /// `source.into_bytes()` converts the `String` into a `Vec<u8>` so we
    /// own the bytes and can index them freely.
    pub fn new(source: String) -> Self {
        Lexer {
            source: source.into_bytes(),
            pos:    0,
            line:   1,
            col:    1,
        }
    }

    /// Peek at the current character without consuming it.
    /// Returns `None` at end-of-file.
    ///
    /// In Rust, `Vec::get(i)` returns `Option<&T>` — `Some(&byte)` if in
    /// bounds, `None` if not.  We dereference with `*` to get a plain `u8`.
    fn peek(&self) -> Option<u8> {
        self.source.get(self.pos).copied()
        // `.copied()` is equivalent to `.map(|&b| b)` — it turns
        // Option<&u8> into Option<u8> by copying the value out.
    }

    /// Peek one position ahead (used to detect `//` and `::` two-char
    /// tokens while keeping the main loop clean).
    fn peek_next(&self) -> Option<u8> {
        self.source.get(self.pos + 1).copied()
    }

    /// Consume the current character, advance the position, and update the
    /// line/column tracking.  Returns the consumed byte.
    fn advance(&mut self) -> u8 {
        let c = self.source[self.pos];
        self.pos += 1;
        if c == b'\n' {
            self.line += 1;
            self.col = 1;
        } else {
            self.col += 1;
        }
        c
    }

    /// Skip whitespace and comments.  Returns only when the next character
    /// is the start of a real token (or EOF).
    ///
    /// We handle line comments (`//`) here so that the main scan loop never
    /// needs to deal with them: by the time `next_token` calls the table
    /// machine, we are guaranteed to be at meaningful input.
    fn skip_whitespace_and_comments(&mut self) {
        loop {
            match self.peek() {
                // Plain whitespace — just advance.
                Some(b' ') | Some(b'\t') | Some(b'\r') | Some(b'\n') => {
                    self.advance();
                }
                // `//` line comment — advance past the slash-slash, then
                // skip everything up to (but not including) the newline.
                Some(b'/') if self.peek_next() == Some(b'/') => {
                    self.advance(); // consume first `/`
                    self.advance(); // consume second `/`
                    while let Some(c) = self.peek() {
                        if c == b'\n' { break; }
                        self.advance();
                    }
                }
                // Anything else (including EOF) — stop skipping.
                _ => break,
            }
        }
    }

    // -----------------------------------------------------------------------
    // Token-specific scanners
    //
    // These helpers are called once the transition table tells us we are in
    // a particular state.  They read just enough characters to finish the
    // current token and return it.
    // -----------------------------------------------------------------------

    /// Scan a full identifier (or keyword, label, path) starting at the
    /// current position.  The first character has ALREADY been consumed by
    /// the caller; `first` carries it.
    ///
    /// After collecting all alphanumeric / underscore characters, we check:
    ///   1. Is it a label? (`bb` followed by digits only → Token::Label)
    ///   2. Is the next char `:` and the one after also `:`?
    ///      If so, we are at the start of a `::` path, so we collect more
    ///      segments recursively → Token::Path.
    ///   3. Is it a reserved word? → Token::Kw
    ///   4. Otherwise → Token::Ident
    fn scan_ident_or_path(&mut self, first: u8) -> Token {
        // Collect the first segment into a String.
        // `String::from(char)` creates a one-character string; we then push
        // more characters as we consume them.
        let mut segment = String::new();
        segment.push(first as char); // safe: we only put ASCII in

        // Keep consuming while the next char is alphanumeric or `_`.
        while let Some(c) = self.peek() {
            if c.is_ascii_alphanumeric() || c == b'_' {
                self.advance();
                segment.push(c as char);
            } else {
                break;
            }
        }

        // --- Check for `::` path separator -----------------------------------
        // If the very next two chars are `::`, this identifier is the first
        // segment of a qualified path.  We collect all segments into a Vec.
        if self.peek() == Some(b':') && self.peek_next() == Some(b':') {
            let mut segments = vec![segment];
            while self.peek() == Some(b':') && self.peek_next() == Some(b':') {
                self.advance(); // consume first `:`
                self.advance(); // consume second `:`
                // Read the next segment
                let mut seg = String::new();
                while let Some(c) = self.peek() {
                    if c.is_ascii_alphanumeric() || c == b'_' {
                        self.advance();
                        seg.push(c as char);
                    } else {
                        break;
                    }
                }
                segments.push(seg);
            }
            return Token::Path(segments);
        }

        // --- Check for label: bb[0-9]+ ---------------------------------------
        // A label starts with `bb` and is followed exclusively by digits.
        // We detect this pattern on the already-collected `segment` string.
        if is_label(&segment) {
            return Token::Label(segment);
        }

        // --- Check for keyword -----------------------------------------------
        if let Some(kw) = str_to_keyword(&segment) {
            return Token::Kw(kw);
        }

        // --- Plain identifier -------------------------------------------------
        Token::Ident(segment)
    }

    /// Scan a local variable: `%` has already been consumed, now read the
    /// identifier part.  A local MUST start with an alpha or `_`.
    fn scan_local(&mut self, start_line: usize, start_col: usize)
        -> Result<Token, LexError>
    {
        let mut name = String::new();
        // The first character after `%` must be alphabetic or `_`.
        match self.peek() {
            Some(c) if c.is_ascii_alphabetic() || c == b'_' => {
                self.advance();
                name.push(c as char);
            }
            other => {
                let bad = other.map(|b| b as char).unwrap_or('\0');
                return Err(LexError {
                    message: format!("expected identifier after `%`, found `{}`", bad),
                    line:    start_line,
                    col:     start_col,
                });
            }
        }
        // Continue collecting alphanumeric / underscore chars.
        while let Some(c) = self.peek() {
            if c.is_ascii_alphanumeric() || c == b'_' {
                self.advance();
                name.push(c as char);
            } else {
                break;
            }
        }
        Ok(Token::Local(name))
    }

    /// Scan an integer literal (or float if we encounter a `.`).
    /// The first digit `first` has already been consumed.
    fn scan_number(&mut self, first: u8) -> Token {
        let mut digits = String::new();
        digits.push(first as char);
        let mut is_float = false;

        loop {
            match self.peek() {
                // More digits — always ok
                Some(c) if c.is_ascii_digit() => {
                    self.advance();
                    digits.push(c as char);
                }
                // Decimal point — switch to float mode (only once)
                Some(b'.') if !is_float => {
                    // Make sure the character *after* `.` is a digit, not
                    // something like `0.` at end of file.
                    if self.peek_next().map_or(false, |c| c.is_ascii_digit()) {
                        self.advance(); // consume `.`
                        digits.push('.');
                        is_float = true;
                    } else {
                        break; // treat as integer; `.` is not part of token
                    }
                }
                _ => break,
            }
        }

        if is_float {
            // `parse::<f64>()` returns a Result; we use `expect` here because
            // we only feed it strings we know are valid float syntax.
            Token::FloatLit(digits.parse::<f64>().expect("valid float"))
        } else {
            Token::IntLit(digits.parse::<i64>().expect("valid integer"))
        }
    }

    // -----------------------------------------------------------------------
    // Main scanning loop
    // -----------------------------------------------------------------------

    /// Scan and return the next `TokenWithPos`, or a `LexError`.
    ///
    /// This is the heart of the table-driven lexer.  It uses the
    /// `TRANSITION` table to decide what state to enter, then delegates to
    /// one of the token-specific scanners above to finish consuming the
    /// current token.
    ///
    /// Most single-character punctuation tokens are dispatched immediately
    /// (they are "instantly accepting"); multi-character tokens (`->`, `::`,
    /// identifiers, numbers) need the state machine.
    pub fn next_token(&mut self) -> Result<TokenWithPos, LexError> {
        // Skip whitespace and comments first so we start at real input.
        self.skip_whitespace_and_comments();

        // Record the position of the token's first character.
        let tok_line = self.line;
        let tok_col  = self.col;

        // If we have reached end-of-file, return Eof.
        let c = match self.peek() {
            None    => return Ok(TokenWithPos { token: Token::Eof, line: tok_line, col: tok_col }),
            Some(b) => b,
        };

        // Consume the character and enter the DFA.
        self.advance();
        let class = classify(c);

        // Use the transition table to decide the initial state.
        // `TRANSITION[State::Start as usize][class as usize]`
        let initial_state = TRANSITION[State::Start as usize][class as usize];

        // Now dispatch based on the initial state we landed in.
        // In most cases we hand off to a dedicated scanner; for single-char
        // tokens we produce the token directly.
        let token = match initial_state {
            // ----------------------------------------------------------------
            // Identifiers, keywords, labels, paths
            // ----------------------------------------------------------------
            State::InIdent => self.scan_ident_or_path(c),

            // ----------------------------------------------------------------
            // Local variables: %name
            // ----------------------------------------------------------------
            State::InLocal => {
                // `?` is the "propagate error" operator.  If `scan_local`
                // returns Err, we immediately return that Err from THIS
                // function too.  Think of it as a concise early-return on
                // failure.
                self.scan_local(tok_line, tok_col)?
            }

            // ----------------------------------------------------------------
            // Numeric literals
            // ----------------------------------------------------------------
            State::InIntLit => self.scan_number(c),

            // ----------------------------------------------------------------
            // `->`  (Arrow) or error
            // ----------------------------------------------------------------
            State::InMinus => {
                // We already consumed `-`.  The table says only `>` is valid
                // as the next char in this state.
                if self.peek() == Some(b'>') {
                    self.advance(); // consume `>`
                    Token::Arrow
                } else {
                    return Err(LexError {
                        message: format!(
                            "unexpected `-` (ResIR has no standalone minus; \
                             did you mean `->`?)"),
                        line: tok_line,
                        col:  tok_col,
                    });
                }
            }

            // ----------------------------------------------------------------
            // `::`  (ColonColon) or `:`  (Colon)
            // ----------------------------------------------------------------
            State::InColon => {
                if self.peek() == Some(b':') {
                    self.advance(); // consume second `:`
                    Token::ColonColon
                } else {
                    // Stand-alone colon; nothing more to consume.
                    Token::Colon
                }
            }

            // ----------------------------------------------------------------
            // Single-character punctuation that the transition table maps to
            // State::Done directly (i.e. they are complete in one character).
            // ----------------------------------------------------------------
            State::Done => {
                match c {
                    b'{'  => Token::LBrace,
                    b'}'  => Token::RBrace,
                    b'('  => Token::LParen,
                    b')'  => Token::RParen,
                    b';'  => Token::Semicolon,
                    b','  => Token::Comma,
                    b'<'  => Token::Lt,
                    b'>'  => Token::Gt,
                    b'='  => Token::Eq,
                    other => {
                        // This branch should only be hit if `classify` maps
                        // something to `Punct` that we forgot to list above.
                        return Err(LexError {
                            message: format!("unhandled punctuation `{}`", other as char),
                            line: tok_line,
                            col:  tok_col,
                        });
                    }
                }
            }

            // ----------------------------------------------------------------
            // Comments: InLineComment means the skip loop missed something;
            // this should not happen, but handle it gracefully.
            // ----------------------------------------------------------------
            State::InLineComment => {
                // Consume until end of line, then recurse to get real token.
                while let Some(ch) = self.peek() {
                    if ch == b'\n' { break; }
                    self.advance();
                }
                // Tail-recurse to get the next real token.
                return self.next_token();
            }

            // ----------------------------------------------------------------
            // Error state
            // ----------------------------------------------------------------
            State::Error | State::Start | State::InFloatFrac => {
                return Err(LexError {
                    message: format!(
                        "unexpected character `{}` (0x{:02X})",
                        c as char, c
                    ),
                    line: tok_line,
                    col:  tok_col,
                });
            }
        };

        Ok(TokenWithPos { token, line: tok_line, col: tok_col })
    }

    /// Tokenise the entire source string and return a `Vec<TokenWithPos>`.
    ///
    /// The final element is always `Token::Eof`.
    ///
    /// # Errors
    /// Returns the first `LexError` encountered.
    pub fn tokenise(&mut self) -> Result<Vec<TokenWithPos>, LexError> {
        // `Vec::new()` creates an empty growable list.
        let mut tokens = Vec::new();

        loop {
            // `?` propagates any error upward.
            let twp = self.next_token()?;
            let is_eof = twp.token == Token::Eof;
            tokens.push(twp);
            if is_eof {
                break;
            }
        }

        Ok(tokens)
    }
}

// ---------------------------------------------------------------------------
// Section 8 – Helper predicates
// ---------------------------------------------------------------------------

/// Return `true` if `s` matches the label pattern: `bb` followed by one or
/// more ASCII digits (and nothing else).
///
/// This is used after the ident scanner has collected a full word, so `s` is
/// always a complete, non-empty string.
fn is_label(s: &str) -> bool {
    // `strip_prefix` returns `Some(rest)` if `s` starts with `"bb"`.
    // We then check that `rest` is non-empty and consists only of digits.
    if let Some(rest) = s.strip_prefix("bb") {
        !rest.is_empty() && rest.bytes().all(|b| b.is_ascii_digit())
    } else {
        false
    }
}

// ---------------------------------------------------------------------------
// Section 9 – Public convenience function
// ---------------------------------------------------------------------------

/// Tokenise a source string.  This is the main entry point for the rest of
/// the compiler: give it source text, get back a token list or an error.
///
/// # Example
/// ```
/// let tokens = resir_compiler::lexer::tokenise("function Math::abs(%x: i32) -> i32 { ... }".to_string()).unwrap();
/// ```
pub fn tokenise(source: String) -> Result<Vec<TokenWithPos>, LexError> {
    Lexer::new(source).tokenise()
}

// ---------------------------------------------------------------------------
// Section 10 – Unit tests
// ---------------------------------------------------------------------------
//
// In Rust, tests live in a `#[cfg(test)]` module inside the same file.
// `cargo test` compiles and runs them; the `#[test]` attribute marks each
// individual test function.
// ---------------------------------------------------------------------------

#[cfg(test)]
mod tests {
    // `super::*` imports everything from the parent module (this file) so
    // tests can access `tokenise`, `Token`, `Keyword`, etc.
    use super::*;

    /// Helper: tokenise a string and return the plain `Token` values
    /// (without position info) for easy comparison.
    fn toks(src: &str) -> Vec<Token> {
        tokenise(src.to_string())
            .expect("tokenise failed")
            .into_iter()
            .map(|twp| twp.token)
            .collect()
    }

    // --- Keywords -----------------------------------------------------------

    #[test]
    fn test_keywords() {
        let result = toks("function locals entry");
        assert_eq!(result, vec![
            Token::Kw(Keyword::Function),
            Token::Kw(Keyword::Locals),
            Token::Kw(Keyword::Entry),
            Token::Eof,
        ]);
    }

    #[test]
    fn test_primitive_type_keywords() {
        let result = toks("i32 i64 u32 f64 bool");
        assert_eq!(result, vec![
            Token::Kw(Keyword::I32),
            Token::Kw(Keyword::I64),
            Token::Kw(Keyword::U32),
            Token::Kw(Keyword::F64),
            Token::Kw(Keyword::Bool),
            Token::Eof,
        ]);
    }

    #[test]
    fn test_boolean_literals() {
        let result = toks("true false null");
        assert_eq!(result, vec![
            Token::Kw(Keyword::True),
            Token::Kw(Keyword::False),
            Token::Kw(Keyword::Null),
            Token::Eof,
        ]);
    }

    // --- Identifiers --------------------------------------------------------

    #[test]
    fn test_plain_ident() {
        let result = toks("myVar _tmp foo123");
        assert_eq!(result, vec![
            Token::Ident("myVar".into()),
            Token::Ident("_tmp".into()),
            Token::Ident("foo123".into()),
            Token::Eof,
        ]);
    }

    #[test]
    fn test_path() {
        let result = toks("Custom::Struct::Point");
        assert_eq!(result, vec![
            Token::Path(vec!["Custom".into(), "Struct".into(), "Point".into()]),
            Token::Eof,
        ]);
    }

    #[test]
    fn test_two_segment_path() {
        let result = toks("Math::abs_diff");
        assert_eq!(result, vec![
            Token::Path(vec!["Math".into(), "abs_diff".into()]),
            Token::Eof,
        ]);
    }

    // --- Locals -------------------------------------------------------------

    #[test]
    fn test_local() {
        let result = toks("%dx %is_neg %r");
        assert_eq!(result, vec![
            Token::Local("dx".into()),
            Token::Local("is_neg".into()),
            Token::Local("r".into()),
            Token::Eof,
        ]);
    }

    // --- Labels -------------------------------------------------------------

    #[test]
    fn test_label() {
        let result = toks("bb0 bb12 bb99");
        assert_eq!(result, vec![
            Token::Label("bb0".into()),
            Token::Label("bb12".into()),
            Token::Label("bb99".into()),
            Token::Eof,
        ]);
    }

    // --- Literals -----------------------------------------------------------

    #[test]
    fn test_integer_literal() {
        let result = toks("0 42 1000");
        assert_eq!(result, vec![
            Token::IntLit(0),
            Token::IntLit(42),
            Token::IntLit(1000),
            Token::Eof,
        ]);
    }

    #[test]
    fn test_float_literal() {
        let result = toks("3.14 0.5");
        assert_eq!(result, vec![
            Token::FloatLit(3.14),
            Token::FloatLit(0.5),
            Token::Eof,
        ]);
    }

    // --- Punctuation --------------------------------------------------------

    #[test]
    fn test_arrow() {
        let result = toks("->");
        assert_eq!(result, vec![Token::Arrow, Token::Eof]);
    }

    #[test]
    fn test_colon_colon() {
        // When `::` appears outside an identifier context (unusual but valid
        // to tokenise on its own).
        let result = toks("::");
        assert_eq!(result, vec![Token::ColonColon, Token::Eof]);
    }

    #[test]
    fn test_single_colon() {
        let result = toks(":");
        assert_eq!(result, vec![Token::Colon, Token::Eof]);
    }

    #[test]
    fn test_punctuation_mix() {
        let result = toks("{ } ( ) ; , < > =");
        assert_eq!(result, vec![
            Token::LBrace, Token::RBrace,
            Token::LParen, Token::RParen,
            Token::Semicolon, Token::Comma,
            Token::Lt, Token::Gt, Token::Eq,
            Token::Eof,
        ]);
    }

    // --- Comments -----------------------------------------------------------

    #[test]
    fn test_line_comment_skipped() {
        let result = toks("i32 // this is a comment\ni64");
        assert_eq!(result, vec![
            Token::Kw(Keyword::I32),
            Token::Kw(Keyword::I64),
            Token::Eof,
        ]);
    }

    // --- Error handling -----------------------------------------------------

    #[test]
    fn test_lone_minus_is_error() {
        // A bare `-` with no `>` after it is a lexical error.
        let err = tokenise("- ".to_string());
        assert!(err.is_err(), "expected a LexError for lone `-`");
    }

    #[test]
    fn test_invalid_local_is_error() {
        // `%123` is not a valid local (ident must start with alpha or `_`).
        let err = tokenise("%123".to_string());
        assert!(err.is_err(), "expected a LexError for `%123`");
    }

    // --- Full snippet (the abs_diff example from the spec) ------------------

    #[test]
    fn test_abs_diff_snippet() {
        let src = r#"
            function Math::abs_diff(%a: i32, %b: i32) -> i32 {
              locals {
                %zero : i32;
                %d    : i32;
              }
              entry bb0;
              bb0:
                %d = bin sub %a, %b;
                cjump %d, bb1, bb2;
              bb1:
                return %d;
              bb2:
                return %d;
            }
        "#;
        // Just make sure tokenisation succeeds without an error.
        let tokens = tokenise(src.to_string());
        assert!(tokens.is_ok(), "abs_diff snippet should tokenise cleanly");
        // Check the first meaningful token is `function`.
        let ts = tokens.unwrap();
        assert_eq!(ts[0].token, Token::Kw(Keyword::Function));
    }
}