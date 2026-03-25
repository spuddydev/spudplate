#include "spudplate/parser.h"

#include <gtest/gtest.h>

#include "spudplate/ast.h"
#include "spudplate/lexer.h"

using spudplate::AskStmt;
using spudplate::BinaryExpr;
using spudplate::ExprPtr;
using spudplate::FileContentSource;
using spudplate::FileFromSource;
using spudplate::FileStmt;
using spudplate::FunctionCallExpr;
using spudplate::IdentifierExpr;
using spudplate::IntegerLiteralExpr;
using spudplate::LetStmt;
using spudplate::Lexer;
using spudplate::MkdirStmt;
using spudplate::ParseError;
using spudplate::Parser;
using spudplate::Program;
using spudplate::RepeatStmt;
using spudplate::StmtPtr;
using spudplate::StringLiteralExpr;
using spudplate::TokenType;
using spudplate::UnaryExpr;
using spudplate::VarType;

static ExprPtr parse_expr(const std::string& input) {
    Lexer lexer(input);
    Parser parser(std::move(lexer));
    return parser.parseExpression();
}

TEST(ParserTest, StringLiteral) {
    auto expr = parse_expr("\"hello\"");
    auto& lit = std::get<StringLiteralExpr>(expr->data);
    EXPECT_EQ(lit.value, "hello");
    EXPECT_EQ(lit.line, 1);
    EXPECT_EQ(lit.column, 1);
}

TEST(ParserTest, IntegerLiteral) {
    auto expr = parse_expr("42");
    auto& lit = std::get<IntegerLiteralExpr>(expr->data);
    EXPECT_EQ(lit.value, 42);
}

TEST(ParserTest, Identifier) {
    auto expr = parse_expr("my_var");
    auto& id = std::get<IdentifierExpr>(expr->data);
    EXPECT_EQ(id.name, "my_var");
}

TEST(ParserTest, BinaryAddition) {
    auto expr = parse_expr("1 + 2");
    auto& bin = std::get<BinaryExpr>(expr->data);
    EXPECT_EQ(bin.op, TokenType::PLUS);

    auto& left = std::get<IntegerLiteralExpr>(bin.left->data);
    EXPECT_EQ(left.value, 1);

    auto& right = std::get<IntegerLiteralExpr>(bin.right->data);
    EXPECT_EQ(right.value, 2);
}

TEST(ParserTest, BinaryMultiplication) {
    auto expr = parse_expr("3 * 4");
    auto& bin = std::get<BinaryExpr>(expr->data);
    EXPECT_EQ(bin.op, TokenType::STAR);
}

TEST(ParserTest, PrecedenceMulOverAdd) {
    // 1 + 2 * 3 should parse as 1 + (2 * 3)
    auto expr = parse_expr("1 + 2 * 3");
    auto& add = std::get<BinaryExpr>(expr->data);
    EXPECT_EQ(add.op, TokenType::PLUS);

    auto& left = std::get<IntegerLiteralExpr>(add.left->data);
    EXPECT_EQ(left.value, 1);

    auto& mul = std::get<BinaryExpr>(add.right->data);
    EXPECT_EQ(mul.op, TokenType::STAR);
}

TEST(ParserTest, PrecedenceSubDiv) {
    // 10 - 5 / 2 should parse as 10 - (5 / 2)
    auto expr = parse_expr("10 - 5 / 2");
    auto& sub = std::get<BinaryExpr>(expr->data);
    EXPECT_EQ(sub.op, TokenType::MINUS);

    auto& div = std::get<BinaryExpr>(sub.right->data);
    EXPECT_EQ(div.op, TokenType::SLASH);
}

TEST(ParserTest, Comparison) {
    auto expr = parse_expr("x > 5");
    auto& cmp = std::get<BinaryExpr>(expr->data);
    EXPECT_EQ(cmp.op, TokenType::GREATER);

    auto& left = std::get<IdentifierExpr>(cmp.left->data);
    EXPECT_EQ(left.name, "x");

    auto& right = std::get<IntegerLiteralExpr>(cmp.right->data);
    EXPECT_EQ(right.value, 5);
}

