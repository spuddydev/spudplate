#ifndef SPUDPLATE_PARSER_H
#define SPUDPLATE_PARSER_H

#include <stdexcept>
#include <string>
#include <unordered_set>

#include "spudplate/ast.h"
#include "spudplate/lexer.h"

namespace spudplate {

/**
 * @brief Exception thrown when the parser encounters invalid spudlang syntax.
 *
 * Carries the source location (line and column) alongside the error message so
 * callers can produce precise diagnostics.
 */
class ParseError : public std::runtime_error {
  public:
    ParseError(const std::string& message, int line, int column)
        : std::runtime_error(message), line_(line), column_(column) {}

    /** @brief 1-based line number where the error occurred. */
    [[nodiscard]] int line() const { return line_; }
    /** @brief 1-based column number where the error occurred. */
    [[nodiscard]] int column() const { return column_; }

  private:
    int line_;
    int column_;
};

/**
 * @brief Recursive-descent parser that produces an AST from a token stream.
 *
 * Construct with a Lexer over the source text, then call parse() to obtain a
 * Program. Throws ParseError on any syntax violation.
 *
 * Individual statement parsers (parseAsk(), parseLet(), etc.) are public so
 * they can be exercised directly in unit tests.
 */
class Parser {
  public:
    /** @brief Constructs a Parser that reads tokens from the given Lexer. */
    explicit Parser(Lexer lexer);

    /** @brief Parses the full program and returns the top-level AST node. */
    Program parse();

    /** @brief Parses a single expression starting at the current token. */
    ExprPtr parseExpression();

    /** @brief Parses an `ask` statement. */
    StmtPtr parseAsk();
    /** @brief Parses a `let` statement. */
    StmtPtr parseLet();
    /** @brief Parses a `mkdir` statement. */
    StmtPtr parseMkdir();
    /** @brief Parses a `file` statement. */
    StmtPtr parseFile();
    /** @brief Parses a `repeat` block. */
    StmtPtr parseRepeat();
    /** @brief Parses a `copy` statement. */
    StmtPtr parseCopy();
    /** @brief Dispatches to the appropriate statement parser based on the current token.
     */
    StmtPtr parseStatement();

  private:
    Lexer lexer_;
    Token current_;
    std::unordered_set<std::string> aliases_;  ///< Names bound by `as <name>` in prior statements.

    // Token consumption
    Token advance();
    [[nodiscard]] bool check(TokenType type) const;
    bool match(TokenType type);
    Token expect(TokenType type, const std::string& message);
    void skip_newlines();

    // Statement helpers
    VarType parse_var_type();
    std::optional<ExprPtr> parse_when_clause();
    std::optional<int> parse_mode_clause();
    PathExpr parse_path_expr();
    ExprPtr parse_literal();
    void expect_newline(const std::string& context);

    // Expression precedence climbing
    ExprPtr parse_or();
    ExprPtr parse_and();
    ExprPtr parse_comparison();
    ExprPtr parse_addition();
    ExprPtr parse_multiplication();
    ExprPtr parse_unary();
    ExprPtr parse_primary();
};

}  // namespace spudplate

#endif  // SPUDPLATE_PARSER_H
