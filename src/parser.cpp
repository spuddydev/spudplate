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

bool Parser::check(TokenType type) const {
    return current_.type == type;
}

bool Parser::match(TokenType type) {
    if (check(type)) {
        advance();
        return true;
    }
    return false;
}

Token Parser::expect(TokenType type, const std::string& message) {
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
    if (match(TokenType::STRING_TYPE)) {
        return VarType::String;
    }
    if (match(TokenType::BOOL_TYPE)) {
        return VarType::Bool;
    }
    if (match(TokenType::INT_TYPE)) {
        return VarType::Int;
    }
    throw ParseError("expected type (string, bool, or int)", current_.line,
                     current_.column);
}

std::optional<ExprPtr> Parser::parse_when_clause() {
    if (!match(TokenType::WHEN)) {
        return std::nullopt;
    }
    return parseExpression();
}

std::optional<int> Parser::parse_mode_clause() {
    if (!match(TokenType::MODE)) {
        return std::nullopt;
    }
    Token tok = expect(TokenType::INTEGER_LITERAL, "expected octal integer after 'mode'");
    return std::stoi(tok.value, nullptr, 8);
}

void Parser::expect_newline(const std::string& context) {
    if (check(TokenType::EOF_TOKEN)) {
        return;
    }
    expect(TokenType::NEWLINE, "expected newline after " + context);
}

ExprPtr Parser::parse_literal() {
    if (check(TokenType::STRING_LITERAL)) {
        Token tok = advance();
        auto expr = std::make_unique<Expr>();
        expr->data =
            StringLiteralExpr{.value = tok.value, .line = tok.line, .column = tok.column};
        return expr;
    }
    if (check(TokenType::INTEGER_LITERAL)) {
        Token tok = advance();
        auto expr = std::make_unique<Expr>();
        expr->data = IntegerLiteralExpr{
            .value = std::stoi(tok.value), .line = tok.line, .column = tok.column};
        return expr;
    }
    if (check(TokenType::TRUE) || check(TokenType::FALSE)) {
        Token tok = advance();
        auto expr = std::make_unique<Expr>();
        expr->data = BoolLiteralExpr{.value = tok.type == TokenType::TRUE,
                                     .line = tok.line,
                                     .column = tok.column};
        return expr;
    }
    throw ParseError("expected literal value", current_.line, current_.column);
}

PathExpr Parser::parse_path_expr() {
    PathExpr path;
    path.line = current_.line;
    path.column = current_.column;

    // Quoted-string fallback for paths with spaces: `mkdir "my notes"`
    if (check(TokenType::STRING_LITERAL)) {
        Token tok = advance();
        path.segments.push_back(
            PathLiteral{.value = tok.value, .line = tok.line, .column = tok.column});
        return path;
    }

    // Unquoted path: greedily consume IDENTIFIER / SLASH / DOT / INTEGER_LITERAL
    // and `{expr}` interpolation blocks. Adjacent non-alias, non-interpolation
    // tokens coalesce into a single PathLiteral via a text buffer.
    std::string buffer;
    int buf_line = 0;
    int buf_col = 0;

    auto flush_buffer = [&]() {
        if (!buffer.empty()) {
            path.segments.push_back(PathLiteral{
                .value = std::move(buffer), .line = buf_line, .column = buf_col});
            buffer.clear();
        }
    };

    auto start_buffer = [&](int line, int col) {
        if (buffer.empty()) {
            buf_line = line;
            buf_col = col;
        }
    };

    while (true) {
        if (check(TokenType::IDENTIFIER)) {
            Token tok = advance();
            if (aliases_.contains(tok.value)) {
                flush_buffer();
                path.segments.push_back(
                    PathVar{.name = tok.value, .line = tok.line, .column = tok.column});
            } else {
                start_buffer(tok.line, tok.column);
                buffer += tok.value;
            }
        } else if (check(TokenType::SLASH)) {
            start_buffer(current_.line, current_.column);
            buffer += '/';
            advance();
        } else if (check(TokenType::DOT)) {
            start_buffer(current_.line, current_.column);
            buffer += '.';
            advance();
        } else if (check(TokenType::INTEGER_LITERAL)) {
            Token tok = advance();
            start_buffer(tok.line, tok.column);
            buffer += tok.value;
        } else if (check(TokenType::LBRACE)) {
            int line = current_.line;
            int col = current_.column;
            advance();
            flush_buffer();
            auto expr = parseExpression();
            expect(TokenType::RBRACE, "expected '}' to close path interpolation");
            path.segments.push_back(PathInterp{
                .expression = std::move(expr), .line = line, .column = col});
        } else {
            break;
        }
    }

    flush_buffer();

    if (path.segments.empty()) {
        throw ParseError("expected path expression", current_.line, current_.column);
    }

    return path;
}

// Statement parsers