TEST(ParserTest, EqualityComparison) {
    auto expr = parse_expr("name == \"foo\"");
    auto& cmp = std::get<BinaryExpr>(expr->data);
    EXPECT_EQ(cmp.op, TokenType::EQUALS);
}

TEST(ParserTest, LogicalAnd) {
    auto expr = parse_expr("a and b");
    auto& bin = std::get<BinaryExpr>(expr->data);
    EXPECT_EQ(bin.op, TokenType::AND);
}

TEST(ParserTest, LogicalOr) {
    auto expr = parse_expr("a or b");
    auto& bin = std::get<BinaryExpr>(expr->data);
    EXPECT_EQ(bin.op, TokenType::OR);
}

TEST(ParserTest, UnaryNot) {
    auto expr = parse_expr("not x");
    auto& un = std::get<UnaryExpr>(expr->data);
    EXPECT_EQ(un.op, TokenType::NOT);

    auto& operand = std::get<IdentifierExpr>(un.operand->data);
    EXPECT_EQ(operand.name, "x");
}

TEST(ParserTest, CombinedPrecedence) {
    // a + b > c and d → (((a + b) > c) and d)
    auto expr = parse_expr("a + b > c and d");
    auto& and_expr = std::get<BinaryExpr>(expr->data);
    EXPECT_EQ(and_expr.op, TokenType::AND);

    auto& cmp = std::get<BinaryExpr>(and_expr.left->data);
    EXPECT_EQ(cmp.op, TokenType::GREATER);

    auto& add = std::get<BinaryExpr>(cmp.left->data);
    EXPECT_EQ(add.op, TokenType::PLUS);
}

TEST(ParserTest, FunctionCall) {
    auto expr = parse_expr("lower(name)");
    auto& call = std::get<FunctionCallExpr>(expr->data);
    EXPECT_EQ(call.name, "lower");

    auto& arg = std::get<IdentifierExpr>(call.argument->data);
    EXPECT_EQ(arg.name, "name");
}

TEST(ParserTest, FunctionCallUpper) {
    auto expr = parse_expr("upper(x)");
    auto& call = std::get<FunctionCallExpr>(expr->data);
    EXPECT_EQ(call.name, "upper");
}

TEST(ParserTest, Parenthesized) {
    // (a + b) * c → should multiply, not add at top
    auto expr = parse_expr("(a + b) * c");
    auto& mul = std::get<BinaryExpr>(expr->data);
    EXPECT_EQ(mul.op, TokenType::STAR);

    auto& add = std::get<BinaryExpr>(mul.left->data);
    EXPECT_EQ(add.op, TokenType::PLUS);
}

TEST(ParserTest, StringConcat) {
    auto expr = parse_expr("\"hello\" + \" \" + name");
    auto& outer = std::get<BinaryExpr>(expr->data);
    EXPECT_EQ(outer.op, TokenType::PLUS);

    // Left-associative: ("hello" + " ") + name
    auto& inner = std::get<BinaryExpr>(outer.left->data);
    EXPECT_EQ(inner.op, TokenType::PLUS);
}

TEST(ParserTest, ErrorUnexpectedToken) {
    EXPECT_THROW(parse_expr("+"), ParseError);
}

TEST(ParserTest, ErrorMissingCloseParen) {
    EXPECT_THROW(parse_expr("(1 + 2"), ParseError);
}

// --- Statement parsing helpers ---

static StmtPtr parse_ask(const std::string& input) {
    Lexer lexer(input);
    Parser parser(std::move(lexer));
    return parser.parseAsk();
}

static StmtPtr parse_let(const std::string& input) {
    Lexer lexer(input);
    Parser parser(std::move(lexer));
    return parser.parseLet();
}

static StmtPtr parse_mkdir(const std::string& input) {
    Lexer lexer(input);
    Parser parser(std::move(lexer));
    return parser.parseMkdir();
}

static StmtPtr parse_file(const std::string& input) {
    Lexer lexer(input);
    Parser parser(std::move(lexer));
    return parser.parseFile();
}

// --- Ask statement tests ---

