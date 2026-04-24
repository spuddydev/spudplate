#include "spudplate/validator.h"

#include <gtest/gtest.h>

#include "spudplate/lexer.h"
#include "spudplate/parser.h"

using spudplate::Lexer;
using spudplate::Parser;
using spudplate::Program;
using spudplate::SemanticError;
using spudplate::validate;

static Program parse(const std::string& input) {
    Lexer lexer(input);
    Parser parser(std::move(lexer));
    return parser.parse();
}

// --- Validator skeleton tests ---

TEST(ValidatorTest, EmptyProgramValidatesCleanly) {
    Program empty;
    EXPECT_NO_THROW(validate(empty));
}

TEST(ValidatorTest, AskAtTopLevelThenRepeatValidates) {
    auto program = parse(
        "ask name \"Name?\" string\n"
        "repeat n as i\n"
        "  mkdir \"m_{i}\"\n"
        "end\n");
    EXPECT_NO_THROW(validate(program));
}

TEST(ValidatorTest, EmptyRepeatBodyValidates) {
    auto program = parse(
        "ask n \"Count?\" int\n"
        "repeat n as i\n"
        "end\n");
    EXPECT_NO_THROW(validate(program));
}

TEST(ValidatorTest, AskInsideSingleRepeatIsError) {
    auto program = parse(
        "ask n \"Count?\" int\n"
        "repeat n as i\n"
        "  ask use_extra \"Extra?\" bool\n"
        "end\n");
    try {
        validate(program);
        FAIL() << "expected SemanticError";
    } catch (const SemanticError& e) {
        // The offending ask is on line 3 (ask n ... is line 1, repeat is line 2).
        EXPECT_EQ(e.line(), 3);
    }
}

TEST(ValidatorTest, AskInsideNestedRepeatPointsAtAskLine) {
    auto program = parse(
        "ask n \"Count?\" int\n"       // line 1
        "ask m \"Count?\" int\n"       // line 2
        "repeat n as i\n"              // line 3 (outer)
        "  repeat m as j\n"            // line 4 (inner)
        "    ask bad \"bad?\" bool\n"  // line 5 — the offender
        "  end\n"
        "end\n");
    try {
        validate(program);
        FAIL() << "expected SemanticError";
    } catch (const SemanticError& e) {
        EXPECT_EQ(e.line(), 5);
    }
}

TEST(ValidatorTest, RepeatBodyWithNonAskStatementsValidates) {
    auto program = parse(
        "ask n \"Count?\" int\n"
        "mkdir base\n"
        "repeat n as i\n"
        "  mkdir \"m_{i}\"\n"
        "  file \"m_{i}/README.md\" content \"hi\"\n"
        "  let x = i + 1\n"
        "  repeat n as j\n"
        "    mkdir \"inner_{j}\"\n"
        "  end\n"
        "end\n");
    EXPECT_NO_THROW(validate(program));
}

// --- Scope stack tests ---

TEST(ValidatorTest, LetInsideRepeatReferencedOutsideIsError) {
    auto program = parse(
        "ask n \"Count?\" int\n"
        "repeat n as i\n"
        "  let x = i\n"
        "end\n"
        "let y = x\n");
    EXPECT_THROW(validate(program), SemanticError);
}

TEST(ValidatorTest, IteratorVarReferencedOutsideIsError) {
    auto program = parse(
        "ask n \"Count?\" int\n"
        "repeat n as i\n"
        "end\n"
        "let y = i\n");
    EXPECT_THROW(validate(program), SemanticError);
}

TEST(ValidatorTest, AliasBoundInsideRepeatReferencedOutsideIsError) {
    auto program = parse(
        "ask n \"Count?\" int\n"
        "repeat n as i\n"
        "  mkdir \"foo\" as bar\n"
        "end\n"
        "mkdir bar/sub\n");
    EXPECT_THROW(validate(program), SemanticError);
}

TEST(ValidatorTest, NestedRepeatsIsolateInnerScope) {
    // Inner-declared `x` must not be visible to the outer body after inner pops.
    auto program = parse(
        "ask n \"Count?\" int\n"
        "repeat n as i\n"
        "  repeat n as j\n"
        "    let x = 1\n"
        "  end\n"
        "  let y = x\n"
        "end\n");
    EXPECT_THROW(validate(program), SemanticError);
}

TEST(ValidatorTest, OuterNamesVisibleFromInsideNestedRepeat) {
    auto program = parse(
        "ask n \"Count?\" int\n"
        "let base = 5\n"
        "repeat n as i\n"
        "  repeat n as j\n"
        "    let x = base + i + j\n"
        "  end\n"
        "end\n");
    EXPECT_NO_THROW(validate(program));
}

TEST(ValidatorTest, SameScopeReferencesInsideRepeatAreValid) {
    auto program = parse(
        "ask n \"Count?\" int\n"
        "repeat n as i\n"
        "  let x = i\n"
        "  let y = x\n"
        "end\n");
    EXPECT_NO_THROW(validate(program));
}

TEST(ValidatorTest, ShadowingTopLevelLetFromInsideRepeatIsError) {
    auto program = parse(
        "ask n \"Count?\" int\n"
        "let x = 1\n"
        "repeat n as i\n"
        "  let x = i\n"
        "end\n");
    EXPECT_THROW(validate(program), SemanticError);
}

TEST(ValidatorTest, WhenClauseReferencingPoppedBindingIsError) {
    auto program = parse(
        "ask n \"Count?\" int\n"
        "repeat n as i\n"
        "  let x = 1\n"
        "end\n"
        "mkdir stuff when x\n");
    EXPECT_THROW(validate(program), SemanticError);
}

TEST(ValidatorTest, PathInterpReferencingPoppedIteratorIsError) {
    // Unquoted path so `{i}` is parsed as a PathInterp segment.
    auto program = parse(
        "ask n \"Count?\" int\n"
        "repeat n as i\n"
        "end\n"
        "mkdir week_{i}\n");
    EXPECT_THROW(validate(program), SemanticError);
}

TEST(ValidatorTest, CollectionVarReferencingPoppedNameIsError) {
    auto program = parse(
        "ask n \"Count?\" int\n"
        "repeat n as k\n"
        "  let x = 1\n"
        "end\n"
        "repeat x as j\n"
        "end\n");
    EXPECT_THROW(validate(program), SemanticError);
}
