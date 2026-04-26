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

// Save and restore an environment variable around a test. Set the value
// with `set()` or remove it with `unset()`; the original is reinstated on
// destruction so other tests in the suite are unaffected.
class ScopedEnv {
  public:
    explicit ScopedEnv(std::string name) : name_(std::move(name)) {
        if (const char* prev = std::getenv(name_.c_str()); prev != nullptr) {
            had_prev_ = true;
            prev_ = prev;
        }
    }
    ScopedEnv(const ScopedEnv&) = delete;
    ScopedEnv& operator=(const ScopedEnv&) = delete;
    ~ScopedEnv() {
        if (had_prev_) {
            ::setenv(name_.c_str(), prev_.c_str(), /*overwrite=*/1);
        } else {
            ::unsetenv(name_.c_str());
        }
    }

    void set(const std::string& value) {
        ::setenv(name_.c_str(), value.c_str(), /*overwrite=*/1);
    }
    void unset() {
        ::unsetenv(name_.c_str());
    }

  private:
    std::string name_;
    bool had_prev_{false};
    std::string prev_;
};

// Convenience: scope `SPUDPLATE_HOME` to a temp install root.
class ScopedHome {
  public:
    explicit ScopedHome(const std::filesystem::path& dir) : env_("SPUDPLATE_HOME") {
        env_.set(dir.string());
    }

