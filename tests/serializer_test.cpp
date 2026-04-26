#include "spudplate/serializer.h"

#include <gtest/gtest.h>

#include <memory>
#include <string>

#include "spudplate/ast.h"
#include "spudplate/lexer.h"
#include "spudplate/parser.h"

using namespace spudplate;

namespace {

ExprPtr make(ExprData data) {
    return std::unique_ptr<Expr>(new Expr{std::move(data)});
}

StmtPtr make_s(StmtData data) {
    return std::unique_ptr<Stmt>(new Stmt{std::move(data)});
}

Program parse_source(const std::string& src) {
    Parser parser(Lexer{src});
    return parser.parse();
}

Program round_trip(const Program& p) {
    return program_from_json(program_to_json(p));
}

}  // namespace

// ---------------------------------------------------------------------------
// Top-level shape
// ---------------------------------------------------------------------------

TEST(SerializerTest, EmitsFormatVersionAndStatementsArray) {
    Program p;
    auto j = program_to_json(p);
    ASSERT_TRUE(j.is_object());
    EXPECT_EQ(j.at("format_version"), SPUDPLATE_FORMAT_VERSION);
    ASSERT_TRUE(j.at("statements").is_array());
    EXPECT_EQ(j.at("statements").size(), 0u);
}

TEST(SerializerTest, FormatVersionMismatchThrows) {
    nlohmann::json j = {{"format_version", 999},
                        {"statements", nlohmann::json::array()}};
    EXPECT_THROW(program_from_json(j), DeserializeError);
}

TEST(SerializerTest, MissingFormatVersionThrows) {
    nlohmann::json j = {{"statements", nlohmann::json::array()}};
    EXPECT_THROW(program_from_json(j), DeserializeError);
}

TEST(SerializerTest, RootMustBeObject) {
    EXPECT_THROW(program_from_json(nlohmann::json::array()), DeserializeError);
}

// ---------------------------------------------------------------------------
// Per-expression-variant round trips (8 alternatives in ExprData)
// ---------------------------------------------------------------------------

TEST(SerializerTest, RoundTripStringLiteralExpr) {
    Program p;
    p.statements.push_back(make_s(LetStmt{
        .name = "s",
        .value = make(StringLiteralExpr{.value = "hi", .line = 1, .column = 9}),
        .line = 1,
        .column = 1}));
    auto rt = round_trip(p);
    EXPECT_TRUE(programs_equal(p, rt));
}

TEST(SerializerTest, RoundTripIntegerLiteralExpr) {
    Program p;
    p.statements.push_back(make_s(LetStmt{
        .name = "n",
        .value = make(IntegerLiteralExpr{.value = 42, .line = 1, .column = 9}),
        .line = 1,
        .column = 1}));
    auto rt = round_trip(p);
    EXPECT_TRUE(programs_equal(p, rt));
}

TEST(SerializerTest, RoundTripBoolLiteralExpr) {
    Program p;
    p.statements.push_back(make_s(LetStmt{
        .name = "b",
        .value = make(BoolLiteralExpr{.value = true, .line = 1, .column = 9}),
        .line = 1,
        .column = 1}));
    auto rt = round_trip(p);
    EXPECT_TRUE(programs_equal(p, rt));
}

TEST(SerializerTest, RoundTripIdentifierExpr) {
    Program p;
    p.statements.push_back(make_s(LetStmt{
        .name = "x",
        .value = make(IdentifierExpr{.name = "y", .line = 1, .column = 9}),
        .line = 1,
        .column = 1}));
    auto rt = round_trip(p);
    EXPECT_TRUE(programs_equal(p, rt));
}

TEST(SerializerTest, RoundTripUnaryExpr) {
    Program p;
    p.statements.push_back(make_s(LetStmt{
        .name = "x",
        .value = make(UnaryExpr{
            .op = TokenType::NOT,
            .operand = make(BoolLiteralExpr{.value = false, .line = 1, .column = 13}),
            .line = 1,
            .column = 9}),
        .line = 1,
        .column = 1}));
    auto rt = round_trip(p);
    EXPECT_TRUE(programs_equal(p, rt));
}

