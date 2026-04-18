// =============================================================================
// token.cpp  —  Implementation of token_kind_name()
// =============================================================================
// This tiny file just provides a human-readable string for each TokenKind.
// It is used by error messages throughout the compiler.

#include "token.hpp"

const char* token_kind_name(TokenKind k) {
    switch (k) {
        // Keywords
        case TokenKind::KW_FUNCTION:   return "function";
        case TokenKind::KW_LOCALS:     return "locals";
        case TokenKind::KW_ENTRY:      return "entry";
        case TokenKind::KW_CONST:      return "const";
        case TokenKind::KW_CAST:       return "cast";
        case TokenKind::KW_UN:         return "un";
        case TokenKind::KW_BIN:        return "bin";
        case TokenKind::KW_ADDR_OF:    return "addr_of";
        case TokenKind::KW_MEMBER_PTR: return "member_ptr";
        case TokenKind::KW_LOAD:       return "load";
        case TokenKind::KW_STORE:      return "store";
        case TokenKind::KW_CALL:       return "call";
        case TokenKind::KW_JUMP:       return "jump";
        case TokenKind::KW_CJUMP:      return "cjump";
        case TokenKind::KW_RETURN:     return "return";
        case TokenKind::KW_VOID:       return "void";
        case TokenKind::KW_PTR:        return "ptr";
        case TokenKind::KW_TO:         return "to";
        case TokenKind::KW_EXTERN:     return "extern";
        case TokenKind::KW_TYPE:       return "type";
        case TokenKind::KW_NEG:        return "neg";
        case TokenKind::KW_NOT:        return "not";
        case TokenKind::KW_ADD:        return "add";
        case TokenKind::KW_SUB:        return "sub";
        case TokenKind::KW_MUL:        return "mul";
        case TokenKind::KW_DIV:        return "div";
        case TokenKind::KW_MOD:        return "mod";
        case TokenKind::KW_EQ:         return "eq";
        case TokenKind::KW_NE:         return "ne";
        case TokenKind::KW_LT:         return "lt";
        case TokenKind::KW_LE:         return "le";
        case TokenKind::KW_GT_OP:      return "gt";
        case TokenKind::KW_GE:         return "ge";
        case TokenKind::KW_AND:        return "and";
        case TokenKind::KW_OR:         return "or";
        // Primitive types
        case TokenKind::KW_BOOL:       return "bool";
        case TokenKind::KW_I32:        return "i32";
        case TokenKind::KW_I64:        return "i64";
        case TokenKind::KW_U32:        return "u32";
        case TokenKind::KW_F64:        return "f64";
        // Literal keywords
        case TokenKind::KW_TRUE:       return "true";
        case TokenKind::KW_FALSE:      return "false";
        case TokenKind::KW_NULL:       return "null";
        // Non-keyword token types
        case TokenKind::IDENT:         return "<identifier>";
        case TokenKind::LOCAL:         return "<local>";
        case TokenKind::LABEL:         return "<label>";
        case TokenKind::INT_LIT:       return "<integer-literal>";
        case TokenKind::FLOAT_LIT:     return "<float-literal>";
        // Punctuation
        case TokenKind::LBRACE:        return "{";
        case TokenKind::RBRACE:        return "}";
        case TokenKind::LPAREN:        return "(";
        case TokenKind::RPAREN:        return ")";
        case TokenKind::COLON:         return ":";
        case TokenKind::SEMICOLON:     return ";";
        case TokenKind::COMMA:         return ",";
        case TokenKind::ARROW:         return "->";
        case TokenKind::LT:            return "<";
        case TokenKind::GT:            return ">";
        case TokenKind::EQ:            return "=";
        case TokenKind::COLON_COLON:   return "::";
        case TokenKind::END_OF_FILE:   return "<EOF>";
        default:                       return "<unknown>";
    }
}
