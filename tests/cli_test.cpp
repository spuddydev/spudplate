#include "spudplate/cli.h"

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <random>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

#include "spudplate/cli_internal.h"
#include "spudplate/interpreter.h"
#include "spudplate/spudpack.h"

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
    void unset() { ::unsetenv(name_.c_str()); }

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
    write_file(file, "ask name \"Project name?\" string\nmkdir \"{name}\"\n");
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
    write_file(file, "mkdir \"foo\" from \"base\"\n");
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
    write_file(file,
               "mkdir \"my_project\"\nfile \"my_project/README.md\" content \"hi\"\n");
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

TEST(CliTest, RunWithNoTimeoutDisablesTimeouts) {
    // Without the flag, this would parse and execute identically. The
    // distinguishing behaviour - that the per-statement timeout is
    // ignored - is exercised by RunTest.NoTimeoutFlagOverridesPerStatementTimeout
    // at the interpreter layer; here we only assert that the CLI accepts
    // the flag without an error and exits zero.
    TmpDir td;
    auto file = td.path() / "ok.spud";
    write_file(file, "run \"sleep 0\" timeout 1\n");
    Argv args({"spudplate", "run", "--yes", "--no-timeout", file.string()});
    std::stringstream out;
    std::stringstream err;
    ScriptedPrompter prompter({});
    int code = cli_main(args.argc(), args.argv(), out, err, prompter);
    EXPECT_EQ(code, 0);
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
    write_file(src, "ask name \"Project name?\" string\nmkdir \"{name}\"\n");
    Argv args({"spudplate", "install", src.string()});
    std::stringstream out;
    std::stringstream err;
    ScriptedPrompter prompter({});
    int code = cli_main(args.argc(), args.argv(), out, err, prompter);
    EXPECT_EQ(code, 0) << err.str();
    EXPECT_TRUE(std::filesystem::is_regular_file(home / "demo.spp"));
    EXPECT_NE(out.str().find("installed demo"), std::string::npos);
}

TEST(CliTest, InstallWithYesOverwritesExisting) {
    TmpDir td;
    auto home = td.path() / "home";
    ScopedHome scoped(home);
    auto src = td.path() / "demo.spud";
    write_file(src, "mkdir \"foo\"\n");
    {
        Argv args({"spudplate", "install", src.string()});
        std::stringstream o;
        std::stringstream e;
        ScriptedPrompter p({});
        ASSERT_EQ(cli_main(args.argc(), args.argv(), o, e, p), 0) << e.str();
    }
    // Rewrite the source so we can detect that the new content landed.
    write_file(src, "mkdir \"bar\"\n");
    Argv args({"spudplate", "install", "--yes", src.string()});
    std::stringstream out;
    std::stringstream err;
    ScriptedPrompter prompter({});
    int code = cli_main(args.argc(), args.argv(), out, err, prompter);
    EXPECT_EQ(code, 0) << err.str();
    EXPECT_NE(out.str().find("reinstalled demo"), std::string::npos);
    spudplate::Spudpack pack = spudplate::spudpack_read_file(home / "demo.spp");
    EXPECT_EQ(pack.source, "mkdir \"bar\"\n");
}

TEST(CliTest, InstallDeclinedPromptLeavesExistingUnchanged) {
    TmpDir td;
    auto home = td.path() / "home";
    ScopedHome scoped(home);
    auto src = td.path() / "demo.spud";
    write_file(src, "mkdir \"foo\"\n");
    {
        Argv args({"spudplate", "install", src.string()});
        std::stringstream o;
        std::stringstream e;
        ScriptedPrompter p({});
        ASSERT_EQ(cli_main(args.argc(), args.argv(), o, e, p), 0) << e.str();
    }
    // No `--yes` and no stdin available - confirm() returns false and the
    // command aborts cleanly with exit 0.
    write_file(src, "mkdir \"bar\"\n");
    Argv args({"spudplate", "install", src.string()});
    std::stringstream out;
    std::stringstream err;
    ScriptedPrompter prompter({});
    int code = cli_main(args.argc(), args.argv(), out, err, prompter);
    EXPECT_EQ(code, 0);
    EXPECT_NE(out.str().find("aborted"), std::string::npos);
    spudplate::Spudpack pack = spudplate::spudpack_read_file(home / "demo.spp");
    EXPECT_EQ(pack.source, "mkdir \"foo\"\n");
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
    write_file(src, "mkdir \"foo\"\n");
    Argv args({"spudplate", "install", src.string()});
    std::stringstream out;
    std::stringstream err;
    ScriptedPrompter prompter({});
    int code = cli_main(args.argc(), args.argv(), out, err, prompter);
    EXPECT_EQ(code, 0) << err.str();
    EXPECT_TRUE(std::filesystem::is_regular_file(xdg_root / "spudplate" / "demo.spp"));
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

// --- run by installed name ---

TEST(CliTest, RunByNameLooksUpInstalledTemplate) {
    TmpDir td;
    auto home = td.path() / "home";
    ScopedHome scoped(home);
    auto src = td.path() / "demo.spud";
    write_file(src, "ask name \"Project name?\" string\nmkdir \"{name}\"\n");
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

TEST(CliTest, RunByUnknownNameMessageNamesArgAndPointsAtList) {
    TmpDir td;
    auto home = td.path() / "home";
    ScopedHome scoped(home);
    Argv args({"spudplate", "run", "ghost"});
    std::stringstream out;
    std::stringstream err;
    ScriptedPrompter prompter({});
    int code = cli_main(args.argc(), args.argv(), out, err, prompter);
    EXPECT_EQ(code, 5);
    EXPECT_NE(err.str().find("'ghost' is not installed"), std::string::npos)
        << err.str();
    EXPECT_NE(err.str().find("spudplate list"), std::string::npos) << err.str();
    EXPECT_EQ(err.str().find("cannot open"), std::string::npos) << err.str();
}

TEST(CliTest, RunByUnknownNameSuggestsClosestInstalled) {
    TmpDir td;
    auto home = td.path() / "home";
    ScopedHome scoped(home);
    install_template(td.path() / "template.spud", "mkdir \"x\"\n");
    Argv args({"spudplate", "run", "templat"});
    std::stringstream out;
    std::stringstream err;
    ScriptedPrompter prompter({});
    int code = cli_main(args.argc(), args.argv(), out, err, prompter);
    EXPECT_EQ(code, 5);
    EXPECT_NE(err.str().find("did you mean 'template'?"), std::string::npos)
        << err.str();
}

TEST(CliTest, RunByUnknownNameOmitsSuggestionForUnrelatedArg) {
    TmpDir td;
    auto home = td.path() / "home";
    ScopedHome scoped(home);
    install_template(td.path() / "template.spud", "mkdir \"x\"\n");
    Argv args({"spudplate", "run", "wxyz123"});
    std::stringstream out;
    std::stringstream err;
    ScriptedPrompter prompter({});
    int code = cli_main(args.argc(), args.argv(), out, err, prompter);
    EXPECT_EQ(code, 5);
    EXPECT_EQ(err.str().find("did you mean"), std::string::npos) << err.str();
    EXPECT_NE(err.str().find("spudplate list"), std::string::npos) << err.str();
}

TEST(CliTest, RunByUnknownNameOmitsSuggestionWhenAmbiguous) {
    TmpDir td;
    auto home = td.path() / "home";
    ScopedHome scoped(home);
    install_template(td.path() / "cat.spud", "mkdir \"x\"\n");
    install_template(td.path() / "bat.spud", "mkdir \"y\"\n");
    Argv args({"spudplate", "run", "rat"});
    std::stringstream out;
    std::stringstream err;
    ScriptedPrompter prompter({});
    int code = cli_main(args.argc(), args.argv(), out, err, prompter);
    EXPECT_EQ(code, 5);
    EXPECT_EQ(err.str().find("did you mean"), std::string::npos) << err.str();
}

// --- list / inspect / uninstall ---

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
    install_template(td.path() / "zebra.spud", "mkdir \"z\"\n");
    install_template(td.path() / "alpha.spud", "mkdir \"a\"\n");
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
    const std::string body = "mkdir \"hello\"\n";
    install_template(td.path() / "demo.spud", body);
    Argv args({"spudplate", "inspect", "demo"});
    std::stringstream out;
    std::stringstream err;
    ScriptedPrompter prompter({});
    int code = cli_main(args.argc(), args.argv(), out, err, prompter);
    EXPECT_EQ(code, 0);
    EXPECT_EQ(out.str(), "demo (v1)\n\n" + body);
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
    install_template(td.path() / "demo.spud", "mkdir \"x\"\n");
    ASSERT_TRUE(std::filesystem::is_regular_file(home / "demo.spp"));
    Argv args({"spudplate", "uninstall", "demo"});
    std::stringstream out;
    std::stringstream err;
    ScriptedPrompter prompter({});
    int code = cli_main(args.argc(), args.argv(), out, err, prompter);
    EXPECT_EQ(code, 0);
    EXPECT_FALSE(std::filesystem::exists(home / "demo.spp"));
}

// --- validate ---

TEST(CliTest, ValidateOkExitsZero) {
    TmpDir td;
    auto src = td.path() / "ok.spud";
    write_file(src, "ask name \"Project name?\" string\nmkdir \"{name}\"\n");
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
    write_file(src, "mkdir \"foo\"\n");
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
               "mkdir \"{local}\"\n");
    Argv args({"spudplate", "validate", src.string()});
    std::stringstream out;
    std::stringstream err;
    ScriptedPrompter prompter({});
    int code = cli_main(args.argc(), args.argv(), out, err, prompter);
    EXPECT_EQ(code, 3);
}

