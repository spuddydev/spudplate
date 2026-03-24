#include "spudplate/parser.h"

namespace spudplate {

Parser::Parser(Lexer lexer) : lexer_(std::move(lexer)) {
    current_ = lexer_.nextToken();
}

Token Parser::advance() {
    Token prev = current_;
    current_ = lexer_.nextToken();
    return prev;
}

bool Parser::check(TokenType type) const { return current_.type == type; }

bool Parser::match(TokenType type) {
    if (check(type)) {
        advance();
        return true;
    }
    return false;
}

Token Parser::expect(TokenType type, const std::string &message) {
    if (check(type)) {
        return advance();
    }
    throw ParseError(message, current_.line, current_.column);
}

void Parser::skip_newlines() {
    while (check(TokenType::NEWLINE)) {
        advance();
    }
}

// Statement helpers

VarType Parser::parse_var_type() {
    if (match(TokenType::STRING_TYPE))
        return VarType::String;
    if (match(TokenType::BOOL_TYPE))
        return VarType::Bool;
    if (match(TokenType::INT_TYPE))
        return VarType::Int;
    throw ParseError("expected type (string, bool, or int)", current_.line,
                     current_.column);
}

std::optional<ExprPtr> Parser::parse_when_clause() {
    if (!match(TokenType::WHEN))
        return std::nullopt;
    return parseExpression();
}

std::optional<int> Parser::parse_mode_clause() {
    if (!match(TokenType::MODE))
        return std::nullopt;
    Token tok = expect(TokenType::INTEGER_LITERAL,
                       "expected octal integer after 'mode'");
    return std::stoi(tok.value, nullptr, 8);
}

void Parser::expect_newline(const std::string &context) {
    if (check(TokenType::EOF_TOKEN))
        return;
    expect(TokenType::NEWLINE, "expected newline after " + context);
}

// Statement parsers

StmtPtr Parser::parseAsk() {
    Token start = expect(TokenType::ASK, "expected 'ask'");
    Token name = expect(TokenType::IDENTIFIER, "expected variable name after 'ask'");
    Token prompt =
        expect(TokenType::STRING_LITERAL, "expected prompt string after name");
    VarType var_type = parse_var_type();
    bool required = match(TokenType::REQUIRED);
    auto when_clause = parse_when_clause();
    expect_newline("ask statement");

    auto stmt = std::make_unique<Stmt>();
    stmt->data = AskStmt{name.value,         prompt.value,          var_type,
                    required,            std::move(when_clause), start.line,
                    start.column};
    return stmt;
}

StmtPtr Parser::parseLet() {
    Token start = expect(TokenType::LET, "expected 'let'");
    Token name =
        expect(TokenType::IDENTIFIER, "expected variable name after 'let'");
    expect(TokenType::ASSIGN, "expected '=' after variable name");
    auto value = parseExpression();
    expect_newline("let statement");

    auto stmt = std::make_unique<Stmt>();
    stmt->data = LetStmt{name.value, std::move(value), start.line, start.column};
    return stmt;
}

StmtPtr Parser::parseMkdir() {
    Token start = expect(TokenType::MKDIR, "expected 'mkdir'");
    Token path =
        expect(TokenType::STRING_LITERAL, "expected path string after 'mkdir'");
    auto mode = parse_mode_clause();
    auto when_clause = parse_when_clause();
    expect_newline("mkdir statement");

    auto stmt = std::make_unique<Stmt>();
    stmt->data = MkdirStmt{path.value, mode, std::move(when_clause), start.line,
                      start.column};
    return stmt;
}

StmtPtr Parser::parseFile() {
    Token start = expect(TokenType::FILE, "expected 'file'");
    Token path =
        expect(TokenType::STRING_LITERAL, "expected path string after 'file'");

    bool append = match(TokenType::APPEND);

    FileSource source = [&]() -> FileSource {
        if (match(TokenType::FROM)) {
            Token src = expect(TokenType::STRING_LITERAL,
                               "expected source path after 'from'");
            bool verbatim = match(TokenType::VERBATIM);
            return FileFromSource{src.value, verbatim};
        }
        if (match(TokenType::CONTENT)) {
            auto value = parseExpression();
            return FileContentSource{std::move(value)};
        }
        throw ParseError("expected 'from' or 'content' after file path",
                         current_.line, current_.column);
    }();

    auto mode = parse_mode_clause();
    auto when_clause = parse_when_clause();
    expect_newline("file statement");

    auto stmt = std::make_unique<Stmt>();
    stmt->data = FileStmt{path.value, std::move(source), append, mode,
                     std::move(when_clause), start.line, start.column};
    return stmt;
}

// Repeat block parser

StmtPtr Parser::parseRepeat() {
    Token start = expect(TokenType::REPEAT, "expected 'repeat'");
    Token collection =
        expect(TokenType::IDENTIFIER, "expected collection variable after 'repeat'");
    expect(TokenType::AS, "expected 'as' after collection variable");
    Token iterator =
        expect(TokenType::IDENTIFIER, "expected iterator variable after 'as'");
    expect_newline("repeat header");

    std::vector<StmtPtr> body;
    skip_newlines();
    while (!check(TokenType::END) && !check(TokenType::EOF_TOKEN)) {
        body.push_back(parseStatement());
        skip_newlines();
    }

    expect(TokenType::END, "expected 'end' to close repeat block");
    expect_newline("repeat block");

    auto stmt = std::make_unique<Stmt>();
    stmt->data = RepeatStmt{collection.value, iterator.value, std::move(body),
                            start.line, start.column};
    return stmt;
}

// Statement dispatch

StmtPtr Parser::parseStatement() {
    if (check(TokenType::ASK))
        return parseAsk();
    if (check(TokenType::LET))
        return parseLet();
    if (check(TokenType::MKDIR))
        return parseMkdir();
    if (check(TokenType::FILE))
        return parseFile();
    if (check(TokenType::REPEAT))
        return parseRepeat();
    throw ParseError("unexpected token: " + current_.value, current_.line,
                     current_.column);
}

// Full program parser

Program Parser::parse() {
    Program program;
    skip_newlines();
    while (!check(TokenType::EOF_TOKEN)) {
        program.statements.push_back(parseStatement());
        skip_newlines();
    }
    return program;
}

// Expression precedence: or < and < comparison < addition < multiplication <
// unary < primary

ExprPtr Parser::parseExpression() { return parse_or(); }

ExprPtr Parser::parse_or() {
    auto left = parse_and();

    while (check(TokenType::OR)) {
        int line = current_.line;
        int col = current_.column;
        advance();
        auto right = parse_and();

        auto expr = std::make_unique<Expr>();
        expr->data = BinaryExpr{TokenType::OR, std::move(left), std::move(right),
                                line, col};
        left = std::move(expr);
    }

    return left;
}

ExprPtr Parser::parse_and() {
    auto left = parse_comparison();

    while (check(TokenType::AND)) {
        int line = current_.line;
        int col = current_.column;
        advance();
        auto right = parse_comparison();

        auto expr = std::make_unique<Expr>();
        expr->data = BinaryExpr{TokenType::AND, std::move(left),
                                std::move(right), line, col};
        left = std::move(expr);
    }

    return left;
}

ExprPtr Parser::parse_comparison() {
    auto left = parse_addition();

    while (check(TokenType::EQUALS) || check(TokenType::NOT_EQUALS) ||
           check(TokenType::GREATER) || check(TokenType::LESS) ||
           check(TokenType::GREATER_EQUAL) || check(TokenType::LESS_EQUAL)) {
        TokenType op = current_.type;
        int line = current_.line;
        int col = current_.column;
        advance();
        auto right = parse_addition();

        auto expr = std::make_unique<Expr>();
        expr->data =
            BinaryExpr{op, std::move(left), std::move(right), line, col};
        left = std::move(expr);
    }

    return left;
}

ExprPtr Parser::parse_addition() {
    auto left = parse_multiplication();

    while (check(TokenType::PLUS) || check(TokenType::MINUS)) {
        TokenType op = current_.type;
        int line = current_.line;
        int col = current_.column;
        advance();
        auto right = parse_multiplication();

        auto expr = std::make_unique<Expr>();
        expr->data =
            BinaryExpr{op, std::move(left), std::move(right), line, col};
        left = std::move(expr);
    }

    return left;
}

ExprPtr Parser::parse_multiplication() {
    auto left = parse_unary();

    while (check(TokenType::STAR) || check(TokenType::SLASH)) {
        TokenType op = current_.type;
        int line = current_.line;
        int col = current_.column;
        advance();
        auto right = parse_unary();

        auto expr = std::make_unique<Expr>();
        expr->data =
            BinaryExpr{op, std::move(left), std::move(right), line, col};
        left = std::move(expr);
    }

    return left;
}

ExprPtr Parser::parse_unary() {
    if (check(TokenType::NOT)) {
        int line = current_.line;
        int col = current_.column;
        advance();
        auto operand = parse_unary();

        auto expr = std::make_unique<Expr>();
        expr->data = UnaryExpr{TokenType::NOT, std::move(operand), line, col};
        return expr;
    }

    return parse_primary();
}

ExprPtr Parser::parse_primary() {
    if (check(TokenType::STRING_LITERAL)) {
        Token tok = advance();
        auto expr = std::make_unique<Expr>();
        expr->data = StringLiteralExpr{tok.value, tok.line, tok.column};
        return expr;
    }

    if (check(TokenType::INTEGER_LITERAL)) {
        Token tok = advance();
        auto expr = std::make_unique<Expr>();
        expr->data =
            IntegerLiteralExpr{std::stoi(tok.value), tok.line, tok.column};
        return expr;
    }

    if (check(TokenType::IDENTIFIER)) {
        Token tok = advance();

        // Check for function call: name(expr)
        if (check(TokenType::LPAREN)) {
            advance();
            auto arg = parseExpression();
            expect(TokenType::RPAREN, "expected ')' after function argument");

            auto expr = std::make_unique<Expr>();
            expr->data = FunctionCallExpr{tok.value, std::move(arg), tok.line,
                                          tok.column};
            return expr;
        }

        auto expr = std::make_unique<Expr>();
        expr->data = IdentifierExpr{tok.value, tok.line, tok.column};
        return expr;
    }

    if (check(TokenType::LPAREN)) {
        advance();
        auto inner = parseExpression();
        expect(TokenType::RPAREN, "expected ')' after expression");
        return inner;
    }

    throw ParseError("unexpected token: " + current_.value, current_.line,
                     current_.column);
}

} // namespace spudplate