TEST(SerializerTest, RoundTripBinaryExpr) {
    Program p;
    p.statements.push_back(make_s(LetStmt{
        .name = "x",
        .value = make(BinaryExpr{
            .op = TokenType::PLUS,
            .left = make(IntegerLiteralExpr{.value = 1, .line = 1, .column = 9}),
            .right = make(IntegerLiteralExpr{.value = 2, .line = 1, .column = 13}),
            .line = 1,
            .column = 11}),
        .line = 1,
        .column = 1}));
    auto rt = round_trip(p);
    EXPECT_TRUE(programs_equal(p, rt));
}

TEST(SerializerTest, RoundTripFunctionCallExpr) {
    std::vector<ExprPtr> args;
    args.push_back(make(StringLiteralExpr{.value = "Hi", .line = 1, .column = 16}));
    Program p;
    p.statements.push_back(make_s(LetStmt{
        .name = "x",
        .value = make(FunctionCallExpr{
            .name = "lower",
            .arguments = std::move(args),
            .line = 1,
            .column = 9}),
        .line = 1,
        .column = 1}));
    auto rt = round_trip(p);
    EXPECT_TRUE(programs_equal(p, rt));
}

TEST(SerializerTest, RoundTripTemplateStringExpr) {
    std::vector<std::variant<std::string, ExprPtr>> parts;
    parts.emplace_back(std::string("hi "));
    parts.emplace_back(make(IdentifierExpr{.name = "n", .line = 1, .column = 14}));
    parts.emplace_back(std::string("!"));
    Program p;
    p.statements.push_back(make_s(LetStmt{
        .name = "x",
        .value = make(TemplateStringExpr{
            .parts = std::move(parts),
            .line = 1,
            .column = 9}),
        .line = 1,
        .column = 1}));
    auto rt = round_trip(p);
    EXPECT_TRUE(programs_equal(p, rt));
}

// ---------------------------------------------------------------------------
// Path segment shapes (3 alternatives in PathSegment)
// ---------------------------------------------------------------------------

TEST(SerializerTest, RoundTripPathLiteralPathVarPathInterp) {
    auto src = parse_source(
        "mkdir base as basepath\n"
        "mkdir basepath/week_{n}\n");
    auto rt = round_trip(src);
    EXPECT_TRUE(programs_equal(src, rt));
}

// ---------------------------------------------------------------------------
// File source shapes (2 alternatives in FileSource)
// ---------------------------------------------------------------------------

TEST(SerializerTest, RoundTripFileFromSource) {
    auto src = parse_source("file dst/main.cpp from src/main.cpp\n");
    auto rt = round_trip(src);
    EXPECT_TRUE(programs_equal(src, rt));
}

TEST(SerializerTest, RoundTripFileContentSource) {
    auto src = parse_source("file readme.md content \"hello\"\n");
    auto rt = round_trip(src);
    EXPECT_TRUE(programs_equal(src, rt));
}

// ---------------------------------------------------------------------------
// Per-statement-variant round trips (9 alternatives in StmtData)
// ---------------------------------------------------------------------------

TEST(SerializerTest, RoundTripAskStmt) {
    auto src = parse_source(
        "ask name \"Project name?\" string default \"demo\"\n");
    auto rt = round_trip(src);
    EXPECT_TRUE(programs_equal(src, rt));
}

TEST(SerializerTest, RoundTripAskStmtWithOptions) {
    auto src = parse_source(
        "ask format \"Format?\" string options \"pdf\" \"html\"\n");
    auto rt = round_trip(src);
    EXPECT_TRUE(programs_equal(src, rt));
}

TEST(SerializerTest, RoundTripLetStmt) {
    auto src = parse_source("let slug = lower(\"X\")\n");
    auto rt = round_trip(src);
    EXPECT_TRUE(programs_equal(src, rt));
}

TEST(SerializerTest, RoundTripAssignStmt) {
    auto src = parse_source("let n = 1\nn = n + 1\n");
    auto rt = round_trip(src);
    EXPECT_TRUE(programs_equal(src, rt));
}