// --- version / update ---

TEST(CliTest, VersionPrintsBakedString) {
    Argv args({"spudplate", "version"});
    std::stringstream out;
    std::stringstream err;
    ScriptedPrompter prompter({});
    int code = cli_main(args.argc(), args.argv(), out, err, prompter);
    EXPECT_EQ(code, 0);
    EXPECT_NE(out.str().find("spudplate v"), std::string::npos);
}

TEST(CliTest, VersionFlagAcceptsDoubleDash) {
    Argv args({"spudplate", "--version"});
    std::stringstream out;
    std::stringstream err;
    ScriptedPrompter prompter({});
    int code = cli_main(args.argc(), args.argv(), out, err, prompter);
    EXPECT_EQ(code, 0);
    EXPECT_NE(out.str().find("spudplate v"), std::string::npos);
}

TEST(CliTest, UpdateWithYesRunsOverrideCommand) {
    ScopedEnv override("SPUDPLATE_UPDATE_COMMAND");
    override.set("true");  // /bin/true succeeds, no network access
    ScopedEnv latest("SPUDPLATE_LATEST_VERSION");
    latest.set("99.0.0");  // pretend a newer release exists
    Argv args({"spudplate", "update", "--yes"});
    std::stringstream out;
    std::stringstream err;
    ScriptedPrompter prompter({});
    int code = cli_main(args.argc(), args.argv(), out, err, prompter);
    EXPECT_EQ(code, 0) << err.str();
    EXPECT_NE(out.str().find("done"), std::string::npos);
    EXPECT_NE(out.str().find("running: true"), std::string::npos);
}

TEST(CliTest, UpdateFailingCommandExitsOne) {
    ScopedEnv override("SPUDPLATE_UPDATE_COMMAND");
    override.set("false");  // /bin/false exits non-zero
    ScopedEnv latest("SPUDPLATE_LATEST_VERSION");
    latest.set("99.0.0");
    Argv args({"spudplate", "update", "--yes"});
    std::stringstream out;
    std::stringstream err;
    ScriptedPrompter prompter({});
    int code = cli_main(args.argc(), args.argv(), out, err, prompter);
    EXPECT_EQ(code, 1);
    EXPECT_NE(err.str().find("update command failed"), std::string::npos);
}

TEST(CliTest, UpdateDeclinedPromptAbortsCleanly) {
    ScopedEnv override("SPUDPLATE_UPDATE_COMMAND");
    // Set to a command that would fail loudly so the test catches any
    // accidental fall-through to execution.
    override.set("false");
    ScopedEnv latest("SPUDPLATE_LATEST_VERSION");
    latest.set("99.0.0");
    Argv args({"spudplate", "update"});
    std::stringstream out;
    std::stringstream err;
    ScriptedPrompter prompter({});
    int code = cli_main(args.argc(), args.argv(), out, err, prompter);
    EXPECT_EQ(code, 0);
    EXPECT_NE(out.str().find("aborted"), std::string::npos);
}

TEST(CliTest, UpdateAlreadyUpToDateExitsZero) {
    ScopedEnv override("SPUDPLATE_UPDATE_COMMAND");
    // If the up-to-date short-circuit fails, this would run /bin/false
    // and surface as exit code 1 — the EQ(code, 0) below would fail.
    override.set("false");
    ScopedEnv latest("SPUDPLATE_LATEST_VERSION");
    latest.set(SPUDPLATE_VERSION_STRING);
    Argv args({"spudplate", "update", "--yes"});
    std::stringstream out;
    std::stringstream err;
    ScriptedPrompter prompter({});
    int code = cli_main(args.argc(), args.argv(), out, err, prompter);
    EXPECT_EQ(code, 0) << err.str();
    EXPECT_NE(out.str().find("already up to date"), std::string::npos);
    EXPECT_EQ(out.str().find("running:"), std::string::npos) << out.str();
}

TEST(CliTest, UpdateForceBypassesUpToDateCheck) {
    ScopedEnv override("SPUDPLATE_UPDATE_COMMAND");
    override.set("true");
    ScopedEnv latest("SPUDPLATE_LATEST_VERSION");
    latest.set(SPUDPLATE_VERSION_STRING);  // would normally short-circuit
    Argv args({"spudplate", "update", "--yes", "--force"});
    std::stringstream out;
    std::stringstream err;
    ScriptedPrompter prompter({});
    int code = cli_main(args.argc(), args.argv(), out, err, prompter);
    EXPECT_EQ(code, 0) << err.str();
    EXPECT_NE(out.str().find("running: true"), std::string::npos);
    EXPECT_EQ(out.str().find("already up to date"), std::string::npos) << out.str();
}