TEST(ParserTest, AskBasicString) {
    auto stmt = parse_ask("ask name \"What is your name?\" string\n");
    auto& ask = std::get<AskStmt>(stmt->data);
    EXPECT_EQ(ask.name, "name");
    EXPECT_EQ(ask.prompt, "What is your name?");
    EXPECT_EQ(ask.var_type, VarType::String);
    EXPECT_FALSE(ask.required);
    EXPECT_FALSE(ask.when_clause.has_value());
    EXPECT_EQ(ask.line, 1);
}

TEST(ParserTest, AskBoolType) {
    auto stmt = parse_ask("ask use_ci \"Enable CI?\" bool\n");
    auto& ask = std::get<AskStmt>(stmt->data);
    EXPECT_EQ(ask.var_type, VarType::Bool);
}

TEST(ParserTest, AskIntType) {
    auto stmt = parse_ask("ask count \"How many?\" int\n");
    auto& ask = std::get<AskStmt>(stmt->data);
    EXPECT_EQ(ask.var_type, VarType::Int);
}

TEST(ParserTest, AskRequired) {
    auto stmt = parse_ask("ask name \"Name?\" string required\n");
    auto& ask = std::get<AskStmt>(stmt->data);
    EXPECT_TRUE(ask.required);
}

TEST(ParserTest, AskWhenClause) {
    auto stmt = parse_ask("ask port \"Port?\" int when use_server\n");
    auto& ask = std::get<AskStmt>(stmt->data);
    ASSERT_TRUE(ask.when_clause.has_value());
    auto& cond = std::get<IdentifierExpr>((*ask.when_clause)->data);
    EXPECT_EQ(cond.name, "use_server");
}

TEST(ParserTest, AskRequiredAndWhen) {
    auto stmt = parse_ask("ask port \"Port?\" int required when use_server\n");
    auto& ask = std::get<AskStmt>(stmt->data);
    EXPECT_TRUE(ask.required);
    ASSERT_TRUE(ask.when_clause.has_value());
}

TEST(ParserTest, AskWhenExpression) {
    auto stmt = parse_ask("ask x \"X?\" string when a and b\n");
    auto& ask = std::get<AskStmt>(stmt->data);
    ASSERT_TRUE(ask.when_clause.has_value());
    auto& bin = std::get<BinaryExpr>((*ask.when_clause)->data);
    EXPECT_EQ(bin.op, TokenType::AND);
}

TEST(ParserTest, AskAtEof) {
    auto stmt = parse_ask("ask name \"Name?\" string");
    auto& ask = std::get<AskStmt>(stmt->data);
    EXPECT_EQ(ask.name, "name");
}

TEST(ParserTest, AskMissingName) {
    EXPECT_THROW(parse_ask("ask \"prompt\" string\n"), ParseError);
}

TEST(ParserTest, AskMissingPrompt) {
    EXPECT_THROW(parse_ask("ask name string\n"), ParseError);
}

TEST(ParserTest, AskMissingType) {
    EXPECT_THROW(parse_ask("ask name \"prompt\"\n"), ParseError);
}

// --- Let statement tests ---

TEST(ParserTest, LetBasic) {
    auto stmt = parse_let("let x = 42\n");
    auto& let = std::get<LetStmt>(stmt->data);
    EXPECT_EQ(let.name, "x");
    auto& val = std::get<IntegerLiteralExpr>(let.value->data);
    EXPECT_EQ(val.value, 42);
}

TEST(ParserTest, LetStringExpression) {
    auto stmt = parse_let("let greeting = \"hello\" + name\n");
    auto& let = std::get<LetStmt>(stmt->data);
    EXPECT_EQ(let.name, "greeting");
    auto& bin = std::get<BinaryExpr>(let.value->data);
    EXPECT_EQ(bin.op, TokenType::PLUS);
}

TEST(ParserTest, LetFunctionCall) {
    auto stmt = parse_let("let lower_name = lower(name)\n");
    auto& let = std::get<LetStmt>(stmt->data);
    auto& call = std::get<FunctionCallExpr>(let.value->data);
    EXPECT_EQ(call.name, "lower");
}

TEST(ParserTest, LetAtEof) {
    auto stmt = parse_let("let x = 1");
    auto& let = std::get<LetStmt>(stmt->data);
    EXPECT_EQ(let.name, "x");
}

TEST(ParserTest, LetMissingAssign) {
    EXPECT_THROW(parse_let("let x 42\n"), ParseError);
}

