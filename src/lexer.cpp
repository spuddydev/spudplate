#include "spudplate/lexer.h"

namespace spudplate {

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

Token Lexer::nextToken() {
    skipWhitespace();

    if (isAtEnd()) {
        return Token(TokenType::EOF_TOKEN, "", line_, column_);
    }

    int startLine = line_;
    int startCol = column_;
    char ch = advance();

    return Token(TokenType::ERROR, std::string(1, ch), startLine, startCol);
}

} // namespace spudplate