StmtPtr Parser::parseAsk() {
    Token start = expect(TokenType::ASK, "expected 'ask'");
    Token name = expect(TokenType::IDENTIFIER, "expected variable name after 'ask'");
    Token prompt = expect(TokenType::STRING_LITERAL, "expected prompt string after name");
    VarType var_type = parse_var_type();

    auto is_literal_start = [this]() {
        return check(TokenType::STRING_LITERAL) || check(TokenType::INTEGER_LITERAL) ||
               check(TokenType::TRUE) || check(TokenType::FALSE);
    };

    auto type_matches = [&](const Expr& lit) {
        switch (var_type) {
            case VarType::String:
                return std::holds_alternative<StringLiteralExpr>(lit.data);
            case VarType::Bool:
                return std::holds_alternative<BoolLiteralExpr>(lit.data);
            case VarType::Int:
                return std::holds_alternative<IntegerLiteralExpr>(lit.data);
        }
        return false;
    };

    auto ensure_type_match = [&](const Expr& lit) {
        if (type_matches(lit)) {
            return;
        }
        int l = 0;
        int c = 0;
        std::visit([&](const auto& n) {
            l = n.line;
            c = n.column;
        }, lit.data);
        throw ParseError("literal type does not match ask type", l, c);
    };

    std::vector<ExprPtr> options;
    if (match(TokenType::OPTIONS)) {
        if (!is_literal_start()) {
            throw ParseError("expected at least one literal after 'options'",
                             current_.line, current_.column);
        }
        while (is_literal_start()) {
            auto lit = parse_literal();
            ensure_type_match(*lit);
            options.push_back(std::move(lit));
        }
    }

    std::optional<ExprPtr> default_value;
    if (match(TokenType::DEFAULT)) {
        if (!is_literal_start()) {
            throw ParseError("expected literal after 'default'", current_.line,
                             current_.column);
        }
        auto lit = parse_literal();
        ensure_type_match(*lit);
        default_value = std::move(lit);
    }

    auto when_clause = parse_when_clause();
    expect_newline("ask statement");

    auto stmt = std::make_unique<Stmt>();
    stmt->data = AskStmt{.name = name.value,
                         .prompt = prompt.value,
                         .var_type = var_type,
                         .default_value = std::move(default_value),
                         .options = std::move(options),
                         .when_clause = std::move(when_clause),
                         .line = start.line,
                         .column = start.column};
    return stmt;
}

StmtPtr Parser::parseLet() {
    Token start = expect(TokenType::LET, "expected 'let'");
    Token name = expect(TokenType::IDENTIFIER, "expected variable name after 'let'");
    expect(TokenType::ASSIGN, "expected '=' after variable name");
    auto value = parseExpression();
    expect_newline("let statement");

    auto stmt = std::make_unique<Stmt>();
    stmt->data = LetStmt{.name = name.value,
                         .value = std::move(value),
                         .line = start.line,
                         .column = start.column};
    return stmt;
}

StmtPtr Parser::parseMkdir() {
    Token start = expect(TokenType::MKDIR, "expected 'mkdir'");
    PathExpr path = parse_path_expr();

    std::optional<PathExpr> from_source;
    bool verbatim = false;
    if (match(TokenType::FROM)) {
        from_source = parse_path_expr();
        verbatim = match(TokenType::VERBATIM);
    }

    auto mode = parse_mode_clause();
    auto when_clause = parse_when_clause();

    std::optional<std::string> alias;
    if (match(TokenType::AS)) {
        Token alias_tok = expect(TokenType::IDENTIFIER, "expected alias name after 'as'");
        alias = alias_tok.value;
        aliases_.insert(*alias);
    }

    expect_newline("mkdir statement");

    auto stmt = std::make_unique<Stmt>();
    stmt->data = MkdirStmt{.path = std::move(path),
                            .alias = std::move(alias),
                            .mkdir_p = true,
                            .from_source = std::move(from_source),
                            .verbatim = verbatim,
                            .mode = mode,
                            .when_clause = std::move(when_clause),
                            .line = start.line,
                            .column = start.column};
    return stmt;
}

StmtPtr Parser::parseFile() {
    Token start = expect(TokenType::FILE, "expected 'file'");
    PathExpr path = parse_path_expr();

    bool append = match(TokenType::APPEND);

    FileSource source = [&]() -> FileSource {
        if (match(TokenType::FROM)) {
            PathExpr src = parse_path_expr();
            bool verbatim = match(TokenType::VERBATIM);
            return FileFromSource{.path = std::move(src), .verbatim = verbatim};
        }
        if (match(TokenType::CONTENT)) {
            auto value = parseExpression();
            return FileContentSource{std::move(value)};
        }
        throw ParseError("expected 'from' or 'content' after file path", current_.line,
                         current_.column);
    }();

    auto mode = parse_mode_clause();
    auto when_clause = parse_when_clause();

    std::optional<std::string> alias;
    if (match(TokenType::AS)) {
        Token alias_tok = expect(TokenType::IDENTIFIER, "expected alias name after 'as'");
        alias = alias_tok.value;
        aliases_.insert(*alias);
    }

    expect_newline("file statement");

    auto stmt = std::make_unique<Stmt>();
    stmt->data = FileStmt{.path = std::move(path),
                          .alias = std::move(alias),
                          .source = std::move(source),
                          .append = append,
                          .mode = mode,
                          .when_clause = std::move(when_clause),
                          .line = start.line,
                          .column = start.column};
    return stmt;
}

