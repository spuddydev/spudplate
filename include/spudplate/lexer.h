#ifndef SPUDPLATE_LEXER_H
#define SPUDPLATE_LEXER_H

#include <cstddef>
#include <string>

#include "spudplate/token.h"

namespace spudplate {

/**
 * @brief Tokenises a spudlang source string into a stream of Token values.
 *
 * Call nextToken() repeatedly to consume tokens one at a time.
 * The lexer returns an EOF_TOKEN once the entire input has been consumed, and
 * an ERROR token for any unrecognised character.
 */
class Lexer {
  public:
    /** @brief Constructs a Lexer over the given source string. */
    explicit Lexer(std::string source);

    /** @brief Returns the next Token from the source, advancing the position. */
    Token nextToken();

  private:
    std::string source_;
    std::size_t pos_;
    int line_;
    int column_;

    [[nodiscard]] char current() const;
    char advance();
    [[nodiscard]] bool isAtEnd() const;
    void skipWhitespace();
    Token readIdentifierOrKeyword();
    Token readStringLiteral();
    Token readIntegerLiteral();
};

}  // namespace spudplate

#endif  // SPUDPLATE_LEXER_H
