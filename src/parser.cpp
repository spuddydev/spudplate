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

ExprPtr Parser::make_string_expression(const std::string& raw, int line,
                                       int column) {
    if (raw.find('{') == std::string::npos) {
        auto expr = std::make_unique<Expr>();
        expr->data = StringLiteralExpr{.value = raw, .line = line, .column = column};
        return expr;
    }

    std::vector<std::variant<std::string, ExprPtr>> parts;
    std::string buf;
    std::size_t i = 0;
    while (i < raw.size()) {
        char c = raw[i];
        if (c != '{') {
            buf.push_back(c);
            ++i;
            continue;
        }
        // Find the matching close, tracking brace depth so `{f({x})}` works.
        std::size_t scan = i + 1;
        int depth = 1;
        while (scan < raw.size() && depth > 0) {
            if (raw[scan] == '{') {
                ++depth;
            } else if (raw[scan] == '}') {
                --depth;
                if (depth == 0) {
                    break;
                }
            }
            ++scan;
        }
        if (depth != 0) {
            throw ParseError("unclosed '{' in string literal", line, column);
        }
        std::string inner = raw.substr(i + 1, scan - i - 1);
        if (inner.empty()) {
            throw ParseError("empty '{}' in string literal", line, column);
        }
        if (!buf.empty()) {
            parts.emplace_back(std::move(buf));
            buf.clear();
        }
        Lexer sub_lexer(inner);
        Parser sub_parser(std::move(sub_lexer));
        ExprPtr inner_expr = sub_parser.parseExpression();
        if (!sub_parser.check(TokenType::EOF_TOKEN) &&
            !sub_parser.check(TokenType::NEWLINE)) {
            throw ParseError(
                "extra tokens in string interpolation '{" + inner + "}'", line,
                column);
        }
        parts.emplace_back(std::move(inner_expr));
        i = scan + 1;
    }
    if (!buf.empty()) {
        parts.emplace_back(std::move(buf));
    }

    auto expr = std::make_unique<Expr>();
    expr->data = TemplateStringExpr{
        .parts = std::move(parts), .line = line, .column = column};
    return expr;
}

ExprPtr Parser::parse_literal() {
    if (check(TokenType::STRING_LITERAL)) {
        Token tok = advance();
        return make_string_expression(tok.value, tok.line, tok.column);
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
        } else if (check(TokenType::MINUS)) {
            // Interior `-` is part of the path (e.g. `my-project`). Reject a
            // leading `-` so `mkdir -foo` is not confused with subtraction.
            if (buffer.empty() && path.segments.empty()) {
                break;
            }
            start_buffer(current_.line, current_.column);
            buffer += '-';
            advance();
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
        auto expr = parseExpression();
        if (std::holds_alternative<StringLiteralExpr>(expr->data) ||
            std::holds_alternative<IntegerLiteralExpr>(expr->data) ||
            std::holds_alternative<BoolLiteralExpr>(expr->data)) {
            ensure_type_match(*expr);
        }
        default_value = std::move(expr);
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

StmtPtr Parser::parseAssign() {
    Token name = expect(TokenType::IDENTIFIER,
                        "expected variable name at start of assignment");
    expect(TokenType::ASSIGN,
           "expected '=' after variable name (use 'let' to declare a new variable)");
    auto value = parseExpression();
    expect_newline("assignment");

    auto stmt = std::make_unique<Stmt>();
    stmt->data = AssignStmt{.name = name.value,
                            .value = std::move(value),
                            .line = name.line,
                            .column = name.column};
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

// Run statement parser

StmtPtr Parser::parseRun() {
    Token start = expect(TokenType::RUN, "expected 'run'");
    auto command = parseExpression();
    std::optional<PathExpr> cwd;
    if (match(TokenType::IN)) {
        cwd = parse_path_expr();
    }
    std::optional<ExprPtr> timeout_expr;
    if (check(TokenType::TIMEOUT)) {
        Token timeout_tok = advance();
        auto value = parseExpression();
        // Reject literal non-positive timeouts at parse time. Non-literal
        // expressions are validated for type and re-checked for positivity at
        // runtime.
        if (const auto* lit =
                std::get_if<IntegerLiteralExpr>(&value->data);
            lit != nullptr && lit->value <= 0) {
            throw ParseError("'timeout' must be a positive integer",
                             timeout_tok.line, timeout_tok.column);
        }
        timeout_expr = std::move(value);
    }
    auto when_clause = parse_when_clause();
    expect_newline("run statement");

    auto stmt = std::make_unique<Stmt>();
    stmt->data = RunStmt{.command = std::move(command),
                         .cwd = std::move(cwd),
                         .timeout = std::move(timeout_expr),
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

StmtPtr Parser::parseIf() {
    Token start = expect(TokenType::IF, "expected 'if'");
    ExprPtr condition = parseExpression();
    if (check(TokenType::WHEN)) {
        throw ParseError(
            "'if' does not take a 'when' clause; nest inside another 'if' if needed",
            current_.line, current_.column);
    }
    expect_newline("if header");

    std::vector<StmtPtr> body;
    skip_newlines();
    while (!check(TokenType::END) && !check(TokenType::EOF_TOKEN)) {
        body.push_back(parseStatement());
        skip_newlines();
    }

    expect(TokenType::END, "expected 'end' to close if block");
    expect_newline("if block");

    auto stmt = std::make_unique<Stmt>();
    stmt->data = IfStmt{.condition = std::move(condition),
                        .body = std::move(body),
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
    if (check(TokenType::IF)) {
        return parseIf();
    }
    if (check(TokenType::COPY)) {
        return parseCopy();
    }
    if (check(TokenType::INCLUDE)) {
        return parseInclude();
    }
    if (check(TokenType::RUN)) {
        return parseRun();
    }
    if (check(TokenType::IDENTIFIER)) {
        return parseAssign();
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
        return make_string_expression(tok.value, tok.line, tok.column);
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

        // Check for function call: name(expr) or name(expr, expr, ...)
        if (check(TokenType::LPAREN)) {
            advance();
            std::vector<ExprPtr> args;
            if (!check(TokenType::RPAREN)) {
                args.push_back(parseExpression());
                while (match(TokenType::COMMA)) {
                    args.push_back(parseExpression());
                }
            }
            expect(TokenType::RPAREN, "expected ')' after function arguments");

            auto expr = std::make_unique<Expr>();
            expr->data = FunctionCallExpr{.name = tok.value,
                                           .arguments = std::move(args),
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
