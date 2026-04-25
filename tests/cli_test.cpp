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

// Scoped override of `SPUDPLATE_HOME`. Tests use this to point the CLI at
// a temporary install root so they don't touch the user's real
// `~/.spudplate` directory. Restores the previous value on destruction.
class ScopedHome {
  public:
    explicit ScopedHome(const std::filesystem::path& dir) {
        if (const char* prev = std::getenv("SPUDPLATE_HOME"); prev != nullptr) {
            had_prev_ = true;
            prev_ = prev;
        }
        ::setenv("SPUDPLATE_HOME", dir.c_str(), /*overwrite=*/1);
    }
    ScopedHome(const ScopedHome&) = delete;
    ScopedHome& operator=(const ScopedHome&) = delete;
    ~ScopedHome() {
        if (had_prev_) {
            ::setenv("SPUDPLATE_HOME", prev_.c_str(), /*overwrite=*/1);
        } else {
            ::unsetenv("SPUDPLATE_HOME");
        }
    }

  private:
    bool had_prev_{false};
    std::string prev_;
};

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
    // `mkdir from` looks up `base` in the cwd; with no such directory the
    // walker raises a runtime error.
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

TEST(CliTest, DryRunPrintsTreeAndDoesNotWrite) {
    TmpDir td;
    auto file = td.path() / "ok.spud";
    write_file(file, "mkdir my_project\nfile my_project/README.md content \"hi\"\n");
    Argv args({"spudplate", "run", "--dry-run", file.string()});
    std::stringstream out;
    std::stringstream err;
    ScriptedPrompter prompter({});
    int code = cli_main(args.argc(), args.argv(), out, err, prompter);
    EXPECT_EQ(code, 0);
    EXPECT_EQ(err.str(), "");
    EXPECT_NE(out.str().find("Would create:"), std::string::npos);
    EXPECT_NE(out.str().find("my_project"), std::string::npos);
    EXPECT_NE(out.str().find("README.md"), std::string::npos);
    EXPECT_FALSE(std::filesystem::exists(td.path() / "my_project"));
}

TEST(CliTest, DryRunWithoutFileExitsOne) {
    Argv args({"spudplate", "run", "--dry-run"});
    std::stringstream out;
    std::stringstream err;
    ScriptedPrompter prompter({});
    int code = cli_main(args.argc(), args.argv(), out, err, prompter);
    EXPECT_EQ(code, 1);
    EXPECT_NE(err.str().find("usage"), std::string::npos);
}

TEST(CliTest, RunWithYesSkipsAuthorization) {
    TmpDir td;
    auto file = td.path() / "ok.spud";
    write_file(file, "run \"touch marker\"\n");
    Argv args({"spudplate", "run", "--yes", file.string()});
    std::stringstream out;
    std::stringstream err;
    ScriptedPrompter prompter({});
    prompter.set_authorize_response(false);  // would abort if --yes ignored
    int code = cli_main(args.argc(), args.argv(), out, err, prompter);
    EXPECT_EQ(code, 0);
    EXPECT_TRUE(std::filesystem::exists(td.path() / "marker"));
    EXPECT_FALSE(prompter.last_authorize_summary().has_value());
}

TEST(CliTest, RunWithoutYesAsksAuthorization) {
    TmpDir td;
    auto file = td.path() / "ok.spud";
    write_file(file, "run \"touch should_not_exist\"\n");
    Argv args({"spudplate", "run", file.string()});
    std::stringstream out;
    std::stringstream err;
    ScriptedPrompter prompter({});
    prompter.set_authorize_response(false);
    int code = cli_main(args.argc(), args.argv(), out, err, prompter);
    EXPECT_EQ(code, 0);
    EXPECT_FALSE(std::filesystem::exists(td.path() / "should_not_exist"));
    EXPECT_TRUE(prompter.last_authorize_summary().has_value());
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

// --- install ---

TEST(CliTest, InstallSuccessStoresTemplate) {
    TmpDir td;
    auto home = td.path() / "home";
    ScopedHome scoped(home);
    auto src = td.path() / "demo.spud";
    write_file(src, "ask name \"Project name?\" string\nmkdir {name}\n");
    Argv args({"spudplate", "install", src.string()});
    std::stringstream out;
    std::stringstream err;
    ScriptedPrompter prompter({});
    int code = cli_main(args.argc(), args.argv(), out, err, prompter);
    EXPECT_EQ(code, 0) << err.str();
    EXPECT_TRUE(std::filesystem::is_regular_file(home / "demo" / "template.spud"));
    EXPECT_NE(out.str().find("installed demo"), std::string::npos);
}

TEST(CliTest, InstallRejectsDuplicate) {
    TmpDir td;
    auto home = td.path() / "home";
    ScopedHome scoped(home);
    auto src = td.path() / "demo.spud";
    write_file(src, "mkdir foo\n");
    Argv args({"spudplate", "install", src.string()});
    std::stringstream out1;
    std::stringstream err1;
    ScriptedPrompter prompter({});
    ASSERT_EQ(cli_main(args.argc(), args.argv(), out1, err1, prompter), 0)
        << err1.str();
    std::stringstream out2;
    std::stringstream err2;
    int code = cli_main(args.argc(), args.argv(), out2, err2, prompter);
    EXPECT_EQ(code, 1);
    EXPECT_NE(err2.str().find("already installed"), std::string::npos);
}

TEST(CliTest, InstallRejectsBrokenTemplateAndLeavesNoTrace) {
    TmpDir td;
    auto home = td.path() / "home";
    ScopedHome scoped(home);
    auto src = td.path() / "broken.spud";
    write_file(src, "ask\n");  // parse error: missing args
    Argv args({"spudplate", "install", src.string()});
    std::stringstream out;
    std::stringstream err;
    ScriptedPrompter prompter({});
    int code = cli_main(args.argc(), args.argv(), out, err, prompter);
    EXPECT_EQ(code, 2);
    EXPECT_FALSE(std::filesystem::exists(home / "broken"));
}

TEST(CliTest, InstallMissingFileExitsFive) {
    TmpDir td;
    auto home = td.path() / "home";
    ScopedHome scoped(home);
    auto src = td.path() / "absent.spud";
    Argv args({"spudplate", "install", src.string()});
    std::stringstream out;
    std::stringstream err;
    ScriptedPrompter prompter({});
    int code = cli_main(args.argc(), args.argv(), out, err, prompter);
    EXPECT_EQ(code, 5);
}
