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

TEST(Introspect, HumanOutputListsEachQuestion) {
    Program prog = parse_source(
        "ask name \"Project name?\" string\n"
        "ask use_git \"Use git?\" bool default true\n");
    auto asks = collect_top_level_asks(prog);
    std::ostringstream out;
    emit_questions_human(out, asks);
    std::string s = out.str();
    EXPECT_NE(s.find("Questions:"), std::string::npos);
    EXPECT_NE(s.find("name (string): Project name?"), std::string::npos);
    EXPECT_NE(s.find("use_git (bool): Use git?"), std::string::npos);
    EXPECT_NE(s.find("[default: true]"), std::string::npos);
}

TEST(Introspect, HumanOutputAnnotatesOptionsAndWhen) {
    Program prog = parse_source(
        "ask use_docs \"docs?\" bool default true\n"
        "ask format \"format?\" string options \"pdf\" \"html\" "
        "default \"pdf\" when use_docs\n");
    auto asks = collect_top_level_asks(prog);
    std::ostringstream out;
    emit_questions_human(out, asks);
    std::string s = out.str();
    EXPECT_NE(s.find("[options: \"pdf\", \"html\"]"), std::string::npos);
    EXPECT_NE(s.find("[when: gated]"), std::string::npos);
}

TEST(Introspect, HumanOutputHandlesEmpty) {
    Program prog = parse_source("");
    auto asks = collect_top_level_asks(prog);
    std::ostringstream out;
    emit_questions_human(out, asks);
    EXPECT_NE(out.str().find("(no top-level questions)"), std::string::npos);
}

TEST(Introspect, YamlOutputPrefillsLiteralDefaults) {
    Program prog = parse_source(
        "ask name \"Project name?\" string\n"
        "ask use_git \"Use git?\" bool default true\n"
        "ask weeks \"Weeks?\" int default 4\n"
        "ask format \"Format?\" string default \"pdf\"\n");
    auto asks = collect_top_level_asks(prog);
    std::ostringstream out;
    emit_questions_yaml(out, asks);
    std::string s = out.str();
    EXPECT_NE(s.find("name: \"\""), std::string::npos);
    EXPECT_NE(s.find("use_git: true"), std::string::npos);
    EXPECT_NE(s.find("weeks: 4"), std::string::npos);
    EXPECT_NE(s.find("format: \"pdf\""), std::string::npos);
}

TEST(Introspect, YamlOutputAnnotatesOptionsAndWhen) {
    Program prog = parse_source(
        "ask use_docs \"docs?\" bool default true\n"
        "ask format \"format\" string options \"pdf\" \"html\" "
        "default \"pdf\" when use_docs\n");
    auto asks = collect_top_level_asks(prog);
    std::ostringstream out;
    emit_questions_yaml(out, asks);
    std::string s = out.str();
    EXPECT_NE(s.find("# options: \"pdf\", \"html\""), std::string::npos);
    EXPECT_NE(s.find("# when: gated"), std::string::npos);
}

TEST(Introspect, ParseAnswersHappyPath) {
    Program prog = parse_source(
        "ask name \"n\" string\n"
        "ask weeks \"w\" int\n"
        "ask use_git \"g\" bool\n");
    auto asks = collect_top_level_asks(prog);
    auto answers = parse_answers_yaml(
        "name: \"my project\"\n"
        "weeks: 12\n"
        "use_git: false\n",
        asks);
    EXPECT_EQ(answers.size(), 3u);
    EXPECT_EQ(std::get<std::string>(answers["name"]), "my project");
    EXPECT_EQ(std::get<std::int64_t>(answers["weeks"]), 12);
    EXPECT_EQ(std::get<bool>(answers["use_git"]), false);
}

TEST(Introspect, ParseAnswersAcceptsBareString) {
    Program prog = parse_source("ask name \"n\" string\n");
    auto asks = collect_top_level_asks(prog);
    auto answers = parse_answers_yaml("name: hello world\n", asks);
    EXPECT_EQ(std::get<std::string>(answers["name"]), "hello world");
}

TEST(Introspect, ParseAnswersIgnoresCommentsAndBlankLines) {
    Program prog = parse_source("ask name \"n\" string\n");
    auto asks = collect_top_level_asks(prog);
    auto answers = parse_answers_yaml(
        "# leading comment\n"
        "\n"
        "name: foo\n"
        "\n"
        "# trailing\n",
        asks);
    EXPECT_EQ(std::get<std::string>(answers["name"]), "foo");
}

TEST(Introspect, ParseAnswersRejectsUnknownKey) {
    Program prog = parse_source("ask name \"n\" string\n");
    auto asks = collect_top_level_asks(prog);
    EXPECT_THROW(parse_answers_yaml("ghost: 1\n", asks),
                 AnswersParseError);
}

TEST(Introspect, ParseAnswersRejectsTypeMismatch) {
    Program prog = parse_source("ask weeks \"w\" int\n");
    auto asks = collect_top_level_asks(prog);
    EXPECT_THROW(parse_answers_yaml("weeks: notanint\n", asks),
                 AnswersParseError);
}

TEST(Introspect, ParseAnswersRejectsBoolFromOtherStrings) {
    Program prog = parse_source("ask flag \"f\" bool\n");
    auto asks = collect_top_level_asks(prog);
    EXPECT_THROW(parse_answers_yaml("flag: yes\n", asks),
                 AnswersParseError);
}

TEST(Introspect, ParseAnswersRejectsDuplicateKey) {
    Program prog = parse_source("ask name \"n\" string\n");
    auto asks = collect_top_level_asks(prog);
    EXPECT_THROW(parse_answers_yaml("name: a\nname: b\n", asks),
                 AnswersParseError);
}

TEST(Introspect, ParseAnswersAcceptsCRLF) {
    Program prog = parse_source("ask name \"n\" string\n");
    auto asks = collect_top_level_asks(prog);
    auto answers = parse_answers_yaml("name: \"foo\"\r\n", asks);
    EXPECT_EQ(std::get<std::string>(answers["name"]), "foo");
}

TEST(Introspect, ParseAnswersHandlesQuotedEscapes) {
    Program prog = parse_source("ask greeting \"g\" string\n");
    auto asks = collect_top_level_asks(prog);
    auto answers = parse_answers_yaml(
        "greeting: \"hi \\\"there\\\"\"\n", asks);
    EXPECT_EQ(std::get<std::string>(answers["greeting"]),
              "hi \"there\"");
}

}  // namespace
}  // namespace spudplate