// Copy statement parser

StmtPtr Parser::parseCopy() {
    Token start = expect(TokenType::COPY, "expected 'copy'");
    PathExpr source = parse_path_expr();
    expect(TokenType::INTO, "expected 'into' after copy source path");
    PathExpr destination = parse_path_expr();
    bool verbatim = match(TokenType::VERBATIM);
    auto when_clause = parse_when_clause();
    expect_newline("copy statement");

    auto stmt = std::make_unique<Stmt>();
    stmt->data = CopyStmt{.source = std::move(source),
                          .destination = std::move(destination),
                          .verbatim = verbatim,
                          .when_clause = std::move(when_clause),
                          .line = start.line,
                          .column = start.column};
    return stmt;
}

// Include statement parser

StmtPtr Parser::parseInclude() {
    Token start = expect(TokenType::INCLUDE, "expected 'include'");
    Token name =
        expect(TokenType::IDENTIFIER, "expected template name after 'include'");
    auto when_clause = parse_when_clause();
    expect_newline("include statement");

    auto stmt = std::make_unique<Stmt>();
    stmt->data = IncludeStmt{.name = name.value,
                             .when_clause = std::move(when_clause),
                             .line = start.line,
                             .column = start.column};
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
    auto when_clause = parse_when_clause();
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
    stmt->data = RepeatStmt{.collection_var = collection.value,
                             .iterator_var = iterator.value,
                             .body = std::move(body),
                             .when_clause = std::move(when_clause),
                             .line = start.line,
                             .column = start.column};
    return stmt;
}

// Statement dispatch

StmtPtr Parser::parseStatement() {
    if (check(TokenType::ASK)) {
        return parseAsk();
    }
    if (check(TokenType::LET)) {
        return parseLet();
    }
    if (check(TokenType::MKDIR)) {
        return parseMkdir();
    }
    if (check(TokenType::FILE)) {
        return parseFile();
    }
    if (check(TokenType::REPEAT)) {
        return parseRepeat();
    }
    if (check(TokenType::COPY)) {
        return parseCopy();
    }
    if (check(TokenType::INCLUDE)) {
        return parseInclude();
    }
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

ExprPtr Parser::parseExpression() {
    return parse_or();
}

ExprPtr Parser::parse_or() {
    auto left = parse_and();

    while (check(TokenType::OR)) {
        int line = current_.line;
        int col = current_.column;
        advance();
        auto right = parse_and();

        auto expr = std::make_unique<Expr>();
        expr->data = BinaryExpr{.op = TokenType::OR,
                                 .left = std::move(left),
                                 .right = std::move(right),
                                 .line = line,
                                 .column = col};
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
        expr->data = BinaryExpr{.op = TokenType::AND,
                                 .left = std::move(left),
                                 .right = std::move(right),
                                 .line = line,
                                 .column = col};
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
        expr->data = BinaryExpr{.op = op,
                                 .left = std::move(left),
                                 .right = std::move(right),
                                 .line = line,
                                 .column = col};
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
        expr->data = BinaryExpr{.op = op,
                                 .left = std::move(left),
                                 .right = std::move(right),
                                 .line = line,
                                 .column = col};
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
        expr->data = BinaryExpr{.op = op,
                                 .left = std::move(left),
                                 .right = std::move(right),
                                 .line = line,
                                 .column = col};
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
        expr->data = UnaryExpr{.op = TokenType::NOT,
                                .operand = std::move(operand),
                                .line = line,
                                .column = col};
        return expr;
    }

    return parse_primary();
}

ExprPtr Parser::parse_primary() {
    if (check(TokenType::STRING_LITERAL)) {
        Token tok = advance();
        auto expr = std::make_unique<Expr>();
        expr->data =
            StringLiteralExpr{.value = tok.value, .line = tok.line, .column = tok.column};
        return expr;
    }

    if (check(TokenType::INTEGER_LITERAL)) {
        Token tok = advance();
        auto expr = std::make_unique<Expr>();
        expr->data = IntegerLiteralExpr{
            .value = std::stoi(tok.value), .line = tok.line, .column = tok.column};
        return expr;
    }

    if (check(TokenType::TRUE) || check(TokenType::FALSE)) {
        Token tok = advance();
        auto expr = std::make_unique<Expr>();
        expr->data = BoolLiteralExpr{.value = tok.type == TokenType::TRUE,
                                     .line = tok.line,
                                     .column = tok.column};
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
            expr->data = FunctionCallExpr{.name = tok.value,
                                           .argument = std::move(arg),
                                           .line = tok.line,
                                           .column = tok.column};
            return expr;
        }

        auto expr = std::make_unique<Expr>();
        expr->data =
            IdentifierExpr{.name = tok.value, .line = tok.line, .column = tok.column};
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

}  // namespace spudplate
