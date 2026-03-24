#ifndef SPUDPLATE_TOKEN_H
#define SPUDPLATE_TOKEN_H

#include <string>

namespace spudplate {

enum class TokenType {
    // Keywords
    ASK,
    LET,
    MKDIR,
    FILE,
    FROM,
    CONTENT,
    WHEN,
    REPEAT,
    END,
    REQUIRED,
    VERBATIM,
    APPEND,
    MODE,
    AS,

    // Logical operators (keywords)
    AND,
    OR,
    NOT,

    // Type keywords
    STRING_TYPE,
    BOOL_TYPE,
    INT_TYPE,

    // Literals
    STRING_LITERAL,
    INTEGER_LITERAL,
    IDENTIFIER,

    // Comparison operators
    EQUALS,
    NOT_EQUALS,
    GREATER,
    LESS,
    GREATER_EQUAL,
    LESS_EQUAL,

    // Arithmetic operators
    PLUS,
    MINUS,
    STAR,
    SLASH,

    // Assignment
    ASSIGN,

    // Structure
    NEWLINE,
    LPAREN,
    RPAREN,

    // Special
    EOF_TOKEN,
    ERROR,
};

struct Token {
    TokenType type;
    std::string value;
    int line;
    int column;

    Token() : type(TokenType::EOF_TOKEN), value(""), line(0), column(0) {}

    Token(TokenType type, std::string value, int line, int column)
        : type(type), value(std::move(value)), line(line), column(column) {}
};

inline std::string tokenTypeToString(TokenType type) {
    switch (type) {
    case TokenType::ASK: return "ASK";
    case TokenType::LET: return "LET";
    case TokenType::MKDIR: return "MKDIR";
    case TokenType::FILE: return "FILE";
    case TokenType::FROM: return "FROM";
    case TokenType::CONTENT: return "CONTENT";
    case TokenType::WHEN: return "WHEN";
    case TokenType::REPEAT: return "REPEAT";
    case TokenType::END: return "END";
    case TokenType::REQUIRED: return "REQUIRED";
    case TokenType::VERBATIM: return "VERBATIM";
    case TokenType::APPEND: return "APPEND";
    case TokenType::MODE: return "MODE";
    case TokenType::AS: return "AS";
    case TokenType::AND: return "AND";
    case TokenType::OR: return "OR";
    case TokenType::NOT: return "NOT";
    case TokenType::STRING_TYPE: return "STRING_TYPE";
    case TokenType::BOOL_TYPE: return "BOOL_TYPE";
    case TokenType::INT_TYPE: return "INT_TYPE";
    case TokenType::STRING_LITERAL: return "STRING_LITERAL";
    case TokenType::INTEGER_LITERAL: return "INTEGER_LITERAL";
    case TokenType::IDENTIFIER: return "IDENTIFIER";
    case TokenType::EQUALS: return "EQUALS";
    case TokenType::NOT_EQUALS: return "NOT_EQUALS";
    case TokenType::GREATER: return "GREATER";
    case TokenType::LESS: return "LESS";
    case TokenType::GREATER_EQUAL: return "GREATER_EQUAL";
    case TokenType::LESS_EQUAL: return "LESS_EQUAL";
    case TokenType::PLUS: return "PLUS";
    case TokenType::MINUS: return "MINUS";
    case TokenType::STAR: return "STAR";
    case TokenType::SLASH: return "SLASH";
    case TokenType::ASSIGN: return "ASSIGN";
    case TokenType::NEWLINE: return "NEWLINE";
    case TokenType::LPAREN: return "LPAREN";
    case TokenType::RPAREN: return "RPAREN";
    case TokenType::EOF_TOKEN: return "EOF_TOKEN";
    case TokenType::ERROR: return "ERROR";
    }
    return "UNKNOWN";
}

} // namespace spudplate

#endif // SPUDPLATE_TOKEN_H