TEST(CliTest, UpdateContinuesOnResolveFailure) {
    ScopedEnv override("SPUDPLATE_UPDATE_COMMAND");
    override.set("true");
    ScopedEnv latest("SPUDPLATE_LATEST_VERSION");
    latest.set("fail");
    Argv args({"spudplate", "update", "--yes"});
    std::stringstream out;
    std::stringstream err;
    ScriptedPrompter prompter({});
    int code = cli_main(args.argc(), args.argv(), out, err, prompter);
    EXPECT_EQ(code, 0) << err.str();
    EXPECT_NE(err.str().find("could not resolve latest version"), std::string::npos);
    EXPECT_NE(out.str().find("running: true"), std::string::npos);
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

// --- spudpack format ---

TEST(CliTest, InstallProducesValidSpudpack) {
    TmpDir td;
    auto home = td.path() / "home";
    ScopedHome scoped(home);
    auto src = td.path() / "demo.spud";
    write_file(src, "mkdir \"foo\"\n");
    Argv args({"spudplate", "install", src.string()});
    std::stringstream out;
    std::stringstream err;
    ScriptedPrompter prompter({});
    ASSERT_EQ(cli_main(args.argc(), args.argv(), out, err, prompter), 0) << err.str();
    spudplate::Spudpack pack = spudplate::spudpack_read_file(home / "demo.spp");
    EXPECT_EQ(pack.source, "mkdir \"foo\"\n");
    EXPECT_FALSE(pack.program_bytes.empty());
}

TEST(CliTest, InstallRejectsSpudpackInputBeforeFileRead) {
    TmpDir td;
    auto home = td.path() / "home";
    ScopedHome scoped(home);
    // Path does not exist on disk. If the rejection fires before the open,
    // the diagnostic is the documented message; otherwise it would be
    // "cannot open: No such file or directory".
    Argv args({"spudplate", "install", "/tmp/does-not-exist.spp"});
    std::stringstream out;
    std::stringstream err;
    ScriptedPrompter prompter({});
    int code = cli_main(args.argc(), args.argv(), out, err, prompter);
    EXPECT_NE(code, 0);
    EXPECT_NE(err.str().find("installing pre-built spudpacks"), std::string::npos);
}

TEST(CliTest, ValidateRejectsSpudpackInput) {
    TmpDir td;
    Argv args({"spudplate", "validate", "/tmp/does-not-exist.spp"});
    std::stringstream out;
    std::stringstream err;
    ScriptedPrompter prompter({});
    int code = cli_main(args.argc(), args.argv(), out, err, prompter);
    EXPECT_NE(code, 0);
    EXPECT_NE(err.str().find("installing pre-built spudpacks"), std::string::npos);
}

TEST(CliTest, RunByNameOnLegacyOnlyAsksForReinstall) {
    TmpDir td;
    auto home = td.path() / "home";
    ScopedHome scoped(home);
    std::filesystem::create_directories(home / "legacy");
    write_file(home / "legacy" / "template.spud", "mkdir \"x\"\n");

    Argv args({"spudplate", "run", "legacy"});
    std::stringstream out;
    std::stringstream err;
    ScriptedPrompter prompter({});
    int code = cli_main(args.argc(), args.argv(), out, err, prompter);
    EXPECT_NE(code, 0);
    EXPECT_NE(err.str().find("legacy install"), std::string::npos);
    EXPECT_NE(err.str().find("reinstall to upgrade"), std::string::npos);
}

TEST(CliTest, UninstallRemovesLegacyDirectory) {
    TmpDir td;
    auto home = td.path() / "home";
    ScopedHome scoped(home);
    std::filesystem::create_directories(home / "legacy");
    write_file(home / "legacy" / "template.spud", "mkdir \"x\"\n");

    Argv args({"spudplate", "uninstall", "legacy"});
    std::stringstream out;
    std::stringstream err;
    ScriptedPrompter prompter({});
    EXPECT_EQ(cli_main(args.argc(), args.argv(), out, err, prompter), 0);
    EXPECT_FALSE(std::filesystem::exists(home / "legacy"));
}

TEST(CliTest, InspectRejectsPathFormArguments) {
    TmpDir td;
    auto home = td.path() / "home";
    ScopedHome scoped(home);
    Argv args({"spudplate", "inspect", "./demo.spp"});
    std::stringstream out;
    std::stringstream err;
    ScriptedPrompter prompter({});
    int code = cli_main(args.argc(), args.argv(), out, err, prompter);
    EXPECT_NE(code, 0);
    EXPECT_NE(err.str().find("inspect takes an installed template name"),
              std::string::npos);
}

TEST(CliTest, UninstallRejectsPathFormArguments) {
    TmpDir td;
    auto home = td.path() / "home";
    ScopedHome scoped(home);
    Argv args({"spudplate", "uninstall", "foo.spud"});
    std::stringstream out;
    std::stringstream err;
    ScriptedPrompter prompter({});
    int code = cli_main(args.argc(), args.argv(), out, err, prompter);
    EXPECT_NE(code, 0);
    EXPECT_NE(err.str().find("uninstall takes an installed template name"),
              std::string::npos);
}

TEST(CliTest, InstallYesRemovesLegacyDirectory) {
    TmpDir td;
    auto home = td.path() / "home";
    ScopedHome scoped(home);
    std::filesystem::create_directories(home / "demo");
    write_file(home / "demo" / "template.spud", "mkdir \"old\"\n");

    auto src = td.path() / "demo.spud";
    write_file(src, "mkdir \"new\"\n");
    Argv args({"spudplate", "install", "--yes", src.string()});
    std::stringstream out;
    std::stringstream err;
    ScriptedPrompter prompter({});
    EXPECT_EQ(cli_main(args.argc(), args.argv(), out, err, prompter), 0) << err.str();
    EXPECT_FALSE(std::filesystem::exists(home / "demo"));
    EXPECT_TRUE(std::filesystem::is_regular_file(home / "demo.spp"));
}

TEST(CliTest, InstallRejectsStrayDirectoryAtTarget) {
    TmpDir td;
    auto home = td.path() / "home";
    ScopedHome scoped(home);
    std::filesystem::create_directories(home / "demo.spp");  // stray dir

    auto src = td.path() / "demo.spud";
    write_file(src, "mkdir \"x\"\n");
    Argv args({"spudplate", "install", "--yes", src.string()});
    std::stringstream out;
    std::stringstream err;
    ScriptedPrompter prompter({});
    int code = cli_main(args.argc(), args.argv(), out, err, prompter);
    EXPECT_NE(code, 0);
    EXPECT_NE(err.str().find("not a regular file"), std::string::npos);
    // The stray dir is preserved; no .spp.tmp left behind.
    EXPECT_TRUE(std::filesystem::is_directory(home / "demo.spp"));
    EXPECT_FALSE(std::filesystem::exists(home / "demo.spp.tmp"));
}

TEST(CliTest, InstallRenameFailureCleansUpTempFile) {
    TmpDir td;
    auto home = td.path() / "home";
    ScopedHome scoped(home);
    auto src = td.path() / "demo.spud";
    write_file(src, "mkdir \"x\"\n");

    struct RenameGuard {
        spudplate::cli_internal::RenameFn prev;
        RenameGuard() {
            prev = spudplate::cli_internal::install_rename_fn();
            spudplate::cli_internal::install_rename_fn() =
                [](const std::filesystem::path&, const std::filesystem::path&) {
                    throw std::filesystem::filesystem_error("stub failure",
                                                            std::error_code{});
                };
        }
        ~RenameGuard() { spudplate::cli_internal::install_rename_fn() = prev; }
    } guard;

    Argv args({"spudplate", "install", "--yes", src.string()});
    std::stringstream out;
    std::stringstream err;
    ScriptedPrompter prompter({});
    int code = cli_main(args.argc(), args.argv(), out, err, prompter);
    EXPECT_NE(code, 0);
    EXPECT_NE(err.str().find("cannot finalise install"), std::string::npos);
    EXPECT_FALSE(std::filesystem::exists(home / "demo.spp"));
    EXPECT_FALSE(std::filesystem::exists(home / "demo.spp.tmp"));
}

TEST(CliTest, InspectPrintsSourceFromSpudpack) {
    TmpDir td;
    auto home = td.path() / "home";
    ScopedHome scoped(home);
    auto src = td.path() / "demo.spud";
    std::string body = "ask name \"Name?\" string\nmkdir \"{name}\"\n";
    write_file(src, body);
    {
        Argv args({"spudplate", "install", src.string()});
        std::stringstream o, e;
        ScriptedPrompter p({});
        ASSERT_EQ(cli_main(args.argc(), args.argv(), o, e, p), 0) << e.str();
    }

    Argv args({"spudplate", "inspect", "demo"});
    std::stringstream out;
    std::stringstream err;
    ScriptedPrompter prompter({});
    EXPECT_EQ(cli_main(args.argc(), args.argv(), out, err, prompter), 0) << err.str();
    EXPECT_EQ(out.str(), "demo (v1)\n\n" + body);
}

TEST(CliTest, InstallRunDeleteSourceMaterialisesAssets) {
    // Headline end-to-end: install a template that bundles assets, delete
    // the source tree, then run by name from a fresh cwd. The bundled
    // bytes must materialise even though /tmp/<source> is gone.
    TmpDir source_td;
    auto source_root = source_td.path() / "src_root";
    std::filesystem::create_directories(source_root / "tpl");
    std::filesystem::create_directories(source_root / "assets");
    std::vector<std::uint8_t> bytes{0x89, 'P', 'N', 'G', 0x00, 0x42, 0xFF};
    std::ofstream(source_root / "assets" / "logo.bin", std::ios::binary)
        .write(reinterpret_cast<const char*>(bytes.data()),
               static_cast<std::streamsize>(bytes.size()));
    write_file(source_root / "tpl" / "main.txt", "hello\n");
    write_file(source_root / "demo.spud",
               "mkdir \"out\"\n"
               "mkdir \"out/assets\"\n"
               "file \"out/main.txt\" from \"tpl/main.txt\"\n"
               "file \"out/assets/logo.bin\" from \"assets/logo.bin\"\n");

    auto home = source_td.path() / "home";
    ScopedHome scoped(home);
    {
        Argv args({"spudplate", "install", (source_root / "demo.spud").string()});
        std::stringstream o, e;
        ScriptedPrompter p({});
        ASSERT_EQ(cli_main(args.argc(), args.argv(), o, e, p), 0) << e.str();
    }

    // Wipe the source tree to prove the run is cwd-independent.
    std::filesystem::remove_all(source_root);

    TmpDir run_td;  // fresh cwd
    Argv args({"spudplate", "run", "--yes", "demo"});
    std::stringstream out;
    std::stringstream err;
    ScriptedPrompter prompter({});
    EXPECT_EQ(cli_main(args.argc(), args.argv(), out, err, prompter), 0) << err.str();

    auto materialised = run_td.path() / "out" / "assets" / "logo.bin";
    ASSERT_TRUE(std::filesystem::is_regular_file(materialised));
    std::ifstream in(materialised, std::ios::binary);
    std::vector<std::uint8_t> got((std::istreambuf_iterator<char>(in)),
                                  std::istreambuf_iterator<char>());
    EXPECT_EQ(got, bytes);
}

TEST(CliTest, ListWarnsAboutShadowedLegacy) {
    TmpDir td;
    auto home = td.path() / "home";
    ScopedHome scoped(home);
    auto src = td.path() / "demo.spud";
    write_file(src, "mkdir \"x\"\n");
    {
        Argv args({"spudplate", "install", src.string()});
        std::stringstream o, e;
        ScriptedPrompter p({});
        ASSERT_EQ(cli_main(args.argc(), args.argv(), o, e, p), 0);
    }
    // Hand-craft a parallel legacy directory.
    std::filesystem::create_directories(home / "demo");
    write_file(home / "demo" / "template.spud", "mkdir \"y\"\n");

    Argv args({"spudplate", "list"});
    std::stringstream out;
    std::stringstream err;
    ScriptedPrompter prompter({});
    EXPECT_EQ(cli_main(args.argc(), args.argv(), out, err, prompter), 0);
    EXPECT_EQ(out.str(), "demo\n");
    EXPECT_NE(err.str().find("shadowed"), std::string::npos);
}

TEST(CliTest, TopLevelLongHelpPrintsUsageToStdout) {
    Argv args({"spudplate", "--help"});
    std::stringstream out;
    std::stringstream err;
    ScriptedPrompter prompter({});
    int code = cli_main(args.argc(), args.argv(), out, err, prompter);
    EXPECT_EQ(code, 0);
    EXPECT_EQ(err.str(), "");
    EXPECT_NE(out.str().find("usage: spudplate"), std::string::npos);
    EXPECT_NE(out.str().find("--help"), std::string::npos);
}

TEST(CliTest, TopLevelShortHelpPrintsUsageToStdout) {
    Argv args({"spudplate", "-h"});
    std::stringstream out;
    std::stringstream err;
    ScriptedPrompter prompter({});
    int code = cli_main(args.argc(), args.argv(), out, err, prompter);
    EXPECT_EQ(code, 0);
    EXPECT_EQ(err.str(), "");
    EXPECT_NE(out.str().find("usage: spudplate"), std::string::npos);
}

TEST(CliTest, TopLevelHelpWordPrintsUsageToStdout) {
    Argv args({"spudplate", "help"});
    std::stringstream out;
    std::stringstream err;
    ScriptedPrompter prompter({});
    int code = cli_main(args.argc(), args.argv(), out, err, prompter);
    EXPECT_EQ(code, 0);
    EXPECT_EQ(err.str(), "");
    EXPECT_NE(out.str().find("usage: spudplate"), std::string::npos);
}

TEST(CliTest, RunHelpDescribesRunFlags) {
    Argv args({"spudplate", "run", "-h"});
    std::stringstream out;
    std::stringstream err;
    ScriptedPrompter prompter({});
    int code = cli_main(args.argc(), args.argv(), out, err, prompter);
    EXPECT_EQ(code, 0);
    EXPECT_EQ(err.str(), "");
    EXPECT_NE(out.str().find("usage: spudplate run"), std::string::npos);
    EXPECT_NE(out.str().find("--dry-run"), std::string::npos);
    EXPECT_NE(out.str().find("--no-timeout"), std::string::npos);
}

TEST(CliTest, RunLongHelpDescribesRunFlags) {
    Argv args({"spudplate", "run", "--help"});
    std::stringstream out;
    std::stringstream err;
    ScriptedPrompter prompter({});
    int code = cli_main(args.argc(), args.argv(), out, err, prompter);
    EXPECT_EQ(code, 0);
    EXPECT_NE(out.str().find("usage: spudplate run"), std::string::npos);
}

TEST(CliTest, InstallHelpDescribesYesFlag) {
    Argv args({"spudplate", "install", "--help"});
    std::stringstream out;
    std::stringstream err;
    ScriptedPrompter prompter({});
    int code = cli_main(args.argc(), args.argv(), out, err, prompter);
    EXPECT_EQ(code, 0);
    EXPECT_NE(out.str().find("usage: spudplate install"), std::string::npos);
    EXPECT_NE(out.str().find("--yes"), std::string::npos);
}

TEST(CliTest, ValidateHelpPrintsToStdout) {
    Argv args({"spudplate", "validate", "-h"});
    std::stringstream out;
    std::stringstream err;
    ScriptedPrompter prompter({});
    int code = cli_main(args.argc(), args.argv(), out, err, prompter);
    EXPECT_EQ(code, 0);
    EXPECT_NE(out.str().find("usage: spudplate validate"), std::string::npos);
}

TEST(CliTest, ListHelpPrintsToStdout) {
    Argv args({"spudplate", "list", "--help"});
    std::stringstream out;
    std::stringstream err;
    ScriptedPrompter prompter({});
    int code = cli_main(args.argc(), args.argv(), out, err, prompter);
    EXPECT_EQ(code, 0);
    EXPECT_NE(out.str().find("usage: spudplate list"), std::string::npos);
}

TEST(CliTest, InspectHelpPrintsToStdout) {
    Argv args({"spudplate", "inspect", "-h"});
    std::stringstream out;
    std::stringstream err;
    ScriptedPrompter prompter({});
    int code = cli_main(args.argc(), args.argv(), out, err, prompter);
    EXPECT_EQ(code, 0);
    EXPECT_NE(out.str().find("usage: spudplate inspect"), std::string::npos);
}

TEST(CliTest, UninstallHelpPrintsToStdout) {
    Argv args({"spudplate", "uninstall", "--help"});
    std::stringstream out;
    std::stringstream err;
    ScriptedPrompter prompter({});
    int code = cli_main(args.argc(), args.argv(), out, err, prompter);
    EXPECT_EQ(code, 0);
    EXPECT_NE(out.str().find("usage: spudplate uninstall"), std::string::npos);
}

TEST(CliTest, VersionHelpPrintsToStdoutWithoutVersion) {
    Argv args({"spudplate", "version", "-h"});
    std::stringstream out;
    std::stringstream err;
    ScriptedPrompter prompter({});
    int code = cli_main(args.argc(), args.argv(), out, err, prompter);
    EXPECT_EQ(code, 0);
    EXPECT_NE(out.str().find("usage: spudplate version"), std::string::npos);
    EXPECT_NE(out.str().find("Print the spudplate version"), std::string::npos);
}

TEST(CliTest, UpdateHelpDoesNotRunUpdate) {
    Argv args({"spudplate", "update", "--help"});
    std::stringstream out;
    std::stringstream err;
    ScriptedPrompter prompter({});
    int code = cli_main(args.argc(), args.argv(), out, err, prompter);
    EXPECT_EQ(code, 0);
    EXPECT_NE(out.str().find("usage: spudplate update"), std::string::npos);
    // The "current version" line is only printed by the real update path.
    EXPECT_EQ(out.str().find("current version"), std::string::npos);
}

// --- completion ---

TEST(CliTest, CompletionBashEmitsBashScript) {
    Argv args({"spudplate", "completion", "bash"});
    std::stringstream out;
    std::stringstream err;
    ScriptedPrompter prompter({});
    int code = cli_main(args.argc(), args.argv(), out, err, prompter);
    EXPECT_EQ(code, 0) << err.str();
    EXPECT_NE(out.str().find("_spudplate"), std::string::npos);
    EXPECT_NE(out.str().find("complete -F _spudplate spudplate"), std::string::npos);
    EXPECT_NE(out.str().find("compgen"), std::string::npos);
    EXPECT_NE(out.str().find("install run validate list"), std::string::npos);
}

TEST(CliTest, CompletionZshEmitsZshScript) {
    Argv args({"spudplate", "completion", "zsh"});
    std::stringstream out;
    std::stringstream err;
    ScriptedPrompter prompter({});
    int code = cli_main(args.argc(), args.argv(), out, err, prompter);
    EXPECT_EQ(code, 0) << err.str();
    EXPECT_NE(out.str().find("#compdef spudplate"), std::string::npos);
    EXPECT_NE(out.str().find("_describe"), std::string::npos);
    EXPECT_NE(out.str().find("_files"), std::string::npos);
}

TEST(CliTest, CompletionMissingShellPrintsUsage) {
    Argv args({"spudplate", "completion"});
    std::stringstream out;
    std::stringstream err;
    ScriptedPrompter prompter({});
    int code = cli_main(args.argc(), args.argv(), out, err, prompter);
    EXPECT_EQ(code, 1);
    EXPECT_NE(err.str().find("usage: spudplate completion"), std::string::npos);
}

TEST(CliTest, CompletionUnknownShellRejected) {
    Argv args({"spudplate", "completion", "fish"});
    std::stringstream out;
    std::stringstream err;
    ScriptedPrompter prompter({});
    int code = cli_main(args.argc(), args.argv(), out, err, prompter);
    EXPECT_EQ(code, 1);
    EXPECT_NE(err.str().find("unknown shell 'fish'"), std::string::npos);
}

TEST(CliTest, CompletionHelpPrintsToStdout) {
    Argv args({"spudplate", "completion", "--help"});
    std::stringstream out;
    std::stringstream err;
    ScriptedPrompter prompter({});
    int code = cli_main(args.argc(), args.argv(), out, err, prompter);
    EXPECT_EQ(code, 0);
    EXPECT_NE(out.str().find("usage: spudplate completion"), std::string::npos);
    EXPECT_EQ(out.str().find("compgen"), std::string::npos);  // not the script
}

TEST(CliTest, TopLevelUsageMentionsCompletion) {
    Argv args({"spudplate", "--help"});
    std::stringstream out;
    std::stringstream err;
    ScriptedPrompter prompter({});
    int code = cli_main(args.argc(), args.argv(), out, err, prompter);
    EXPECT_EQ(code, 0);
    EXPECT_NE(out.str().find("completion"), std::string::npos);
}

// --- self-uninstall ---

namespace {
struct SelfUninstallEnv {
    TmpDir td;
    std::filesystem::path home;
    std::filesystem::path bin;
    std::filesystem::path bash_completion;
    std::filesystem::path zsh_completion;
    std::filesystem::path zshrc;
    std::filesystem::path templates;
    ScopedEnv home_env{"HOME"};
    ScopedEnv self_bin{"SPUDPLATE_SELF_BINARY"};
    ScopedHome spudhome;

    SelfUninstallEnv() : home(td.path() / "home"), templates(home / "templates"),
                         spudhome(templates) {
        bin = home / ".local" / "bin" / "spudplate";
        bash_completion = home / ".local" / "share" / "bash-completion" /
                          "completions" / "spudplate";
        zsh_completion = home / ".zsh" / "completions" / "_spudplate";
        zshrc = home / ".zshrc";
        std::filesystem::create_directories(bin.parent_path());
        std::filesystem::create_directories(bash_completion.parent_path());
        std::filesystem::create_directories(zsh_completion.parent_path());
        std::filesystem::create_directories(templates);
        std::ofstream(bin) << "fake-binary\n";
        std::ofstream(bash_completion) << "fake-bash-completion\n";
        std::ofstream(zsh_completion) << "fake-zsh-completion\n";
        write_zshrc_with_block();
        home_env.set(home.string());
        self_bin.set(bin.string());
    }

    void write_zshrc_with_block() {
        std::ofstream f(zshrc);
        f << "# user config\n";
        f << "alias ll=\"ls -la\"\n";
        f << "\n";
        f << "# >>> spudplate completion >>>\n";
        f << "fpath=(~/.zsh/completions $fpath)\n";
        f << "autoload -U compinit && compinit\n";
        f << "# <<< spudplate completion <<<\n";
        f << "\n";
        f << "export EDITOR=vim\n";
    }

    void add_template(const std::string& name) {
        std::ofstream(templates / (name + ".spp")) << "fake-spp\n";
    }
};
}  // namespace

TEST(CliTest, SelfUninstallRemovesBinaryAndCompletions) {
    SelfUninstallEnv env;
    Argv args({"spudplate", "self-uninstall", "--yes"});
    std::stringstream out;
    std::stringstream err;
    ScriptedPrompter prompter({});
    int code = cli_main(args.argc(), args.argv(), out, err, prompter);
    EXPECT_EQ(code, 0) << err.str();
    EXPECT_FALSE(std::filesystem::exists(env.bin));
    EXPECT_FALSE(std::filesystem::exists(env.bash_completion));
    EXPECT_FALSE(std::filesystem::exists(env.zsh_completion));
    std::ifstream in(env.zshrc);
    std::string content((std::istreambuf_iterator<char>(in)),
                        std::istreambuf_iterator<char>());
    EXPECT_EQ(content.find("spudplate completion"), std::string::npos)
        << content;
    EXPECT_NE(content.find("alias ll"), std::string::npos) << content;
    EXPECT_NE(content.find("EDITOR=vim"), std::string::npos) << content;
}

TEST(CliTest, SelfUninstallDefaultPreservesTemplates) {
    SelfUninstallEnv env;
    env.add_template("alpha");
    env.add_template("beta");
    Argv args({"spudplate", "self-uninstall", "--yes"});
    std::stringstream out;
    std::stringstream err;
    ScriptedPrompter prompter({});
    int code = cli_main(args.argc(), args.argv(), out, err, prompter);
    EXPECT_EQ(code, 0) << err.str();
    EXPECT_TRUE(std::filesystem::exists(env.templates / "alpha.spp"));
    EXPECT_TRUE(std::filesystem::exists(env.templates / "beta.spp"));
    EXPECT_NE(out.str().find("will be preserved"), std::string::npos);
}

TEST(CliTest, SelfUninstallPurgeRemovesTemplates) {
    SelfUninstallEnv env;
    env.add_template("alpha");
    env.add_template("beta");
    Argv args({"spudplate", "self-uninstall", "--purge", "--yes"});
    std::stringstream out;
    std::stringstream err;
    ScriptedPrompter prompter({});
    int code = cli_main(args.argc(), args.argv(), out, err, prompter);
    EXPECT_EQ(code, 0) << err.str();
    EXPECT_FALSE(std::filesystem::exists(env.templates / "alpha.spp"));
    EXPECT_FALSE(std::filesystem::exists(env.templates / "beta.spp"));
    EXPECT_NE(out.str().find("2 .spp files"), std::string::npos);
}

TEST(CliTest, SelfUninstallPurgeWarnsIrreversibleEvenWithYes) {
    SelfUninstallEnv env;
    env.add_template("alpha");
    Argv args({"spudplate", "self-uninstall", "--purge", "--yes"});
    std::stringstream out;
    std::stringstream err;
    ScriptedPrompter prompter({});
    int code = cli_main(args.argc(), args.argv(), out, err, prompter);
    EXPECT_EQ(code, 0) << err.str();
    EXPECT_NE(out.str().find("irreversible"), std::string::npos);
}

TEST(CliTest, SelfUninstallDeclinedPromptAbortsCleanly) {
    SelfUninstallEnv env;
    Argv args({"spudplate", "self-uninstall"});
    std::stringstream out;
    std::stringstream err;
    ScriptedPrompter prompter({});
    int code = cli_main(args.argc(), args.argv(), out, err, prompter);
    EXPECT_EQ(code, 0);
    EXPECT_TRUE(std::filesystem::exists(env.bin));
    EXPECT_TRUE(std::filesystem::exists(env.bash_completion));
    EXPECT_TRUE(std::filesystem::exists(env.zsh_completion));
    EXPECT_NE(out.str().find("aborted"), std::string::npos);
}

TEST(CliTest, SelfUninstallIdempotentWhenNothingPresent) {
    SelfUninstallEnv env;
    std::filesystem::remove(env.bin);
    std::filesystem::remove(env.bash_completion);
    std::filesystem::remove(env.zsh_completion);
    std::filesystem::remove(env.zshrc);
    Argv args({"spudplate", "self-uninstall", "--yes"});
    std::stringstream out;
    std::stringstream err;
    ScriptedPrompter prompter({});
    int code = cli_main(args.argc(), args.argv(), out, err, prompter);
    EXPECT_EQ(code, 0) << err.str();
    EXPECT_NE(out.str().find("done"), std::string::npos);
}

TEST(CliTest, SelfUninstallHelpPrintsToStdout) {
    Argv args({"spudplate", "self-uninstall", "--help"});
    std::stringstream out;
    std::stringstream err;
    ScriptedPrompter prompter({});
    int code = cli_main(args.argc(), args.argv(), out, err, prompter);
    EXPECT_EQ(code, 0);
    EXPECT_NE(out.str().find("usage: spudplate self-uninstall"), std::string::npos);
}

// --- include with bundled deps ---------------------------------------------

TEST(CliTest, InstallParentBundlesIncludedDep) {
    TmpDir td;
    ScopedHome home(td.path() / "home");

    write_file(td.path() / "child.spud", "ask greeting \"Greeting?\" string\n");
    {
        Argv args({"spudplate", "install", (td.path() / "child.spud").string()});
        std::stringstream out;
        std::stringstream err;
        ScriptedPrompter prompter({});
        int code = cli_main(args.argc(), args.argv(), out, err, prompter);
        ASSERT_EQ(code, 0) << err.str();
    }

    write_file(td.path() / "parent.spud",
               "ask before \"Before?\" string\n"
               "include child\n"
               "ask after \"After?\" string\n");
    {
        Argv args({"spudplate", "install", (td.path() / "parent.spud").string()});
        std::stringstream out;
        std::stringstream err;
        ScriptedPrompter prompter({});
        int code = cli_main(args.argc(), args.argv(), out, err, prompter);
        ASSERT_EQ(code, 0) << err.str();
    }

    auto pack = spudplate::spudpack_read_file(td.path() / "home" / "parent.spp");
    ASSERT_EQ(pack.deps.size(), 1u);
    EXPECT_EQ(pack.deps[0].name, "child");
}

TEST(CliTest, RunInstalledParentPromptsInSourceOrder) {
    TmpDir td;
    ScopedHome home(td.path() / "home");

    write_file(td.path() / "child.spud", "ask middle \"Middle?\" string\n");
    {
        Argv args({"spudplate", "install", (td.path() / "child.spud").string()});
        std::stringstream out;
        std::stringstream err;
        ScriptedPrompter prompter({});
        ASSERT_EQ(cli_main(args.argc(), args.argv(), out, err, prompter), 0)
            << err.str();
    }

    write_file(td.path() / "parent.spud",
               "ask before \"Before?\" string\n"
               "include child\n"
               "ask after \"After?\" string\n"
               "mkdir \"{before}_{after}\"\n");
    {
        Argv args({"spudplate", "install", (td.path() / "parent.spud").string()});
        std::stringstream out;
        std::stringstream err;
        ScriptedPrompter prompter({});
        ASSERT_EQ(cli_main(args.argc(), args.argv(), out, err, prompter), 0)
            << err.str();
    }

    // Uninstall the child to prove the parent is now self-contained: a
    // freshly installed parent must run without the dep being separately
    // present on the install root.
    {
        Argv args({"spudplate", "uninstall", "child"});
        std::stringstream out;
        std::stringstream err;
        ScriptedPrompter prompter({});
        ASSERT_EQ(cli_main(args.argc(), args.argv(), out, err, prompter), 0)
            << err.str();
    }

    Argv run_args({"spudplate", "run", "parent"});
    std::stringstream run_out;
    std::stringstream run_err;
    // Source order: before -> middle (from child) -> after.
    ScriptedPrompter run_prompter({"start", "mid", "end"});
    int code = cli_main(run_args.argc(), run_args.argv(), run_out, run_err,
                        run_prompter);
    EXPECT_EQ(code, 0) << run_err.str();
    EXPECT_TRUE(std::filesystem::is_directory(td.path() / "start_end"));
}

// --- Version tags: install/bump/archive/sticky/pin/drift -------------------

namespace {

// Run a single CLI invocation with the supplied argv and return its stdout,
// stderr, and exit code. Helps the version-tag tests stay readable.
struct CliRun {
    int code;
    std::string out;
    std::string err;
};

CliRun run_cli(std::vector<std::string> argv) {
    Argv args(argv);
    std::stringstream out, err;
    ScriptedPrompter p({});
    int code = cli_main(args.argc(), args.argv(), out, err, p);
    return {code, out.str(), err.str()};
}

}  // namespace

TEST(CliTest, FirstInstallTagsAsV1) {
    TmpDir td;
    auto home_path = td.path() / "home";
    ScopedHome home(home_path);
    write_file(td.path() / "foo.spud", "ask q \"q?\" string default \"x\"\n");
    auto r = run_cli({"spudplate", "install",
                      (td.path() / "foo.spud").string()});
    ASSERT_EQ(r.code, 0) << r.err;
    EXPECT_NE(r.out.find("(v1)"), std::string::npos) << r.out;
    auto p = spudplate::spudpack_read_file(home_path / "foo.spp");
    EXPECT_EQ(p.version_tag, 1u);
}

TEST(CliTest, IdenticalReinstallIsAlreadyUpToDate) {
    TmpDir td;
    auto home_path = td.path() / "home";
    ScopedHome home(home_path);
    write_file(td.path() / "foo.spud", "ask q \"q?\" string default \"x\"\n");
    ASSERT_EQ(run_cli({"spudplate", "install",
                       (td.path() / "foo.spud").string()})
                  .code,
              0);
    auto r = run_cli(
        {"spudplate", "install", (td.path() / "foo.spud").string()});
    EXPECT_EQ(r.code, 0) << r.err;
    EXPECT_NE(r.out.find("already up to date"), std::string::npos) << r.out;
    auto p = spudplate::spudpack_read_file(home_path / "foo.spp");
    EXPECT_EQ(p.version_tag, 1u);
}

TEST(CliTest, ChangedReinstallBumpsAndArchives) {
    TmpDir td;
    auto home_path = td.path() / "home";
    ScopedHome home(home_path);
    write_file(td.path() / "foo.spud", "ask q \"q?\" string default \"x\"\n");
    ASSERT_EQ(run_cli({"spudplate", "install",
                       (td.path() / "foo.spud").string()})
                  .code,
              0);
    write_file(td.path() / "foo.spud", "ask q \"q changed?\" string default \"x\"\n");
    auto r = run_cli({"spudplate", "install", "--yes",
                      (td.path() / "foo.spud").string()});
    ASSERT_EQ(r.code, 0) << r.err;
    EXPECT_NE(r.out.find("(v2)"), std::string::npos) << r.out;
    auto p = spudplate::spudpack_read_file(home_path / "foo.spp");
    EXPECT_EQ(p.version_tag, 2u);
    EXPECT_TRUE(std::filesystem::is_regular_file(home_path / ".archive" /
                                                 "foo.v1.spp"));
}

TEST(CliTest, ParentBundlesDepAtCurrentVersion) {
    TmpDir td;
    auto home_path = td.path() / "home";
    ScopedHome home(home_path);
    write_file(td.path() / "child.spud",
               "ask q \"q?\" string default \"x\"\n");
    ASSERT_EQ(run_cli({"spudplate", "install",
                       (td.path() / "child.spud").string()})
                  .code,
              0);
    write_file(td.path() / "child.spud",
               "ask q \"q v2?\" string default \"x\"\n");
    ASSERT_EQ(run_cli({"spudplate", "install", "--yes",
                       (td.path() / "child.spud").string()})
                  .code,
              0);
    write_file(td.path() / "parent.spud", "include child\n");
    ASSERT_EQ(run_cli({"spudplate", "install",
                       (td.path() / "parent.spud").string()})
                  .code,
              0);
    auto p = spudplate::spudpack_read_file(home_path / "parent.spp");
    ASSERT_EQ(p.deps.size(), 1u);
    EXPECT_EQ(p.deps[0].name, "child");
    EXPECT_EQ(p.deps[0].version_tag, 2u);
}

TEST(CliTest, ReinstallParentIsStickyForUnpinnedDeps) {
    TmpDir td;
    auto home_path = td.path() / "home";
    ScopedHome home(home_path);
    write_file(td.path() / "child.spud",
               "ask q \"q v1?\" string default \"x\"\n");
    ASSERT_EQ(run_cli({"spudplate", "install",
                       (td.path() / "child.spud").string()})
                  .code,
              0);
    write_file(td.path() / "parent.spud", "include child\n");
    ASSERT_EQ(run_cli({"spudplate", "install",
                       (td.path() / "parent.spud").string()})
                  .code,
              0);
    // Child gets bumped to v2 in the install root.
    write_file(td.path() / "child.spud",
               "ask q \"q v2?\" string default \"x\"\n");
    ASSERT_EQ(run_cli({"spudplate", "install", "--yes",
                       (td.path() / "child.spud").string()})
                  .code,
              0);
    // Parent reinstall without --update-deps must keep child at v1 - the
    // bytes are unchanged, so the bundler's content compare flags this
    // as a no-op.
    auto r = run_cli({"spudplate", "install", "--yes",
                      (td.path() / "parent.spud").string()});
    ASSERT_EQ(r.code, 0) << r.err;
    EXPECT_NE(r.out.find("already up to date"), std::string::npos) << r.out;
    auto p = spudplate::spudpack_read_file(home_path / "parent.spp");
    EXPECT_EQ(p.deps[0].version_tag, 1u);
}

TEST(CliTest, UpdateDepsRefreshesBundledDep) {
    TmpDir td;
    auto home_path = td.path() / "home";
    ScopedHome home(home_path);
    write_file(td.path() / "child.spud",
               "ask q \"q v1?\" string default \"x\"\n");
    ASSERT_EQ(run_cli({"spudplate", "install",
                       (td.path() / "child.spud").string()})
                  .code,
              0);
    write_file(td.path() / "parent.spud", "include child\n");
    ASSERT_EQ(run_cli({"spudplate", "install",
                       (td.path() / "parent.spud").string()})
                  .code,
              0);
    write_file(td.path() / "child.spud",
               "ask q \"q v2?\" string default \"x\"\n");
    ASSERT_EQ(run_cli({"spudplate", "install", "--yes",
                       (td.path() / "child.spud").string()})
                  .code,
              0);
    auto r = run_cli({"spudplate", "install", "--yes", "--update-deps",
                      "child", (td.path() / "parent.spud").string()});
    ASSERT_EQ(r.code, 0) << r.err;
    auto p = spudplate::spudpack_read_file(home_path / "parent.spp");
    EXPECT_EQ(p.deps[0].version_tag, 2u);
    EXPECT_EQ(p.version_tag, 2u);  // parent itself bumped because content changed
}

TEST(CliTest, PinResolvesAgainstArchive) {
    TmpDir td;
    auto home_path = td.path() / "home";
    ScopedHome home(home_path);
    write_file(td.path() / "child.spud",
               "ask q \"q v1?\" string default \"x\"\n");
    ASSERT_EQ(run_cli({"spudplate", "install",
                       (td.path() / "child.spud").string()})
                  .code,
              0);
    write_file(td.path() / "child.spud",
               "ask q \"q v2?\" string default \"x\"\n");
    ASSERT_EQ(run_cli({"spudplate", "install", "--yes",
                       (td.path() / "child.spud").string()})
                  .code,
              0);
    // child is now v2 in the install root; v1 is in archive.
    write_file(td.path() / "parent.spud", "include child@1\n");
    auto r = run_cli({"spudplate", "install",
                      (td.path() / "parent.spud").string()});
    ASSERT_EQ(r.code, 0) << r.err;
    auto p = spudplate::spudpack_read_file(home_path / "parent.spp");
    EXPECT_EQ(p.deps[0].version_tag, 1u);
}

TEST(CliTest, PinForUnknownVersionFails) {
    TmpDir td;
    auto home_path = td.path() / "home";
    ScopedHome home(home_path);
    write_file(td.path() / "child.spud",
               "ask q \"q?\" string default \"x\"\n");
    ASSERT_EQ(run_cli({"spudplate", "install",
                       (td.path() / "child.spud").string()})
                  .code,
              0);
    write_file(td.path() / "parent.spud", "include child@9\n");
    auto r = run_cli({"spudplate", "install",
                      (td.path() / "parent.spud").string()});
    EXPECT_NE(r.code, 0);
    EXPECT_NE(r.err.find("version pin v9"), std::string::npos) << r.err;
}

TEST(CliTest, UpdateDepsOnPinnedPrintsNoteAndDoesNothing) {
    TmpDir td;
    auto home_path = td.path() / "home";
    ScopedHome home(home_path);
    write_file(td.path() / "child.spud",
               "ask q \"q v1?\" string default \"x\"\n");
    ASSERT_EQ(run_cli({"spudplate", "install",
                       (td.path() / "child.spud").string()})
                  .code,
              0);
    write_file(td.path() / "parent.spud", "include child@1\n");
    ASSERT_EQ(run_cli({"spudplate", "install",
                       (td.path() / "parent.spud").string()})
                  .code,
              0);
    auto r = run_cli({"spudplate", "install", "--yes", "--update-deps",
                      "child", (td.path() / "parent.spud").string()});
    EXPECT_EQ(r.code, 0) << r.err;
    EXPECT_NE(r.err.find("'child' is pinned"), std::string::npos) << r.err;
    EXPECT_NE(r.out.find("already up to date"), std::string::npos) << r.out;
}

TEST(CliTest, UninstallSweepsArchiveEntries) {
    TmpDir td;
    auto home_path = td.path() / "home";
    ScopedHome home(home_path);
    write_file(td.path() / "foo.spud",
               "ask q \"q v1?\" string default \"x\"\n");
    ASSERT_EQ(run_cli({"spudplate", "install",
                       (td.path() / "foo.spud").string()})
                  .code,
              0);
    write_file(td.path() / "foo.spud",
               "ask q \"q v2?\" string default \"x\"\n");
    ASSERT_EQ(run_cli({"spudplate", "install", "--yes",
                       (td.path() / "foo.spud").string()})
                  .code,
              0);
    EXPECT_TRUE(std::filesystem::is_regular_file(home_path / ".archive" /
                                                 "foo.v1.spp"));
    auto r = run_cli({"spudplate", "uninstall", "foo"});
    EXPECT_EQ(r.code, 0) << r.err;
    EXPECT_FALSE(std::filesystem::exists(home_path / "foo.spp"));
    EXPECT_FALSE(std::filesystem::exists(home_path / ".archive" /
                                         "foo.v1.spp"));
}

TEST(CliTest, UninstallLeavesOtherNamesArchive) {
    TmpDir td;
    auto home_path = td.path() / "home";
    ScopedHome home(home_path);
    write_file(td.path() / "foo.spud", "ask q \"q?\" string default \"x\"\n");
    write_file(td.path() / "foobar.spud",
               "ask q \"q?\" string default \"x\"\n");
    ASSERT_EQ(run_cli({"spudplate", "install",
                       (td.path() / "foo.spud").string()})
                  .code,
              0);
    ASSERT_EQ(run_cli({"spudplate", "install",
                       (td.path() / "foobar.spud").string()})
                  .code,
              0);
    write_file(td.path() / "foobar.spud",
               "ask q \"q v2?\" string default \"x\"\n");
    ASSERT_EQ(run_cli({"spudplate", "install", "--yes",
                       (td.path() / "foobar.spud").string()})
                  .code,
              0);
    // Uninstalling foo must not touch foobar.v1.spp (different name).
    auto r = run_cli({"spudplate", "uninstall", "foo"});
    ASSERT_EQ(r.code, 0) << r.err;
    EXPECT_TRUE(std::filesystem::is_regular_file(home_path / ".archive" /
                                                 "foobar.v1.spp"));
}

TEST(CliTest, InspectAtArchivedVersionPrintsArchivedSource) {
    TmpDir td;
    auto home_path = td.path() / "home";
    ScopedHome home(home_path);
    const std::string v1_body = "ask q \"v1 prompt?\" string default \"x\"\n";
    const std::string v2_body = "ask q \"v2 prompt?\" string default \"x\"\n";
    write_file(td.path() / "foo.spud", v1_body);
    ASSERT_EQ(run_cli({"spudplate", "install",
                       (td.path() / "foo.spud").string()})
                  .code,
              0);
    write_file(td.path() / "foo.spud", v2_body);
    ASSERT_EQ(run_cli({"spudplate", "install", "--yes",
                       (td.path() / "foo.spud").string()})
                  .code,
              0);
    auto r = run_cli({"spudplate", "inspect", "foo@1"});
    EXPECT_EQ(r.code, 0) << r.err;
    EXPECT_NE(r.out.find("foo (v1)"), std::string::npos) << r.out;
    EXPECT_NE(r.out.find("v1 prompt"), std::string::npos) << r.out;
    EXPECT_EQ(r.out.find("v2 prompt"), std::string::npos) << r.out;
}

TEST(CliTest, InspectAtCurrentVersionUsesLiveInstall) {
    TmpDir td;
    auto home_path = td.path() / "home";
    ScopedHome home(home_path);
    write_file(td.path() / "foo.spud", "ask q \"q?\" string default \"x\"\n");
    ASSERT_EQ(run_cli({"spudplate", "install",
                       (td.path() / "foo.spud").string()})
                  .code,
              0);
    auto r = run_cli({"spudplate", "inspect", "foo@1"});
    EXPECT_EQ(r.code, 0) << r.err;
    EXPECT_NE(r.out.find("foo (v1)"), std::string::npos) << r.out;
}

TEST(CliTest, InspectAtUnknownVersionExitsFive) {
    TmpDir td;
    auto home_path = td.path() / "home";
    ScopedHome home(home_path);
    write_file(td.path() / "foo.spud", "ask q \"q?\" string default \"x\"\n");
    ASSERT_EQ(run_cli({"spudplate", "install",
                       (td.path() / "foo.spud").string()})
                  .code,
              0);
    auto r = run_cli({"spudplate", "inspect", "foo@9"});
    EXPECT_EQ(r.code, 5);
    EXPECT_NE(r.err.find("foo@9"), std::string::npos) << r.err;
    EXPECT_NE(r.err.find("not installed"), std::string::npos) << r.err;
}

TEST(CliTest, RunAtArchivedVersionRunsArchivedProgram) {
    TmpDir td;
    auto home_path = td.path() / "home";
    ScopedHome home(home_path);
    write_file(td.path() / "foo.spud", "mkdir \"v1_dir\"\n");
    ASSERT_EQ(run_cli({"spudplate", "install",
                       (td.path() / "foo.spud").string()})
                  .code,
              0);
    write_file(td.path() / "foo.spud", "mkdir \"v2_dir\"\n");
    ASSERT_EQ(run_cli({"spudplate", "install", "--yes",
                       (td.path() / "foo.spud").string()})
                  .code,
              0);
    auto r = run_cli({"spudplate", "run", "foo@1"});
    EXPECT_EQ(r.code, 0) << r.err;
    EXPECT_TRUE(std::filesystem::is_directory(td.path() / "v1_dir"));
    EXPECT_FALSE(std::filesystem::exists(td.path() / "v2_dir"));
}

TEST(CliTest, RunAtCurrentVersionUsesLiveInstall) {
    TmpDir td;
    auto home_path = td.path() / "home";
    ScopedHome home(home_path);
    write_file(td.path() / "foo.spud", "mkdir \"only_dir\"\n");
    ASSERT_EQ(run_cli({"spudplate", "install",
                       (td.path() / "foo.spud").string()})
                  .code,
              0);
    auto r = run_cli({"spudplate", "run", "foo@1"});
    EXPECT_EQ(r.code, 0) << r.err;
    EXPECT_TRUE(std::filesystem::is_directory(td.path() / "only_dir"));
}

TEST(CliTest, RunAtUnknownVersionExitsFive) {
    TmpDir td;
    auto home_path = td.path() / "home";
    ScopedHome home(home_path);
    write_file(td.path() / "foo.spud", "mkdir \"x\"\n");
    ASSERT_EQ(run_cli({"spudplate", "install",
                       (td.path() / "foo.spud").string()})
                  .code,
              0);
    auto r = run_cli({"spudplate", "run", "foo@9"});
    EXPECT_EQ(r.code, 5);
    EXPECT_NE(r.err.find("foo@9"), std::string::npos) << r.err;
    EXPECT_NE(r.err.find("not installed"), std::string::npos) << r.err;
}

TEST(CliTest, ListAfterReinstallSkipsArchive) {
    TmpDir td;
    auto home_path = td.path() / "home";
    ScopedHome home(home_path);
    write_file(td.path() / "foo.spud",
               "ask q \"q v1?\" string default \"x\"\n");
    ASSERT_EQ(run_cli({"spudplate", "install",
                       (td.path() / "foo.spud").string()})
                  .code,
              0);
    write_file(td.path() / "foo.spud",
               "ask q \"q v2?\" string default \"x\"\n");
    ASSERT_EQ(run_cli({"spudplate", "install", "--yes",
                       (td.path() / "foo.spud").string()})
                  .code,
              0);
    auto r = run_cli({"spudplate", "list"});
    EXPECT_EQ(r.code, 0) << r.err;
    EXPECT_EQ(r.out, "foo\n");
}

TEST(CliTest, InspectShowsVersionAndDeps) {
    TmpDir td;
    auto home_path = td.path() / "home";
    ScopedHome home(home_path);
    write_file(td.path() / "child.spud",
               "ask q \"q?\" string default \"x\"\n");
    ASSERT_EQ(run_cli({"spudplate", "install",
                       (td.path() / "child.spud").string()})
                  .code,
              0);
    write_file(td.path() / "parent.spud", "include child\n");
    ASSERT_EQ(run_cli({"spudplate", "install",
                       (td.path() / "parent.spud").string()})
                  .code,
              0);
    auto r = run_cli({"spudplate", "inspect", "parent"});
    EXPECT_EQ(r.code, 0) << r.err;
    EXPECT_NE(r.out.find("parent (v1)"), std::string::npos) << r.out;
    EXPECT_NE(r.out.find("child (v1)"), std::string::npos) << r.out;
    EXPECT_NE(r.out.find("Dependencies:"), std::string::npos) << r.out;
}

TEST(CliTest, RunDriftWarnsWhenInstalledDepNewer) {
    TmpDir td;
    auto home_path = td.path() / "home";
    ScopedHome home(home_path);
    write_file(td.path() / "child.spud",
               "ask q \"q v1?\" string default \"x\"\n");
    ASSERT_EQ(run_cli({"spudplate", "install",
                       (td.path() / "child.spud").string()})
                  .code,
              0);
    write_file(td.path() / "parent.spud",
               "include child\nask done \"done?\" string default \"d\"\n");
    ASSERT_EQ(run_cli({"spudplate", "install",
                       (td.path() / "parent.spud").string()})
                  .code,
              0);
    write_file(td.path() / "child.spud",
               "ask q \"q v2?\" string default \"x\"\n");
    ASSERT_EQ(run_cli({"spudplate", "install", "--yes",
                       (td.path() / "child.spud").string()})
                  .code,
              0);
    // Run parent: drift expected.
    Argv run_args({"spudplate", "run", "--yes", "parent"});
    std::stringstream rout, rerr;
    ScriptedPrompter rprompter({"a", "b"});
    int code = cli_main(run_args.argc(), run_args.argv(), rout, rerr,
                        rprompter);
    EXPECT_EQ(code, 0) << rerr.str();
    EXPECT_NE(rerr.str().find("warning: dep 'child'"), std::string::npos)
        << rerr.str();
    EXPECT_NE(rerr.str().find("bundled at v1, installed v2"),
              std::string::npos)
        << rerr.str();
}
