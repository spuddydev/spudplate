#include "spudplate/cli.h"

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <random>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

#include "spudplate/interpreter.h"

using spudplate::cli_main;
using spudplate::Prompter;
using spudplate::ScriptedPrompter;
using spudplate::VarType;

namespace {

class TmpDir {
  public:
    TmpDir() {
        prev_ = std::filesystem::current_path();
        std::random_device rd;
        std::stringstream ss;
        ss << "spudplate-cli-" << std::hex << rd() << rd();
        path_ = std::filesystem::temp_directory_path() / ss.str();
        std::filesystem::create_directories(path_);
        std::filesystem::current_path(path_);
    }
    TmpDir(const TmpDir&) = delete;
    TmpDir& operator=(const TmpDir&) = delete;
    ~TmpDir() {
        std::error_code ec;
        std::filesystem::current_path(prev_, ec);
        std::filesystem::remove_all(path_, ec);
    }

    [[nodiscard]] const std::filesystem::path& path() const { return path_; }

  private:
    std::filesystem::path path_;
    std::filesystem::path prev_;
};

void write_file(const std::filesystem::path& p, const std::string& content) {
    std::ofstream out(p);
    out << content;
}

// Build an argv array suitable for cli_main from a vector of arguments.
class Argv {
  public:
    explicit Argv(std::vector<std::string> args) : args_(std::move(args)) {
        for (auto& s : args_) {
            ptrs_.push_back(s.data());
        }
    }
    [[nodiscard]] int argc() const { return static_cast<int>(ptrs_.size()); }
    [[nodiscard]] char** argv() { return ptrs_.data(); }

  private:
    std::vector<std::string> args_;
    std::vector<char*> ptrs_;
};

}  // namespace

TEST(CliTest, RunSuccessExitsZero) {
    TmpDir td;
    auto file = td.path() / "ok.spud";
    write_file(file, "ask name \"Project name?\" string\nmkdir {name}\n");
    Argv args({"spudplate", "run", file.string()});
    std::stringstream out;
    std::stringstream err;
    ScriptedPrompter prompter({"my_project"});
    int code = cli_main(args.argc(), args.argv(), out, err, prompter);
    EXPECT_EQ(code, 0);
    EXPECT_EQ(err.str(), "");
    EXPECT_TRUE(std::filesystem::is_directory(td.path() / "my_project"));
}

TEST(CliTest, ParseErrorExitsTwo) {
    TmpDir td;
    auto file = td.path() / "broken.spud";
    write_file(file, "ask\n");
    Argv args({"spudplate", "run", file.string()});
    std::stringstream out;
    std::stringstream err;
    ScriptedPrompter prompter({});
    int code = cli_main(args.argc(), args.argv(), out, err, prompter);
    EXPECT_EQ(code, 2);
    EXPECT_NE(err.str().find("parse error"), std::string::npos);
}

TEST(CliTest, SemanticErrorExitsThree) {
    TmpDir td;
    auto file = td.path() / "bad.spud";
    // Shadowing a name inside `repeat` is a semantic error, not a parse error.
    write_file(file, "let x = 1\nlet n = 1\nrepeat n as i\n  let x = 2\nend\n");
    Argv args({"spudplate", "run", file.string()});
    std::stringstream out;
    std::stringstream err;
    ScriptedPrompter prompter({});
    int code = cli_main(args.argc(), args.argv(), out, err, prompter);
    EXPECT_EQ(code, 3);
    EXPECT_NE(err.str().find("semantic error"), std::string::npos);
}

TEST(CliTest, RuntimeErrorExitsFour) {
    TmpDir td;
    auto file = td.path() / "runtime.spud";
    // `mkdir from` is rejected at runtime as not yet supported.
    write_file(file, "mkdir foo from base\n");
    Argv args({"spudplate", "run", file.string()});
    std::stringstream out;
    std::stringstream err;
    ScriptedPrompter prompter({});
    int code = cli_main(args.argc(), args.argv(), out, err, prompter);
    EXPECT_EQ(code, 4);
    EXPECT_NE(err.str().find("runtime error"), std::string::npos);
}

TEST(CliTest, UnknownSubcommandExitsOne) {
    TmpDir td;
    Argv args({"spudplate", "frobnicate"});
    std::stringstream out;
    std::stringstream err;
    ScriptedPrompter prompter({});
    int code = cli_main(args.argc(), args.argv(), out, err, prompter);
    EXPECT_EQ(code, 1);
    EXPECT_NE(err.str().find("usage"), std::string::npos);
}

TEST(CliTest, NoArgumentsExitsOne) {
    Argv args({"spudplate"});
    std::stringstream out;
    std::stringstream err;
    ScriptedPrompter prompter({});
    int code = cli_main(args.argc(), args.argv(), out, err, prompter);
    EXPECT_EQ(code, 1);
    EXPECT_NE(err.str().find("usage"), std::string::npos);
}

TEST(CliTest, MissingFileExitsFive) {
    TmpDir td;
    auto file = td.path() / "absent.spud";
    Argv args({"spudplate", "run", file.string()});
    std::stringstream out;
    std::stringstream err;
    ScriptedPrompter prompter({});
    int code = cli_main(args.argc(), args.argv(), out, err, prompter);
    EXPECT_EQ(code, 5);
    EXPECT_NE(err.str().find("cannot open"), std::string::npos);
}
