#ifndef SPUDPLATE_TOKEN_H
#define SPUDPLATE_TOKEN_H

#include <string>

namespace spudplate {

/** @brief All token types produced by the Lexer. */
enum class TokenType {
    // Keywords
    ASK,       ///< `ask` — declare a user prompt
    LET,       ///< `let` — declare a derived variable
    MKDIR,     ///< `mkdir` — create a directory
    FILE,      ///< `file` — create or append to a file
    FROM,      ///< `from` — source file path for file statements
    CONTENT,   ///< `content` — inline content for file statements
    WHEN,      ///< `when` — conditional modifier
    REPEAT,    ///< `repeat` — begin a loop block
    END,       ///< `end` — close a repeat block
    VERBATIM,  ///< `verbatim` — suppress interpolation in from-file contents
    APPEND,    ///< `append` — append to file instead of overwriting
    MODE,      ///< `mode` — set file/directory permissions (octal)
    AS,        ///< `as` — bind loop iterator variable in repeat
    DEFAULT,   ///< `default` — fallback value for an ask when the user skips it
    OPTIONS,   ///< `options` — restrict an ask to a bounded list of literals
    COPY,      ///< `copy` — copy a source directory into an existing destination
    INTO,      ///< `into` — destination marker used by `copy`
    INCLUDE,   ///< `include` — run another installed template as a subprocess

    // Logical operators (keywords)
    AND,  ///< `and` — logical AND
    OR,   ///< `or`  — logical OR
    NOT,  ///< `not` — logical NOT

    // Type keywords
    STRING_TYPE,  ///< `string` type annotation
    BOOL_TYPE,    ///< `bool` type annotation
    INT_TYPE,     ///< `int` type annotation

    // Literals
    STRING_LITERAL,   ///< Quoted string literal, e.g. `"hello"`
    INTEGER_LITERAL,  ///< Integer literal, e.g. `42`
    TRUE,             ///< `true` boolean literal
    FALSE,            ///< `false` boolean literal
    IDENTIFIER,       ///< Variable or function name

    // Comparison operators
    EQUALS,         ///< `==`
    NOT_EQUALS,     ///< `!=`
    GREATER,        ///< `>`
    LESS,           ///< `<`
    GREATER_EQUAL,  ///< `>=`
    LESS_EQUAL,     ///< `<=`

    // Arithmetic operators
    PLUS,   ///< `+` — addition or string concatenation
    MINUS,  ///< `-` — subtraction
    STAR,   ///< `*` — multiplication
    SLASH,  ///< `/` — division

    // Assignment
    ASSIGN,  ///< `=`

    // Structure
    NEWLINE,  ///< Logical line break (newline or semicolon)
    LPAREN,   ///< `(`
    RPAREN,   ///< `)`
    LBRACE,   ///< `{` — open inline interpolation in path expressions
    RBRACE,   ///< `}` — close inline interpolation in path expressions
    DOT,      ///< `.` — separator in path expressions (e.g. `README.md`)

    // Special
    EOF_TOKEN,  ///< End of input
    ERROR,      ///< Lexer error (unrecognised character)
};

/** @brief A single lexical token with source location. */
struct Token {
    TokenType type;     ///< The token's classification.
    std::string value;  ///< The raw text of the token (empty for punctuation).
    int line;           ///< 1-based source line number.
    int column;         ///< 1-based source column number.

    Token() : type(TokenType::EOF_TOKEN), line(0), column(0) {}

    Token(TokenType type, std::string value, int line, int column)
        : type(type), value(std::move(value)), line(line), column(column) {}
};

/** @brief Returns a human-readable string for a TokenType value. */
inline std::string tokenTypeToString(TokenType type) {
    switch (type) {
        case TokenType::ASK:
            return "ASK";
        case TokenType::LET:
            return "LET";
        case TokenType::MKDIR:
            return "MKDIR";
        case TokenType::FILE:
            return "FILE";
        case TokenType::FROM:
            return "FROM";
        case TokenType::CONTENT:
            return "CONTENT";
        case TokenType::WHEN:
            return "WHEN";
        case TokenType::REPEAT:
            return "REPEAT";
        case TokenType::END:
            return "END";
        case TokenType::VERBATIM:
            return "VERBATIM";
        case TokenType::APPEND:
            return "APPEND";
        case TokenType::MODE:
            return "MODE";
        case TokenType::AS:
            return "AS";
        case TokenType::DEFAULT:
            return "DEFAULT";
        case TokenType::OPTIONS:
            return "OPTIONS";
        case TokenType::COPY:
            return "COPY";
        case TokenType::INTO:
            return "INTO";
        case TokenType::INCLUDE:
            return "INCLUDE";
        case TokenType::AND:
            return "AND";
        case TokenType::OR:
            return "OR";
        case TokenType::NOT:
            return "NOT";
        case TokenType::STRING_TYPE:
            return "STRING_TYPE";
        case TokenType::BOOL_TYPE:
            return "BOOL_TYPE";
        case TokenType::INT_TYPE:
            return "INT_TYPE";
        case TokenType::STRING_LITERAL:
            return "STRING_LITERAL";
        case TokenType::INTEGER_LITERAL:
            return "INTEGER_LITERAL";
        case TokenType::TRUE:
            return "TRUE";
        case TokenType::FALSE:
            return "FALSE";
        case TokenType::IDENTIFIER:
            return "IDENTIFIER";
        case TokenType::EQUALS:
            return "EQUALS";
        case TokenType::NOT_EQUALS:
            return "NOT_EQUALS";
        case TokenType::GREATER:
            return "GREATER";
        case TokenType::LESS:
            return "LESS";
        case TokenType::GREATER_EQUAL:
            return "GREATER_EQUAL";
        case TokenType::LESS_EQUAL:
            return "LESS_EQUAL";
        case TokenType::PLUS:
            return "PLUS";
        case TokenType::MINUS:
            return "MINUS";
        case TokenType::STAR:
            return "STAR";
        case TokenType::SLASH:
            return "SLASH";
        case TokenType::ASSIGN:
            return "ASSIGN";
        case TokenType::NEWLINE:
            return "NEWLINE";
        case TokenType::LPAREN:
            return "LPAREN";
        case TokenType::RPAREN:
            return "RPAREN";
        case TokenType::LBRACE:
            return "LBRACE";
        case TokenType::RBRACE:
            return "RBRACE";
        case TokenType::DOT:
            return "DOT";
        case TokenType::EOF_TOKEN:
            return "EOF_TOKEN";
        case TokenType::ERROR:
            return "ERROR";
    }
    return "UNKNOWN";
}

}  // namespace spudplate

#endif  // SPUDPLATE_TOKEN_H
