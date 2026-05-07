#include "spudplate/introspect.h"

#include <gtest/gtest.h>

#include <sstream>
#include <string>

#include "spudplate/lexer.h"
#include "spudplate/parser.h"

namespace spudplate {
namespace {

Program parse_source(const std::string& src) {
    Lexer lex(src);
    Parser p(std::move(lex));
    return p.parse();
}

TEST(Introspect, CollectsTopLevelAsksInOrder) {
    Program prog = parse_source(
        "ask a \"first\" string\n"
        "ask b \"second\" int default 7\n"
        "ask c \"third\" bool default true\n");
    auto asks = collect_top_level_asks(prog);
    ASSERT_EQ(asks.size(), 3u);
    EXPECT_EQ(asks[0].name, "a");
    EXPECT_EQ(asks[0].var_type, VarType::String);
    EXPECT_EQ(asks[0].prompt, "first");
    EXPECT_EQ(asks[0].default_value, nullptr);
    EXPECT_EQ(asks[0].when_clause, nullptr);
    EXPECT_EQ(asks[1].name, "b");
    EXPECT_EQ(asks[1].var_type, VarType::Int);
    EXPECT_NE(asks[1].default_value, nullptr);
    EXPECT_EQ(asks[2].name, "c");
    EXPECT_EQ(asks[2].var_type, VarType::Bool);
}

TEST(Introspect, SkipsAsksInsideRepeat) {
    Program prog = parse_source(
        "ask n \"how many\" int\n"
        "repeat n as i\n"
        "  ask name \"name\" string\n"
        "end\n");
    auto asks = collect_top_level_asks(prog);
    ASSERT_EQ(asks.size(), 1u);
    EXPECT_EQ(asks[0].name, "n");
}

TEST(Introspect, SkipsAsksInsideIf) {
    Program prog = parse_source(
        "ask use_extras \"extras?\" bool default false\n"
        "if use_extras\n"
        "  ask flavour \"which?\" string\n"
        "end\n");
    auto asks = collect_top_level_asks(prog);
    ASSERT_EQ(asks.size(), 1u);
    EXPECT_EQ(asks[0].name, "use_extras");
}

TEST(Introspect, RecordsOptionsAndWhenClause) {
    Program prog = parse_source(
        "ask use_docs \"docs?\" bool default true\n"
        "ask format \"output format\" string options \"pdf\" \"html\" "
        "default \"pdf\" when use_docs\n");
    auto asks = collect_top_level_asks(prog);
    ASSERT_EQ(asks.size(), 2u);
    EXPECT_NE(asks[1].when_clause, nullptr);
    ASSERT_NE(asks[1].options, nullptr);
    EXPECT_EQ(asks[1].options->size(), 2u);
}

TEST(Introspect, EmptyProgramYieldsEmptyVector) {
    Program prog = parse_source("");
    auto asks = collect_top_level_asks(prog);
    EXPECT_TRUE(asks.empty());
}

}  // namespace
}  // namespace spudplate
