#include "spudplate/interpreter.h"

#include <gtest/gtest.h>

#include <cstdint>
#include <stdexcept>
#include <string>
#include <utility>

#include "spudplate/ast.h"
#include "spudplate/lexer.h"
#include "spudplate/parser.h"

using spudplate::Environment;
using spudplate::Lexer;
using spudplate::Parser;
using spudplate::Program;
using spudplate::Prompter;
using spudplate::run;
using spudplate::run_for_tests;
using spudplate::RuntimeError;
using spudplate::Value;
using spudplate::VarType;

namespace {

// Test stub — never asked, never answered. Used in Part 1 where every
// statement throws "not yet supported" before the prompter is reached.
class NullPrompter : public Prompter {
  public:
    std::string prompt(const std::string& /*message*/,
                       VarType /*type*/) override {
        throw std::logic_error("NullPrompter should not be called");
    }
};

Program parse(const std::string& source) {
    Lexer lexer(source);
    Parser parser(std::move(lexer));
    return parser.parse();
}

}  // namespace

// --- RuntimeError shape ---

TEST(RuntimeErrorTest, RoundTripsMessageAndPosition) {
    RuntimeError e("boom", 3, 7);
    EXPECT_STREQ(e.what(), "boom");
    EXPECT_EQ(e.line(), 3);
    EXPECT_EQ(e.column(), 7);
}

// --- Environment primitives ---

TEST(EnvironmentTest, DeclareAndLookupString) {
    Environment env;
    env.declare("name", Value{std::string{"hello"}});
    auto v = env.lookup("name");
    ASSERT_TRUE(v.has_value());
    EXPECT_EQ(std::get<std::string>(*v), "hello");
}

TEST(EnvironmentTest, DeclareAndLookupInt) {
    Environment env;
    env.declare("n", Value{std::int64_t{42}});
    auto v = env.lookup("n");
    ASSERT_TRUE(v.has_value());
    EXPECT_EQ(std::get<std::int64_t>(*v), 42);
}

TEST(EnvironmentTest, DeclareAndLookupBool) {
    Environment env;
    env.declare("flag", Value{true});
    auto v = env.lookup("flag");
    ASSERT_TRUE(v.has_value());
    EXPECT_EQ(std::get<bool>(*v), true);
}

TEST(EnvironmentTest, LookupReturnsNulloptForUnknown) {
    Environment env;
    EXPECT_FALSE(env.lookup("missing").has_value());
}

TEST(EnvironmentTest, DeclareRejectsShadow) {
    Environment env;
    env.declare("x", Value{std::int64_t{1}});
    EXPECT_THROW(env.declare("x", Value{std::int64_t{2}}), std::logic_error);
}

TEST(EnvironmentTest, NestedFrameSeesOuterBinding) {
    Environment env;
    env.declare("outer", Value{std::int64_t{1}});
    env.push();
    auto v = env.lookup("outer");
    ASSERT_TRUE(v.has_value());
    EXPECT_EQ(std::get<std::int64_t>(*v), 1);
}

TEST(EnvironmentTest, PoppedFrameLosesItsBindings) {
    Environment env;
    env.push();
    env.declare("inner", Value{std::int64_t{1}});
    env.pop();
    EXPECT_FALSE(env.lookup("inner").has_value());
}

// --- run() on empty program ---

TEST(InterpreterTest, EmptyProgramRunsCleanly) {
    Program empty;
    NullPrompter prompter;
    EXPECT_NO_THROW(run(empty, prompter));
}

// --- Every statement type throws "not yet supported" ---

TEST(InterpreterTest, AskThrowsNotYetSupported) {
    auto program = parse(R"(ask name "Project name?" string
)");
    NullPrompter prompter;
    try {
        run(program, prompter);
        FAIL() << "expected RuntimeError";
    } catch (const RuntimeError& e) {
        EXPECT_NE(std::string(e.what()).find("ask"), std::string::npos);
    }
}

TEST(InterpreterTest, LetThrowsNotYetSupported) {
    auto program = parse(R"(let x = 1
)");
    NullPrompter prompter;
    try {
        run(program, prompter);
        FAIL() << "expected RuntimeError";
    } catch (const RuntimeError& e) {
        EXPECT_NE(std::string(e.what()).find("let"), std::string::npos);
    }
}

TEST(InterpreterTest, MkdirThrowsNotYetSupported) {
    auto program = parse(R"(mkdir foo
)");
    NullPrompter prompter;
    try {
        run(program, prompter);
        FAIL() << "expected RuntimeError";
    } catch (const RuntimeError& e) {
        EXPECT_NE(std::string(e.what()).find("mkdir"), std::string::npos);
    }
}

TEST(InterpreterTest, FileThrowsNotYetSupported) {
    auto program = parse(R"(file foo.txt content "hi"
)");
    NullPrompter prompter;
    try {
        run(program, prompter);
        FAIL() << "expected RuntimeError";
    } catch (const RuntimeError& e) {
        EXPECT_NE(std::string(e.what()).find("file"), std::string::npos);
    }
}

TEST(InterpreterTest, RepeatThrowsNotYetSupported) {
    auto program = parse(R"(repeat n as i
end
)");
    NullPrompter prompter;
    try {
        run(program, prompter);
        FAIL() << "expected RuntimeError";
    } catch (const RuntimeError& e) {
        EXPECT_NE(std::string(e.what()).find("repeat"), std::string::npos);
    }
}

TEST(InterpreterTest, CopyThrowsNotYetSupported) {
    auto program = parse(R"(copy src into dst
)");
    NullPrompter prompter;
    try {
        run(program, prompter);
        FAIL() << "expected RuntimeError";
    } catch (const RuntimeError& e) {
        EXPECT_NE(std::string(e.what()).find("copy"), std::string::npos);
    }
}

TEST(InterpreterTest, IncludeThrowsNotYetSupported) {
    auto program = parse(R"(include claude_setup
)");
    NullPrompter prompter;
    try {
        run(program, prompter);
        FAIL() << "expected RuntimeError";
    } catch (const RuntimeError& e) {
        EXPECT_NE(std::string(e.what()).find("include"), std::string::npos);
    }
}

// --- run_for_tests returns the interpreter's environment ---

TEST(InterpreterTest, RunForTestsReturnsEnvironmentOnEmptyProgram) {
    Program empty;
    NullPrompter prompter;
    Environment env = run_for_tests(empty, prompter);
    EXPECT_FALSE(env.lookup("anything").has_value());
}