TEST(ParserTest, LetMissingName) {
    EXPECT_THROW(parse_let("let = 42\n"), ParseError);
}

// --- Mkdir statement tests ---

TEST(ParserTest, MkdirBasic) {
    auto stmt = parse_mkdir("mkdir \"src\"\n");
    auto& mk = std::get<MkdirStmt>(stmt->data);
    EXPECT_EQ(mk.path, "src");
    EXPECT_FALSE(mk.mode.has_value());
    EXPECT_FALSE(mk.when_clause.has_value());
}

TEST(ParserTest, MkdirWithMode) {
    auto stmt = parse_mkdir("mkdir \"bin\" mode 0755\n");
    auto& mk = std::get<MkdirStmt>(stmt->data);
    EXPECT_EQ(mk.path, "bin");
    ASSERT_TRUE(mk.mode.has_value());
    EXPECT_EQ(*mk.mode, 0755);
}

TEST(ParserTest, MkdirWithWhen) {
    auto stmt = parse_mkdir("mkdir \"tests\" when use_tests\n");
    auto& mk = std::get<MkdirStmt>(stmt->data);
    ASSERT_TRUE(mk.when_clause.has_value());
    auto& cond = std::get<IdentifierExpr>((*mk.when_clause)->data);
    EXPECT_EQ(cond.name, "use_tests");
}

TEST(ParserTest, MkdirWithModeAndWhen) {
    auto stmt = parse_mkdir("mkdir \"bin\" mode 0755 when need_bin\n");
    auto& mk = std::get<MkdirStmt>(stmt->data);
    ASSERT_TRUE(mk.mode.has_value());
    EXPECT_EQ(*mk.mode, 0755);
    ASSERT_TRUE(mk.when_clause.has_value());
}

TEST(ParserTest, MkdirAtEof) {
    auto stmt = parse_mkdir("mkdir \"src\"");
    auto& mk = std::get<MkdirStmt>(stmt->data);
    EXPECT_EQ(mk.path, "src");
}

TEST(ParserTest, MkdirMissingPath) {
    EXPECT_THROW(parse_mkdir("mkdir\n"), ParseError);
}

// --- File statement tests ---

TEST(ParserTest, FileFromBasic) {
    auto stmt = parse_file("file \"out.txt\" from \"template.txt\"\n");
    auto& file = std::get<FileStmt>(stmt->data);
    EXPECT_EQ(file.path, "out.txt");
    EXPECT_FALSE(file.append);
    auto& src = std::get<FileFromSource>(file.source);
    EXPECT_EQ(src.path, "template.txt");
    EXPECT_FALSE(src.verbatim);
}

TEST(ParserTest, FileFromVerbatim) {
    auto stmt = parse_file("file \"out.c\" from \"main.c\" verbatim\n");
    auto& file = std::get<FileStmt>(stmt->data);
    auto& src = std::get<FileFromSource>(file.source);
    EXPECT_TRUE(src.verbatim);
}

TEST(ParserTest, FileContentExpression) {
    auto stmt = parse_file("file \"readme.md\" content \"# \" + name\n");
    auto& file = std::get<FileStmt>(stmt->data);
    auto& src = std::get<FileContentSource>(file.source);
    auto& bin = std::get<BinaryExpr>(src.value->data);
    EXPECT_EQ(bin.op, TokenType::PLUS);
}

TEST(ParserTest, FileWithMode) {
    auto stmt = parse_file("file \"run.sh\" from \"run.sh\" mode 0755\n");
    auto& file = std::get<FileStmt>(stmt->data);
    ASSERT_TRUE(file.mode.has_value());
    EXPECT_EQ(*file.mode, 0755);
}

TEST(ParserTest, FileWithWhen) {
    auto stmt = parse_file("file \"ci.yml\" from \"ci.yml\" when use_ci\n");
    auto& file = std::get<FileStmt>(stmt->data);
    ASSERT_TRUE(file.when_clause.has_value());
}

