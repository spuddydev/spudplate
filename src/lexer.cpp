#include "spudplate/lexer.h"

#include <unordered_map>

namespace spudplate {

static const std::unordered_map<std::string, TokenType> keywords = {
    {"ask", TokenType::ASK},
    {"let", TokenType::LET},
    {"mkdir", TokenType::MKDIR},
    {"file", TokenType::FILE},
    {"from", TokenType::FROM},
    {"content", TokenType::CONTENT},
    {"when", TokenType::WHEN},
    {"repeat", TokenType::REPEAT},
    {"end", TokenType::END},
    {"required", TokenType::REQUIRED},
    {"verbatim", TokenType::VERBATIM},
    {"mode", TokenType::MODE},
    {"as", TokenType::AS},
    {"and", TokenType::AND},
    {"or", TokenType::OR},
    {"not", TokenType::NOT},
    {"string", TokenType::STRING_TYPE},
    {"bool", TokenType::BOOL_TYPE},
    {"int", TokenType::INT_TYPE},
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

bool Lexer::isAtEnd() const { return pos_ >= source_.size(); }

void Lexer::skipWhitespace() {
    while (!isAtEnd() && (current() == ' ' || current() == '\t')) {
        advance();
    }
}

Token Lexer::readIdentifierOrKeyword() {
    int startLine = line_;
    int startCol = column_;
    std::string value;

    while (!isAtEnd() &&
           (std::isalnum(static_cast<unsigned char>(current())) ||
            current() == '_')) {
        value += advance();
    }

    auto it = keywords.find(value);
    if (it != keywords.end()) {
        return Token(it->second, value, startLine, startCol);
    }
    return Token(TokenType::IDENTIFIER, value, startLine, startCol);
}

Token Lexer::readStringLiteral() {
    int startLine = line_;
    int startCol = column_;
    advance(); // skip opening "

    std::string value;
    while (!isAtEnd() && current() != '"') {
        value += advance();
    }

    if (isAtEnd()) {
        return Token(TokenType::ERROR, "unterminated string", startLine,
                     startCol);
    }

    advance(); // skip closing "
    return Token(TokenType::STRING_LITERAL, value, startLine, startCol);
}

Token Lexer::nextToken() {
    skipWhitespace();

    if (isAtEnd()) {
        return Token(TokenType::EOF_TOKEN, "", line_, column_);
    }

    char ch = current();

    if (std::isalpha(static_cast<unsigned char>(ch)) || ch == '_') {
        return readIdentifierOrKeyword();
    }

    if (ch == '"') {
        return readStringLiteral();
    }

    int startLine = line_;
    int startCol = column_;
    advance();

    return Token(TokenType::ERROR, std::string(1, ch), startLine, startCol);
}

} // namespace spudplate
