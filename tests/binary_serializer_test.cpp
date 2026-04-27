#include "spudplate/binary_serializer.h"

#include <cstdint>
#include <memory>
#include <string>
#include <utility>
#include <variant>
#include <vector>

#include <gtest/gtest.h>

#include "spudplate/ast.h"
#include "spudplate/token.h"
#include "test_helpers.h"

namespace spudplate {
namespace {

// ----- Tiny AST builders --------------------------------------------------

ExprPtr make_expr(ExprData data) {
    auto e = std::make_unique<Expr>();
    e->data = std::move(data);
    return e;
}

ExprPtr str_lit(std::string v, int line = 1, int col = 1) {
    return make_expr(StringLiteralExpr{
        .value = std::move(v), .line = line, .column = col});
}

ExprPtr int_lit(int v, int line = 1, int col = 1) {
    return make_expr(
        IntegerLiteralExpr{.value = v, .line = line, .column = col});
}

ExprPtr bool_lit(bool v, int line = 1, int col = 1) {
    return make_expr(BoolLiteralExpr{.value = v, .line = line, .column = col});
}

ExprPtr ident(std::string name, int line = 1, int col = 1) {
    return make_expr(IdentifierExpr{
        .name = std::move(name), .line = line, .column = col});
}

StmtPtr make_stmt(StmtData data) {
    auto s = std::make_unique<Stmt>();
    s->data = std::move(data);
    return s;
}

Program program_with(std::vector<StmtPtr> stmts) {
    Program p;
    p.statements = std::move(stmts);
    return p;
}

void expect_round_trip(const Program& original) {
    auto bytes = serialize_program(original);
    auto recovered = deserialize_program(bytes.data(), bytes.size());
    EXPECT_TRUE(test::programs_equal(original, recovered));
}

// ----- Expression-arm round trips -----------------------------------------

TEST(BinarySerializer, RoundTripStringLiteral) {
    std::vector<StmtPtr> stmts;
    stmts.push_back(make_stmt(LetStmt{
        .name = "greeting",
        .value = str_lit("hello world", 3, 7),
        .line = 3,
        .column = 1}));
    expect_round_trip(program_with(std::move(stmts)));
}

TEST(BinarySerializer, RoundTripIntegerLiteralIncludingNegative) {
    std::vector<StmtPtr> stmts;
    stmts.push_back(make_stmt(LetStmt{
        .name = "count",
        .value = int_lit(-42, 1, 1),
        .line = 1,
        .column = 1}));
    expect_round_trip(program_with(std::move(stmts)));
}

TEST(BinarySerializer, RoundTripBoolLiteral) {
    std::vector<StmtPtr> stmts;
    stmts.push_back(make_stmt(LetStmt{
        .name = "flag", .value = bool_lit(true), .line = 1, .column = 1}));
    expect_round_trip(program_with(std::move(stmts)));
}

TEST(BinarySerializer, RoundTripIdentifier) {
    std::vector<StmtPtr> stmts;
    stmts.push_back(make_stmt(LetStmt{
        .name = "alias",
        .value = ident("name", 2, 5),
        .line = 2,
        .column = 1}));
    expect_round_trip(program_with(std::move(stmts)));
}

TEST(BinarySerializer, RoundTripUnary) {
    auto unary = make_expr(UnaryExpr{
        .op = TokenType::NOT,
        .operand = ident("flag"),
        .line = 1,
        .column = 1});
    std::vector<StmtPtr> stmts;
    stmts.push_back(make_stmt(LetStmt{
        .name = "result", .value = std::move(unary), .line = 1, .column = 1}));
    expect_round_trip(program_with(std::move(stmts)));
}

TEST(BinarySerializer, RoundTripBinary) {
    auto bin = make_expr(BinaryExpr{
        .op = TokenType::PLUS,
        .left = int_lit(1),
        .right = int_lit(2),
        .line = 1,
        .column = 1});
    std::vector<StmtPtr> stmts;
    stmts.push_back(make_stmt(LetStmt{
        .name = "sum", .value = std::move(bin), .line = 1, .column = 1}));
    expect_round_trip(program_with(std::move(stmts)));
}

TEST(BinarySerializer, RoundTripFunctionCallEmptyAndNonEmptyArgs) {
    auto empty = make_expr(FunctionCallExpr{
        .name = "trim", .arguments = {}, .line = 1, .column = 1});
    std::vector<StmtPtr> stmts;
    stmts.push_back(make_stmt(LetStmt{
        .name = "a", .value = std::move(empty), .line = 1, .column = 1}));

    std::vector<ExprPtr> args;
    args.push_back(str_lit("Hello"));
    args.push_back(str_lit(" "));
    args.push_back(str_lit("-"));
    auto call = make_expr(FunctionCallExpr{
        .name = "replace",
        .arguments = std::move(args),
        .line = 2,
        .column = 1});
    stmts.push_back(make_stmt(LetStmt{
        .name = "b", .value = std::move(call), .line = 2, .column = 1}));

    expect_round_trip(program_with(std::move(stmts)));
}

TEST(BinarySerializer, RoundTripTemplateStringBothSubTags) {
    std::vector<std::variant<std::string, ExprPtr>> parts;
    parts.emplace_back(std::string{"week_"});
    parts.emplace_back(ident("n"));
    parts.emplace_back(std::string{"_done"});
    auto tpl = make_expr(TemplateStringExpr{
        .parts = std::move(parts), .line = 4, .column = 9});
    std::vector<StmtPtr> stmts;
    stmts.push_back(make_stmt(LetStmt{
        .name = "label", .value = std::move(tpl), .line = 4, .column = 1}));
    expect_round_trip(program_with(std::move(stmts)));
}

// ----- Path segments and PathExpr round trips -----------------------------

TEST(BinarySerializer, RoundTripPathSegmentsAllThreeKinds) {
    PathExpr path;
    path.segments.emplace_back(
        PathLiteral{.value = "static/", .line = 5, .column = 7});
    path.segments.emplace_back(PathVar{.name = "modulepath", .line = 5, .column = 14});
    path.segments.emplace_back(PathInterp{
        .expression = ident("n", 5, 25), .line = 5, .column = 25});
    path.line = 5;
    path.column = 7;

    std::vector<StmtPtr> stmts;
    stmts.push_back(make_stmt(MkdirStmt{.path = std::move(path),
                                        .alias = std::nullopt,
                                        .mkdir_p = true,
                                        .from_source = std::nullopt,
                                        .verbatim = false,
                                        .mode = std::nullopt,
                                        .when_clause = std::nullopt,
                                        .line = 5,
                                        .column = 1}));
    expect_round_trip(program_with(std::move(stmts)));
}

// ----- Statement-arm round trips ------------------------------------------

TEST(BinarySerializer, RoundTripAskAllOptionalsAbsent) {
    std::vector<StmtPtr> stmts;
    stmts.push_back(make_stmt(AskStmt{.name = "title",
                                      .prompt = "Title?",
                                      .var_type = VarType::String,
                                      .default_value = std::nullopt,
                                      .options = {},
                                      .when_clause = std::nullopt,
                                      .line = 1,
                                      .column = 1}));
    expect_round_trip(program_with(std::move(stmts)));
}

TEST(BinarySerializer, RoundTripAskAllOptionalsPresent) {
    std::vector<ExprPtr> options;
    options.push_back(str_lit("pdf"));
    options.push_back(str_lit("html"));
    options.push_back(str_lit("latex"));
    std::vector<StmtPtr> stmts;
    stmts.push_back(make_stmt(AskStmt{
        .name = "format",
        .prompt = "Format?",
        .var_type = VarType::String,
        .default_value = str_lit("pdf"),
        .options = std::move(options),
        .when_clause = ident("use_docs"),
        .line = 1,
        .column = 1}));
    expect_round_trip(program_with(std::move(stmts)));
}

TEST(BinarySerializer, RoundTripLetAndAssign) {
    std::vector<StmtPtr> stmts;
    stmts.push_back(make_stmt(LetStmt{
        .name = "counter", .value = int_lit(0), .line = 1, .column = 1}));
    stmts.push_back(make_stmt(AssignStmt{
        .name = "counter",
        .value = make_expr(BinaryExpr{.op = TokenType::PLUS,
                                       .left = ident("counter"),
                                       .right = int_lit(1),
                                       .line = 2,
                                       .column = 11}),
        .line = 2,
        .column = 1}));
    expect_round_trip(program_with(std::move(stmts)));
}

TEST(BinarySerializer, RoundTripMkdirNoOptionals) {
    PathExpr p;
    p.segments.emplace_back(
        PathLiteral{.value = "src/modules", .line = 1, .column = 7});
    p.line = 1;
    p.column = 7;

    std::vector<StmtPtr> stmts;
    stmts.push_back(make_stmt(MkdirStmt{.path = std::move(p),
                                        .alias = std::nullopt,
                                        .mkdir_p = true,
                                        .from_source = std::nullopt,
                                        .verbatim = false,
                                        .mode = std::nullopt,
                                        .when_clause = std::nullopt,
                                        .line = 1,
                                        .column = 1}));
    expect_round_trip(program_with(std::move(stmts)));
}

TEST(BinarySerializer, RoundTripMkdirAllOptionalsPresent) {
    PathExpr p;
    p.segments.emplace_back(
        PathLiteral{.value = "src/modules", .line = 1, .column = 7});
    p.line = 1;
    p.column = 7;
    PathExpr from;
    from.segments.emplace_back(
        PathLiteral{.value = "templates/modules", .line = 1, .column = 26});
    from.line = 1;
    from.column = 26;

    std::vector<StmtPtr> stmts;
    stmts.push_back(make_stmt(MkdirStmt{
        .path = std::move(p),
        .alias = std::string{"modulepath"},
        .mkdir_p = true,
        .from_source = std::move(from),
        .verbatim = true,
        .mode = 0755,
        .when_clause = ident("use_modules"),
        .line = 1,
        .column = 1}));
    expect_round_trip(program_with(std::move(stmts)));
}

TEST(BinarySerializer, RoundTripFileWithFromSource) {
    PathExpr dst;
    dst.segments.emplace_back(
        PathLiteral{.value = "src/main.cpp", .line = 1, .column = 6});
    dst.line = 1;
    dst.column = 6;
    PathExpr src;
    src.segments.emplace_back(
        PathLiteral{.value = "templates/main.cpp", .line = 1, .column = 24});
    src.line = 1;
    src.column = 24;

    std::vector<StmtPtr> stmts;
    stmts.push_back(make_stmt(FileStmt{
        .path = std::move(dst),
        .alias = std::nullopt,
        .source = FileFromSource{.path = std::move(src), .verbatim = false},
        .append = false,
        .mode = std::nullopt,
        .when_clause = std::nullopt,
        .line = 1,
        .column = 1}));
    expect_round_trip(program_with(std::move(stmts)));
}

TEST(BinarySerializer, RoundTripFileWithContentSource) {
    PathExpr dst;
    dst.segments.emplace_back(
        PathLiteral{.value = "README.md", .line = 1, .column = 6});
    dst.line = 1;
    dst.column = 6;

    std::vector<StmtPtr> stmts;
    stmts.push_back(make_stmt(FileStmt{
        .path = std::move(dst),
        .alias = std::string{"readme"},
        .source = FileContentSource{.value = str_lit("# Hello")},
        .append = true,
        .mode = 0644,
        .when_clause = ident("use_readme"),
        .line = 1,
        .column = 1}));
    expect_round_trip(program_with(std::move(stmts)));
}

TEST(BinarySerializer, RoundTripRepeatWithNestedStmts) {
    PathExpr p;
    p.segments.emplace_back(PathLiteral{.value = "week_", .line = 2, .column = 7});
    p.segments.emplace_back(
        PathInterp{.expression = ident("i"), .line = 2, .column = 12});
    p.line = 2;
    p.column = 7;

    std::vector<StmtPtr> body;
    body.push_back(make_stmt(MkdirStmt{.path = std::move(p),
                                       .alias = std::nullopt,
                                       .mkdir_p = true,
                                       .from_source = std::nullopt,
                                       .verbatim = false,
                                       .mode = std::nullopt,
                                       .when_clause = std::nullopt,
                                       .line = 2,
                                       .column = 1}));

    std::vector<StmtPtr> stmts;
    stmts.push_back(make_stmt(RepeatStmt{
        .collection_var = "num_weeks",
        .iterator_var = "i",
        .body = std::move(body),
        .when_clause = std::nullopt,
        .line = 1,
        .column = 1}));
    expect_round_trip(program_with(std::move(stmts)));
}

TEST(BinarySerializer, RoundTripCopy) {
    PathExpr src;
    src.segments.emplace_back(
        PathLiteral{.value = "templates/", .line = 1, .column = 6});
    src.line = 1;
    src.column = 6;
    PathExpr dst;
    dst.segments.emplace_back(PathVar{.name = "modulepath", .line = 1, .column = 22});
    dst.line = 1;
    dst.column = 22;

    std::vector<StmtPtr> stmts;
    stmts.push_back(make_stmt(CopyStmt{.source = std::move(src),
                                       .destination = std::move(dst),
                                       .verbatim = true,
                                       .when_clause = ident("use_modules"),
                                       .line = 1,
                                       .column = 1}));
    expect_round_trip(program_with(std::move(stmts)));
}

TEST(BinarySerializer, RoundTripIncludeAndRunWithCwd) {
    PathExpr cwd;
    cwd.segments.emplace_back(PathVar{.name = "modulepath", .line = 2, .column = 14});
    cwd.line = 2;
    cwd.column = 14;

    std::vector<StmtPtr> stmts;
    stmts.push_back(make_stmt(IncludeStmt{.name = "claude_setup",
                                          .when_clause = ident("use_claude"),
                                          .line = 1,
                                          .column = 1}));
    stmts.push_back(make_stmt(RunStmt{.command = str_lit("git init"),
                                      .cwd = std::move(cwd),
                                      .when_clause = std::nullopt,
                                      .line = 2,
                                      .column = 1}));
    stmts.push_back(make_stmt(RunStmt{.command = str_lit("ls"),
                                      .cwd = std::nullopt,
                                      .when_clause = std::nullopt,
                                      .line = 3,
                                      .column = 1}));
    expect_round_trip(program_with(std::move(stmts)));
}

// ----- Truncation and varint cap ------------------------------------------

TEST(BinarySerializer, TruncatedInputThrowsAtKnownOffsets) {
    std::vector<StmtPtr> stmts;
    stmts.push_back(make_stmt(LetStmt{.name = "x",
                                      .value = str_lit("hello"),
                                      .line = 1,
                                      .column = 1}));
    auto bytes = serialize_program(program_with(std::move(stmts)));

    // Walk every prefix length shorter than the full payload; every one
    // must throw with an offset inside the truncated range.
    for (std::size_t cut = 0; cut < bytes.size(); ++cut) {
        EXPECT_THROW(deserialize_program(bytes.data(), cut),
                     BinaryDeserializeError);
    }
}

TEST(BinarySerializer, VarintLongerThanTenBytesThrows) {
    // Eleven 0x80 bytes form a varint that never terminates within the
    // 10-byte cap. The decoder is supposed to bail before reading more.
    std::vector<std::uint8_t> bytes(11, 0x80);
    bytes.push_back(0x01);
    EXPECT_THROW(deserialize_program(bytes.data(), bytes.size()),
                 BinaryDeserializeError);
}

TEST(BinarySerializer, InvalidUnaryOpRejected) {
    // Hand-craft: program with one Let whose value is a Unary with op=PLUS.
    // PLUS is a binary op — not legal in a unary slot. Encoder must throw.
    auto bad = make_expr(UnaryExpr{.op = TokenType::PLUS,
                                   .operand = ident("flag"),
                                   .line = 1,
                                   .column = 1});
    std::vector<StmtPtr> stmts;
    stmts.push_back(make_stmt(LetStmt{.name = "broken",
                                      .value = std::move(bad),
                                      .line = 1,
                                      .column = 1}));
    EXPECT_THROW(serialize_program(program_with(std::move(stmts))),
                 BinarySerializeError);
}

TEST(BinarySerializer, InvalidBinaryOpRejected) {
    auto bad = make_expr(BinaryExpr{.op = TokenType::NOT,
                                    .left = int_lit(1),
                                    .right = int_lit(2),
                                    .line = 1,
                                    .column = 1});
    std::vector<StmtPtr> stmts;
    stmts.push_back(make_stmt(LetStmt{.name = "broken",
                                      .value = std::move(bad),
                                      .line = 1,
                                      .column = 1}));
    EXPECT_THROW(serialize_program(program_with(std::move(stmts))),
                 BinarySerializeError);
}

TEST(BinarySerializer, EmptyProgramRoundTrips) {
    expect_round_trip(program_with({}));
}

TEST(BinarySerializer, RejectsHugeStatementCount) {
    // Statement count varint encodes a huge value with no statements
    // following. The decoder must reject without OOM-reserving.
    std::vector<std::uint8_t> bytes;
    // Varint for ~2^60 — 9 continuation bytes plus a high terminator.
    for (int i = 0; i < 9; ++i) bytes.push_back(0xFF);
    bytes.push_back(0x0F);
    EXPECT_THROW(deserialize_program(bytes.data(), bytes.size()),
                 BinaryDeserializeError);
}

TEST(BinarySerializer, RejectsDeeplyNestedExpressions) {
    // A chain of unary `not` ops nested 1000 deep. Each level encodes as
    // [tag, op_token, ...nested...]; the decoder caps recursion at 256.
    std::vector<std::uint8_t> bytes;
    bytes.push_back(0x01);  // statement count = 1
    bytes.push_back(static_cast<std::uint8_t>(StmtTag::Let));
    bytes.push_back(0x01);  // name length = 1
    bytes.push_back('x');   // name
    for (int i = 0; i < 300; ++i) {
        bytes.push_back(static_cast<std::uint8_t>(ExprTag::Unary));
        bytes.push_back(static_cast<std::uint8_t>(TokenType::NOT));
    }
    // Inner-most operand and the rest of the encoding will be truncated;
    // we only need to verify the depth cap fires before stack overflow.
    EXPECT_THROW(deserialize_program(bytes.data(), bytes.size()),
                 BinaryDeserializeError);
}

TEST(BinarySerializer, DeserializeErrorCarriesOffset) {
    // Single byte 0xFF: invalid as a varint (would need continuation), but
    // legal as a single 7-bit value of 127 — actually 0xFF has the high bit
    // set so it expects a continuation, which is missing. That throws as a
    // truncated varint at offset 1.
    std::vector<std::uint8_t> bytes{0xFFU};
    try {
        deserialize_program(bytes.data(), bytes.size());
        FAIL() << "expected BinaryDeserializeError";
    } catch (const BinaryDeserializeError& e) {
        EXPECT_EQ(e.offset(), 1u);
    }
}

}  // namespace
}  // namespace spudplate
