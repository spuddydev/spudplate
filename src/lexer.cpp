#include "spudplate/lexer.h"

#include <unordered_map>

namespace spudplate {

static const std::unordered_map<std::string, TokenType> keywords = {
    {"ask", TokenType::ASK},           {"let", TokenType::LET},
    {"mkdir", TokenType::MKDIR},       {"file", TokenType::FILE},
    {"from", TokenType::FROM},         {"content", TokenType::CONTENT},
    {"when", TokenType::WHEN},         {"repeat", TokenType::REPEAT},
    {"end", TokenType::END},           {"required", TokenType::REQUIRED},
    {"verbatim", TokenType::VERBATIM}, {"append", TokenType::APPEND},
    {"mode", TokenType::MODE},         {"as", TokenType::AS},
    {"and", TokenType::AND},           {"or", TokenType::OR},
    {"not", TokenType::NOT},           {"string", TokenType::STRING_TYPE},
    {"bool", TokenType::BOOL_TYPE},    {"int", TokenType::INT_TYPE},
    {"true", TokenType::TRUE},         {"false", TokenType::FALSE},
};

Lexer::Lexer(std::string source)
    : source_(std::move(source)), pos_(0), line_(1), column_(1) {}

char Lexer::current() const {
    if (pos_ >= source_.size()) {
        return '\0';
    }
    return source_[pos_];
}

char Lexer::advance() {
    char ch = source_[pos_];
    pos_++;
    if (ch == '\n') {
        line_++;
        column_ = 1;
    } else {
        column_++;
    }
    return ch;
}

bool Lexer::isAtEnd() const {
    return pos_ >= source_.size();
}

void Lexer::skipWhitespace() {
    while (!isAtEnd() && (current() == ' ' || current() == '\t')) {
        advance();
    }
}

Token Lexer::readIdentifierOrKeyword() {
    int start_line = line_;
    int start_col = column_;
    std::string value;

    while (!isAtEnd() && (std::isalnum(static_cast<unsigned char>(current())) != 0 ||
                          current() == '_')) {
        value += advance();
    }

    auto it = keywords.find(value);
    if (it != keywords.end()) {
        return Token(it->second, value, start_line, start_col);
    }
    return Token(TokenType::IDENTIFIER, value, start_line, start_col);
}

Token Lexer::readStringLiteral() {
    int start_line = line_;
    int start_col = column_;
    advance();  // skip opening "

    std::string value;
    while (!isAtEnd() && current() != '"') {
        value += advance();
    }

    if (isAtEnd()) {
        return Token(TokenType::ERROR, "unterminated string", start_line, start_col);
    }

    advance();  // skip closing "
    return Token(TokenType::STRING_LITERAL, value, start_line, start_col);
}

Token Lexer::readIntegerLiteral() {
    int start_line = line_;
    int start_col = column_;
    std::string value;

    while (!isAtEnd() && std::isdigit(static_cast<unsigned char>(current())) != 0) {
        value += advance();
    }

    return Token(TokenType::INTEGER_LITERAL, value, start_line, start_col);
}

void Lexer::skipLineContinuation() {
    while (!isAtEnd() && current() == '\\') {
        std::size_t peek = pos_ + 1;
        while (peek < source_.size() && (source_[peek] == ' ' || source_[peek] == '\t')) {
            peek++;
        }
        if (peek < source_.size() && source_[peek] == '\n') {
            while (pos_ <= peek) {
                advance();
            }
            skipWhitespace();
        } else {
            break;
        }
    }
}

Token Lexer::nextToken() {
    skipWhitespace();
    skipLineContinuation();

    if (isAtEnd()) {
        return Token(TokenType::EOF_TOKEN, "", line_, column_);
    }

    if (current() == '\n') {
        int start_line = line_;
        int start_col = column_;
        advance();
        return Token(TokenType::NEWLINE, "\\n", start_line, start_col);
    }

    if (current() == '#') {
        while (!isAtEnd() && current() != '\n') {
            advance();
        }
        return nextToken();
    }

    char ch = current();

    if (std::isalpha(static_cast<unsigned char>(ch)) != 0 || ch == '_') {
        return readIdentifierOrKeyword();
    }

    if (ch == '"') {
        return readStringLiteral();
    }

    if (std::isdigit(static_cast<unsigned char>(ch)) != 0) {
        return readIntegerLiteral();
    }

    int start_line = line_;
    int start_col = column_;
    advance();

    switch (ch) {
        case '+':
            return Token(TokenType::PLUS, "+", start_line, start_col);
        case '-':
            return Token(TokenType::MINUS, "-", start_line, start_col);
        case '*':
            return Token(TokenType::STAR, "*", start_line, start_col);
        case '/':
            return Token(TokenType::SLASH, "/", start_line, start_col);
        case '(':
            return Token(TokenType::LPAREN, "(", start_line, start_col);
        case ')':
            return Token(TokenType::RPAREN, ")", start_line, start_col);
        case '{':
            return Token(TokenType::LBRACE, "{", start_line, start_col);
        case '}':
            return Token(TokenType::RBRACE, "}", start_line, start_col);
        case '.':
            return Token(TokenType::DOT, ".", start_line, start_col);
        case '=':
            if (!isAtEnd() && current() == '=') {
                advance();
                return Token(TokenType::EQUALS, "==", start_line, start_col);
            }
            return Token(TokenType::ASSIGN, "=", start_line, start_col);
        case '!':
            if (!isAtEnd() && current() == '=') {
                advance();
                return Token(TokenType::NOT_EQUALS, "!=", start_line, start_col);
            }
            return Token(TokenType::ERROR, "!", start_line, start_col);
        case '>':
            if (!isAtEnd() && current() == '=') {
                advance();
                return Token(TokenType::GREATER_EQUAL, ">=", start_line, start_col);
            }
            return Token(TokenType::GREATER, ">", start_line, start_col);
        case '<':
            if (!isAtEnd() && current() == '=') {
                advance();
                return Token(TokenType::LESS_EQUAL, "<=", start_line, start_col);
            }
            return Token(TokenType::LESS, "<", start_line, start_col);
        default:
            return Token(TokenType::ERROR, std::string(1, ch), start_line, start_col);
    }
}

}  // namespace spudplate
