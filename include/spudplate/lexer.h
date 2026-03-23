#ifndef SPUDPLATE_LEXER_H
#define SPUDPLATE_LEXER_H

#include "spudplate/token.h"

#include <cstddef>
#include <string>

namespace spudplate {

class Lexer {
  public:
    explicit Lexer(std::string source);

    Token nextToken();

  private:
    std::string source_;
    std::size_t pos_;
    int line_;
    int column_;

    char current() const;
    char advance();
    bool isAtEnd() const;
    void skipWhitespace();
    Token readIdentifierOrKeyword();
    Token readStringLiteral();
};

} // namespace spudplate

#endif // SPUDPLATE_LEXER_H