  private:
    ScopedEnv env_;
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

TEST(CliTest, InstallWithYesOverwritesExisting) {
    TmpDir td;
    auto home = td.path() / "home";
    ScopedHome scoped(home);
    auto src = td.path() / "demo.spud";
    write_file(src, "mkdir foo\n");
    {
        Argv args({"spudplate", "install", src.string()});
        std::stringstream o;
        std::stringstream e;
        ScriptedPrompter p({});
        ASSERT_EQ(cli_main(args.argc(), args.argv(), o, e, p), 0) << e.str();
    }
    // Rewrite the source so we can detect that the new content landed.
    write_file(src, "mkdir bar\n");
    Argv args({"spudplate", "install", "--yes", src.string()});
    std::stringstream out;
    std::stringstream err;
    ScriptedPrompter prompter({});
    int code = cli_main(args.argc(), args.argv(), out, err, prompter);
    EXPECT_EQ(code, 0) << err.str();
    EXPECT_NE(out.str().find("reinstalled demo"), std::string::npos);
    std::ifstream installed(home / "demo" / "template.spud");
    std::stringstream contents;
    contents << installed.rdbuf();
    EXPECT_EQ(contents.str(), "mkdir bar\n");
}

TEST(CliTest, InstallDeclinedPromptLeavesExistingUnchanged) {
    TmpDir td;
    auto home = td.path() / "home";
    ScopedHome scoped(home);
    auto src = td.path() / "demo.spud";
    write_file(src, "mkdir foo\n");
    {
        Argv args({"spudplate", "install", src.string()});
        std::stringstream o;
        std::stringstream e;
        ScriptedPrompter p({});
        ASSERT_EQ(cli_main(args.argc(), args.argv(), o, e, p), 0) << e.str();
    }
    // No `--yes` and no stdin available — confirm() returns false and the
    // command aborts cleanly with exit 0.
    write_file(src, "mkdir bar\n");
    Argv args({"spudplate", "install", src.string()});
    std::stringstream out;
    std::stringstream err;
    ScriptedPrompter prompter({});
    int code = cli_main(args.argc(), args.argv(), out, err, prompter);
    EXPECT_EQ(code, 0);
    EXPECT_NE(out.str().find("aborted"), std::string::npos);
    std::ifstream installed(home / "demo" / "template.spud");
    std::stringstream contents;
    contents << installed.rdbuf();
    EXPECT_EQ(contents.str(), "mkdir foo\n");
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

TEST(CliTest, InstallUsesXdgDataHomeWhenNoSpudplateHome) {
    TmpDir td;
    ScopedEnv spud_home("SPUDPLATE_HOME");
    ScopedEnv xdg("XDG_DATA_HOME");
    spud_home.unset();
    auto xdg_root = td.path() / "xdg";
    xdg.set(xdg_root.string());
    auto src = td.path() / "demo.spud";
    write_file(src, "mkdir foo\n");
    Argv args({"spudplate", "install", src.string()});
    std::stringstream out;
    std::stringstream err;
    ScriptedPrompter prompter({});
    int code = cli_main(args.argc(), args.argv(), out, err, prompter);
    EXPECT_EQ(code, 0) << err.str();
    EXPECT_TRUE(std::filesystem::is_regular_file(
        xdg_root / "spudplate" / "demo" / "template.spud"));
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

// --- run by installed name ---

TEST(CliTest, RunByNameLooksUpInstalledTemplate) {
    TmpDir td;
    auto home = td.path() / "home";
    ScopedHome scoped(home);
    auto src = td.path() / "demo.spud";
    write_file(src, "ask name \"Project name?\" string\nmkdir {name}\n");
    {
        Argv install_args({"spudplate", "install", src.string()});
        std::stringstream o;
        std::stringstream e;
        ScriptedPrompter p({});
        ASSERT_EQ(cli_main(install_args.argc(), install_args.argv(), o, e, p), 0)
            << e.str();
    }
    Argv run_args({"spudplate", "run", "demo"});
    std::stringstream out;
    std::stringstream err;
    ScriptedPrompter prompter({"my_project"});
    int code = cli_main(run_args.argc(), run_args.argv(), out, err, prompter);
    EXPECT_EQ(code, 0) << err.str();
    EXPECT_TRUE(std::filesystem::is_directory(td.path() / "my_project"));
}

TEST(CliTest, RunByUnknownNameExitsFive) {
    TmpDir td;
    auto home = td.path() / "home";
    ScopedHome scoped(home);
    Argv args({"spudplate", "run", "ghost"});
    std::stringstream out;
    std::stringstream err;
    ScriptedPrompter prompter({});
    int code = cli_main(args.argc(), args.argv(), out, err, prompter);
    EXPECT_EQ(code, 5);
}

// --- list / inspect / uninstall ---

namespace {
void install_template(const std::filesystem::path& src, const std::string& body) {
    write_file(src, body);
    Argv args({"spudplate", "install", src.string()});
    std::stringstream o;
    std::stringstream e;
    ScriptedPrompter p({});
    ASSERT_EQ(cli_main(args.argc(), args.argv(), o, e, p), 0) << e.str();
}
}  // namespace

TEST(CliTest, ListEmptyProducesNoOutput) {
    TmpDir td;
    auto home = td.path() / "home";
    ScopedHome scoped(home);
    Argv args({"spudplate", "list"});
    std::stringstream out;
    std::stringstream err;
    ScriptedPrompter prompter({});
    int code = cli_main(args.argc(), args.argv(), out, err, prompter);
    EXPECT_EQ(code, 0);
    EXPECT_EQ(out.str(), "");
}

TEST(CliTest, ListShowsInstalledNamesSorted) {
    TmpDir td;
    auto home = td.path() / "home";
    ScopedHome scoped(home);
    install_template(td.path() / "zebra.spud", "mkdir z\n");
    install_template(td.path() / "alpha.spud", "mkdir a\n");
    Argv args({"spudplate", "list"});
    std::stringstream out;
    std::stringstream err;
    ScriptedPrompter prompter({});
    int code = cli_main(args.argc(), args.argv(), out, err, prompter);
    EXPECT_EQ(code, 0);
    EXPECT_EQ(out.str(), "alpha\nzebra\n");
}

TEST(CliTest, InspectPrintsSource) {
    TmpDir td;
    auto home = td.path() / "home";
    ScopedHome scoped(home);
    const std::string body = "mkdir hello\n";
    install_template(td.path() / "demo.spud", body);
    Argv args({"spudplate", "inspect", "demo"});
    std::stringstream out;
    std::stringstream err;
    ScriptedPrompter prompter({});
    int code = cli_main(args.argc(), args.argv(), out, err, prompter);
    EXPECT_EQ(code, 0);
    EXPECT_EQ(out.str(), body);
}

TEST(CliTest, InspectUnknownExitsFive) {
    TmpDir td;
    auto home = td.path() / "home";
    ScopedHome scoped(home);
    Argv args({"spudplate", "inspect", "ghost"});
    std::stringstream out;
    std::stringstream err;
    ScriptedPrompter prompter({});
    int code = cli_main(args.argc(), args.argv(), out, err, prompter);
    EXPECT_EQ(code, 5);
}

TEST(CliTest, UninstallRemovesTemplate) {
    TmpDir td;
    auto home = td.path() / "home";
    ScopedHome scoped(home);
    install_template(td.path() / "demo.spud", "mkdir x\n");
    ASSERT_TRUE(std::filesystem::is_directory(home / "demo"));
    Argv args({"spudplate", "uninstall", "demo"});
    std::stringstream out;
    std::stringstream err;
    ScriptedPrompter prompter({});
    int code = cli_main(args.argc(), args.argv(), out, err, prompter);
    EXPECT_EQ(code, 0);
    EXPECT_FALSE(std::filesystem::exists(home / "demo"));
}

// --- validate ---

TEST(CliTest, ValidateOkExitsZero) {
    TmpDir td;
    auto src = td.path() / "ok.spud";
    write_file(src, "ask name \"Project name?\" string\nmkdir {name}\n");
    Argv args({"spudplate", "validate", src.string()});
    std::stringstream out;
    std::stringstream err;
    ScriptedPrompter prompter({});
    int code = cli_main(args.argc(), args.argv(), out, err, prompter);
    EXPECT_EQ(code, 0);
    EXPECT_NE(out.str().find("ok"), std::string::npos);
}

TEST(CliTest, CheckIsAliasForValidate) {
    TmpDir td;
    auto src = td.path() / "ok.spud";
    write_file(src, "mkdir foo\n");
    Argv args({"spudplate", "check", src.string()});
    std::stringstream out;
    std::stringstream err;
    ScriptedPrompter prompter({});
    int code = cli_main(args.argc(), args.argv(), out, err, prompter);
    EXPECT_EQ(code, 0);
}

TEST(CliTest, ValidateParseErrorExitsTwo) {
    TmpDir td;
    auto src = td.path() / "broken.spud";
    write_file(src, "ask\n");
    Argv args({"spudplate", "validate", src.string()});
    std::stringstream out;
    std::stringstream err;
    ScriptedPrompter prompter({});
    int code = cli_main(args.argc(), args.argv(), out, err, prompter);
    EXPECT_EQ(code, 2);
}

TEST(CliTest, ValidateSemanticErrorExitsThree) {
    TmpDir td;
    auto src = td.path() / "bad.spud";
    // Reference an undeclared name from outside a popped repeat scope.
    write_file(src,
               "let n = 1\n"
               "repeat n as i\n"
               "  let local = 0\n"
               "end\n"
               "mkdir {local}\n");
    Argv args({"spudplate", "validate", src.string()});
    std::stringstream out;
    std::stringstream err;
    ScriptedPrompter prompter({});
    int code = cli_main(args.argc(), args.argv(), out, err, prompter);
    EXPECT_EQ(code, 3);
}

TEST(CliTest, ValidateMissingFileExitsFive) {
    TmpDir td;
    Argv args({"spudplate", "validate", (td.path() / "absent.spud").string()});
    std::stringstream out;
    std::stringstream err;
    ScriptedPrompter prompter({});
    int code = cli_main(args.argc(), args.argv(), out, err, prompter);
    EXPECT_EQ(code, 5);
}

TEST(CliTest, UninstallUnknownExitsFive) {
    TmpDir td;
    auto home = td.path() / "home";
    ScopedHome scoped(home);
    Argv args({"spudplate", "uninstall", "ghost"});
    std::stringstream out;
    std::stringstream err;
    ScriptedPrompter prompter({});
    int code = cli_main(args.argc(), args.argv(), out, err, prompter);
    EXPECT_EQ(code, 5);
}