TEST(ParserTest, FileFromVerbatimModeWhen) {
    auto stmt =
        parse_file("file \"main.c\" from \"main.c\" verbatim mode 0644 when need_c\n");
    auto& file = std::get<FileStmt>(stmt->data);
    auto& src = std::get<FileFromSource>(file.source);
    EXPECT_TRUE(src.verbatim);
    ASSERT_TRUE(file.mode.has_value());
    EXPECT_EQ(*file.mode, 0644);
    ASSERT_TRUE(file.when_clause.has_value());
}

TEST(ParserTest, FileContentWithMode) {
    auto stmt = parse_file("file \"x\" content \"data\" mode 0444\n");
    auto& file = std::get<FileStmt>(stmt->data);
    std::get<FileContentSource>(file.source);  // NOLINT
    ASSERT_TRUE(file.mode.has_value());
    EXPECT_EQ(*file.mode, 0444);
}

TEST(ParserTest, FileAtEof) {
    auto stmt = parse_file("file \"out.txt\" from \"in.txt\"");
    auto& file = std::get<FileStmt>(stmt->data);
    EXPECT_EQ(file.path, "out.txt");
}

TEST(ParserTest, FileMissingSource) {
    EXPECT_THROW(parse_file("file \"out.txt\"\n"), ParseError);
}

TEST(ParserTest, FileMissingPath) {
    EXPECT_THROW(parse_file("file\n"), ParseError);
}

TEST(ParserTest, FileFromMissingSourcePath) {
    EXPECT_THROW(parse_file("file \"out.txt\" from\n"), ParseError);
}

// --- Append tests ---

TEST(ParserTest, FileAppendFrom) {
    auto stmt = parse_file("file \"log.txt\" append from \"entry.txt\"\n");
    auto& file = std::get<FileStmt>(stmt->data);
    EXPECT_EQ(file.path, "log.txt");
    EXPECT_TRUE(file.append);
    auto& src = std::get<FileFromSource>(file.source);
    EXPECT_EQ(src.path, "entry.txt");
    EXPECT_FALSE(src.verbatim);
}

TEST(ParserTest, FileAppendContent) {
    auto stmt = parse_file("file \"log.txt\" append content \"new line\"\n");
    auto& file = std::get<FileStmt>(stmt->data);
    EXPECT_TRUE(file.append);
    auto& src = std::get<FileContentSource>(file.source);
    auto& lit = std::get<StringLiteralExpr>(src.value->data);
    EXPECT_EQ(lit.value, "new line");
}

TEST(ParserTest, FileAppendFromVerbatim) {
    auto stmt = parse_file("file \"out\" append from \"src\" verbatim\n");
    auto& file = std::get<FileStmt>(stmt->data);
    EXPECT_TRUE(file.append);
    auto& src = std::get<FileFromSource>(file.source);
    EXPECT_TRUE(src.verbatim);
}

TEST(ParserTest, FileAppendFromModeWhen) {
    auto stmt = parse_file("file \"out\" append from \"src\" mode 0644 when active\n");
    auto& file = std::get<FileStmt>(stmt->data);
    EXPECT_TRUE(file.append);
    auto& src = std::get<FileFromSource>(file.source);
    EXPECT_EQ(src.path, "src");
    ASSERT_TRUE(file.mode.has_value());
    EXPECT_EQ(*file.mode, 0644);
    ASSERT_TRUE(file.when_clause.has_value());
}

TEST(ParserTest, FileWithoutAppendDefaultsFalse) {
    auto stmt = parse_file("file \"out\" from \"src\"\n");
    auto& file = std::get<FileStmt>(stmt->data);
    EXPECT_FALSE(file.append);
}

// --- Repeat and program parsing helpers ---

static Program parse_program(const std::string& input) {
    Lexer lexer(input);
    Parser parser(std::move(lexer));
    return parser.parse();
}

// --- Program parsing tests ---

TEST(ParserTest, SingleStatementProgram) {
    auto program = parse_program("ask name \"Name?\" string\n");
    ASSERT_EQ(program.statements.size(), 1);
    auto& ask = std::get<AskStmt>(program.statements[0]->data);
    EXPECT_EQ(ask.name, "name");
}