TEST(SerializerTest, RoundTripMkdirStmt) {
    auto src = parse_source("mkdir src/modules mode 0755 as srcpath\n");
    auto rt = round_trip(src);
    EXPECT_TRUE(programs_equal(src, rt));
}

TEST(SerializerTest, RoundTripMkdirFromSource) {
    auto src = parse_source("mkdir templates from base_templates\n");
    auto rt = round_trip(src);
    EXPECT_TRUE(programs_equal(src, rt));
}

TEST(SerializerTest, RoundTripFileStmt) {
    auto src = parse_source("file readme.md append content \"x\" mode 0644\n");
    auto rt = round_trip(src);
    EXPECT_TRUE(programs_equal(src, rt));
}

TEST(SerializerTest, RoundTripRepeatStmt) {
    auto src = parse_source(
        "ask n \"How many?\" int default 3\n"
        "repeat n as i\n"
        "  mkdir week_{i}\n"
        "end\n");
    auto rt = round_trip(src);
    EXPECT_TRUE(programs_equal(src, rt));
}

TEST(SerializerTest, RoundTripCopyStmt) {
    auto src = parse_source(
        "mkdir templates as templatepath\n"
        "copy extras into templatepath\n");
    auto rt = round_trip(src);
    EXPECT_TRUE(programs_equal(src, rt));
}

TEST(SerializerTest, RoundTripIncludeStmt) {
    auto src = parse_source("include claude_setup\n");
    auto rt = round_trip(src);
    EXPECT_TRUE(programs_equal(src, rt));
}

TEST(SerializerTest, RoundTripRunStmt) {
    auto src = parse_source("run \"git init\"\n");
    auto rt = round_trip(src);
    EXPECT_TRUE(programs_equal(src, rt));
}

// ---------------------------------------------------------------------------
// End-to-end: a realistic mixed source survives the round trip
// ---------------------------------------------------------------------------

TEST(SerializerTest, RoundTripMixedRealisticProgram) {
    auto src = parse_source(
        "ask project \"Project?\" string default \"demo\"\n"
        "ask use_git \"Use git?\" bool default true\n"
        "ask weeks \"Weeks?\" int default 2\n"
        "let slug = lower(project)\n"
        "mkdir {slug} as root\n"
        "file root/README.md content \"# \" + project\n"
        "repeat weeks as w when weeks > 0\n"
        "  mkdir week_{w}\n"
        "  file week_{w}/notes.md content \"hi\"\n"
        "end\n"
        "run \"git init\" in {slug} when use_git\n");
    auto rt = round_trip(src);
    EXPECT_TRUE(programs_equal(src, rt));
}

// ---------------------------------------------------------------------------
// Error reporting
// ---------------------------------------------------------------------------

TEST(SerializerTest, UnknownStatementTypeThrows) {
    nlohmann::json stmt = {{"type", "Bogus"}, {"line", 1}, {"column", 1}};
    nlohmann::json root = {{"format_version", SPUDPLATE_FORMAT_VERSION},
                           {"statements", nlohmann::json::array({stmt})}};
    EXPECT_THROW(program_from_json(root), DeserializeError);
}

TEST(SerializerTest, MissingFieldThrows) {
    nlohmann::json stmt = {{"type", "Let"}, {"line", 1}, {"column", 1}};
    nlohmann::json root = {{"format_version", SPUDPLATE_FORMAT_VERSION},
                           {"statements", nlohmann::json::array({stmt})}};
    EXPECT_THROW(program_from_json(root), DeserializeError);
}

TEST(SerializerTest, ErrorCarriesPointer) {
    nlohmann::json stmt = {{"type", "Let"}, {"line", 1}, {"column", 1}};
    nlohmann::json root = {{"format_version", SPUDPLATE_FORMAT_VERSION},
                           {"statements", nlohmann::json::array({stmt})}};
    try {
        program_from_json(root);
        FAIL() << "expected DeserializeError";
    } catch (const DeserializeError& e) {
        EXPECT_FALSE(e.json_pointer().empty());
    }
}