TEST(ParserTest, MultiStatementWithBlankLinesAndComments) {
    auto program = parse_program(
        "# A comment\n"
        "\n"
        "ask name \"Name?\" string\n"
        "\n"
        "# Another comment\n"
        "let lower_name = lower(name)\n"
        "\n"
        "mkdir \"src\"\n");
    ASSERT_EQ(program.statements.size(), 3);
    std::get<AskStmt>(program.statements[0]->data);
    std::get<LetStmt>(program.statements[1]->data);
    std::get<MkdirStmt>(program.statements[2]->data);
}

// --- Repeat block tests ---

TEST(ParserTest, RepeatBasic) {
    auto program = parse_program(
        "repeat items as item\n"
        "  mkdir \"dir\"\n"
        "end\n");
    ASSERT_EQ(program.statements.size(), 1);
    auto& rep = std::get<RepeatStmt>(program.statements[0]->data);
    EXPECT_EQ(rep.collection_var, "items");
    EXPECT_EQ(rep.iterator_var, "item");
    ASSERT_EQ(rep.body.size(), 1);
    std::get<MkdirStmt>(rep.body[0]->data);
}

TEST(ParserTest, RepeatMultipleBodyStatements) {
    auto program = parse_program(
        "repeat modules as mod\n"
        "  mkdir \"src\"\n"
        "  file \"main.c\" from \"template.c\"\n"
        "  let x = 1\n"
        "end\n");
    ASSERT_EQ(program.statements.size(), 1);
    auto& rep = std::get<RepeatStmt>(program.statements[0]->data);
    ASSERT_EQ(rep.body.size(), 3);
    std::get<MkdirStmt>(rep.body[0]->data);
    std::get<FileStmt>(rep.body[1]->data);
    std::get<LetStmt>(rep.body[2]->data);
}

TEST(ParserTest, RepeatNested) {
    auto program = parse_program(
        "repeat outer as o\n"
        "  repeat inner as i\n"
        "    mkdir \"dir\"\n"
        "  end\n"
        "end\n");
    ASSERT_EQ(program.statements.size(), 1);
    auto& outer = std::get<RepeatStmt>(program.statements[0]->data);
    ASSERT_EQ(outer.body.size(), 1);
    auto& inner = std::get<RepeatStmt>(outer.body[0]->data);
    EXPECT_EQ(inner.collection_var, "inner");
    EXPECT_EQ(inner.iterator_var, "i");
    ASSERT_EQ(inner.body.size(), 1);
    std::get<MkdirStmt>(inner.body[0]->data);
}

// --- Full integration test ---

TEST(ParserTest, FullSpudFile) {
    auto program = parse_program(
        "# Project scaffolding\n"
        "\n"
        "ask name \"Project name?\" string required\n"
        "ask use_ci \"Enable CI?\" bool\n"
        "ask num_modules \"How many modules?\" int\n"
        "\n"
        "let lower_name = lower(name)\n"
        "\n"
        "mkdir \"src\"\n"
        "mkdir \"tests\" when use_ci\n"
        "\n"
        "file \"README.md\" content \"# \" + name\n"
        "file \"main.c\" from \"templates/main.c\" mode 0644\n"
        "\n"
        "repeat modules as mod\n"
        "  mkdir \"src\"\n"
        "  file \"mod.c\" from \"templates/mod.c\"\n"
        "end\n");
    ASSERT_EQ(program.statements.size(), 9);
    std::get<AskStmt>(program.statements[0]->data);
    std::get<AskStmt>(program.statements[1]->data);
    std::get<AskStmt>(program.statements[2]->data);
    std::get<LetStmt>(program.statements[3]->data);
    std::get<MkdirStmt>(program.statements[4]->data);
    std::get<MkdirStmt>(program.statements[5]->data);
    std::get<FileStmt>(program.statements[6]->data);
    std::get<FileStmt>(program.statements[7]->data);
    auto& rep = std::get<RepeatStmt>(program.statements[8]->data);
    ASSERT_EQ(rep.body.size(), 2);
}

// --- Error tests ---

TEST(ParserTest, RepeatWithoutEnd) {
    EXPECT_THROW(parse_program("repeat items as item\n"
                               "  mkdir \"dir\"\n"),
                 ParseError);
}

TEST(ParserTest, UnexpectedTokenAtTopLevel) {
    EXPECT_THROW(parse_program("42\n"), ParseError);
}
