#include "spudplate/cli.h"

#include <algorithm>
#include <cctype>
#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <system_error>
#include <vector>

#include "spudplate/interpreter.h"
#include "spudplate/lexer.h"
#include "spudplate/parser.h"
#include "spudplate/validator.h"

namespace spudplate {

namespace {

void print_usage(std::ostream& err) {
    err << "usage: spudplate <command> [args...]\n"
        << "  install [--yes] <file.spud>     validate and store a template;\n"
        << "                                  prompts before overwriting an existing one\n"
        << "  run [--dry-run] [--yes] <name|file.spud>\n"
        << "                                  run an installed template by name,\n"
        << "                                  or run a .spud file directly\n"
        << "  list                            list installed templates\n"
        << "  inspect <name>                  print the source of an installed template\n"
        << "  uninstall <name>                remove an installed template\n";
}

// Resolve the directory templates are installed into. Honours, in order:
//   1. `SPUDPLATE_HOME` — explicit override (used by tests and dev setups).
//   2. `XDG_DATA_HOME/spudplate` — the XDG Base Directory standard.
//   3. `$HOME/.local/share/spudplate` — XDG default when the env var is unset.
// Returns an empty path if none of those sources is available so the caller
// can surface a usable error.
std::filesystem::path install_dir() {
    if (const char* env = std::getenv("SPUDPLATE_HOME"); env != nullptr && *env != '\0') {
        return env;
    }
    if (const char* xdg = std::getenv("XDG_DATA_HOME"); xdg != nullptr && *xdg != '\0') {
        return std::filesystem::path(xdg) / "spudplate";
    }
    if (const char* home = std::getenv("HOME"); home != nullptr && *home != '\0') {
        return std::filesystem::path(home) / ".local" / "share" / "spudplate";
    }
    return {};
}

// Heuristic: an argument that contains a slash or ends with `.spud` is
// treated as a path. Anything else is an installed-template name.
bool looks_like_path(const std::string& arg) {
    if (arg.find('/') != std::string::npos) {
        return true;
    }
    constexpr std::string_view ext = ".spud";
    return arg.size() >= ext.size() &&
           arg.compare(arg.size() - ext.size(), ext.size(), ext) == 0;
}

void print_error(std::ostream& err, const std::string& file, const char* kind,
                 int line, int column, const char* message) {
    err << file << ":" << line << ":" << column << ": " << kind << ": "
        << message << "\n";
}

// Resolve a `run`/`inspect`/`uninstall` argument to a `.spud` file path on
// disk. If the argument looks like a path it is returned as-is; otherwise
// it is treated as an installed template name. Returns the empty path and
// writes a diagnostic to `err` if the install dir cannot be located. The
// caller is responsible for checking whether the returned path exists.
std::filesystem::path resolve_template_arg(const std::string& arg,
                                           std::ostream& err) {
    if (looks_like_path(arg)) {
        return arg;
    }
    auto dir = install_dir();
    if (dir.empty()) {
        err << "cannot determine install directory: set SPUDPLATE_HOME, "
           "XDG_DATA_HOME, or HOME\n";
        return {};
    }
    return dir / arg / "template.spud";
}

// Parse and validate `source` so install rejects broken templates before
// they are copied. Returns true on success; on failure writes a `<file>:`
// diagnostic to `err` and sets `exit_code` to the appropriate error.
bool validate_template_source(const std::string& source,
                              const std::string& file_label,
                              std::ostream& err, int& exit_code) {
    try {
        Lexer lexer(source);
        Parser parser(std::move(lexer));
        Program program = parser.parse();
        validate(program);
    } catch (const ParseError& e) {
        print_error(err, file_label, "parse error", e.line(), e.column(),
                    e.what());
        exit_code = 2;
        return false;
    } catch (const SemanticError& e) {
        print_error(err, file_label, "semantic error", e.line(), e.column(),
                    e.what());
        exit_code = 3;
        return false;
    }
    return true;
}

// Read a single line from stdin and return true if it parses as
// affirmative (`y`, `yes`, case-insensitive). Empty input returns false so
// the prompt's `[y/N]` default is "no".
bool confirm_yes_no(std::ostream& out, const std::string& prompt) {
    out << prompt << std::flush;
    std::string line;
    if (!std::getline(std::cin, line)) {
        return false;
    }
    std::string lc;
    lc.reserve(line.size());
    for (char c : line) {
        lc.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
    }
    return lc == "y" || lc == "yes";
}

int cmd_install(int argc, char* argv[], std::ostream& out, std::ostream& err) {
    bool skip_confirm = false;
    int positional_start = 2;
    while (positional_start < argc) {
        std::string arg{argv[positional_start]};
        if (arg == "--yes" || arg == "-y") {
            skip_confirm = true;
            ++positional_start;
        } else {
            break;
        }
    }
    if (argc - positional_start != 1) {
        print_usage(err);
        return 1;
    }

    std::filesystem::path source_path{argv[positional_start]};
    std::ifstream in(source_path);
    if (!in) {
        err << source_path.string() << ": cannot open: " << std::strerror(errno)
            << "\n";
        return 5;
    }
    std::stringstream buffer;
    buffer << in.rdbuf();
    std::string source = buffer.str();

    int exit_code = 0;
    if (!validate_template_source(source, source_path.string(), err,
                                  exit_code)) {
        return exit_code;
    }

    std::filesystem::path home = install_dir();
    if (home.empty()) {
        err << "cannot determine install directory: set SPUDPLATE_HOME, "
           "XDG_DATA_HOME, or HOME\n";
        return 1;
    }

    std::string name = source_path.stem().string();
    if (name.empty()) {
        err << source_path.string() << ": cannot derive template name\n";
        return 1;
    }

    std::filesystem::path target_dir = home / name;
    bool overwriting = std::filesystem::exists(target_dir);
    if (overwriting && !skip_confirm) {
        if (!confirm_yes_no(
                out, "this will overwrite the existing '" + name +
                         "' template. continue? [y/N] ")) {
            out << "aborted\n";
            return 0;
        }
    }
    if (overwriting) {
        std::error_code rm_ec;
        std::filesystem::remove_all(target_dir, rm_ec);
        if (rm_ec) {
            err << target_dir.string()
                << ": cannot remove existing install: " << rm_ec.message()
                << "\n";
            return 1;
        }
    }

    std::error_code ec;
    std::filesystem::create_directories(target_dir, ec);
    if (ec) {
        err << target_dir.string()
            << ": cannot create install directory: " << ec.message() << "\n";
        return 1;
    }
    std::filesystem::path target_file = target_dir / "template.spud";
    std::ofstream sink(target_file, std::ios::binary);
    if (!sink) {
        err << target_file.string()
            << ": cannot write: " << std::strerror(errno) << "\n";
        std::filesystem::remove_all(target_dir, ec);
        return 1;
    }
    sink << source;
    if (!sink.good()) {
        err << target_file.string() << ": write failed\n";
        std::filesystem::remove_all(target_dir, ec);
        return 1;
    }

    out << (overwriting ? "reinstalled " : "installed ") << name << " to "
        << target_dir.string() << "\n";
    return 0;
}

int cmd_list(int argc, char* argv[], std::ostream& out, std::ostream& err) {
    if (argc != 2) {
        print_usage(err);
        return 1;
    }
    std::filesystem::path home = install_dir();
    if (home.empty()) {
        err << "cannot determine install directory: set SPUDPLATE_HOME, "
           "XDG_DATA_HOME, or HOME\n";
        return 1;
    }
    if (!std::filesystem::is_directory(home)) {
        return 0;  // No installs yet — empty output, success.
    }
    std::vector<std::string> names;
    std::error_code ec;
    for (const auto& entry : std::filesystem::directory_iterator(home, ec)) {
        if (entry.is_directory() &&
            std::filesystem::is_regular_file(entry.path() / "template.spud")) {
            names.push_back(entry.path().filename().string());
        }
    }
    std::sort(names.begin(), names.end());
    for (const auto& n : names) {
        out << n << "\n";
    }
    return 0;
}

int cmd_inspect(int argc, char* argv[], std::ostream& out, std::ostream& err) {
    if (argc - 2 != 1) {
        print_usage(err);
        return 1;
    }
    std::string name{argv[2]};
    std::filesystem::path home = install_dir();
    if (home.empty()) {
        err << "cannot determine install directory: set SPUDPLATE_HOME, "
           "XDG_DATA_HOME, or HOME\n";
        return 1;
    }
    std::filesystem::path file_path = home / name / "template.spud";
    std::ifstream in(file_path);
    if (!in) {
        err << name << ": not installed\n";
        return 5;
    }
    out << in.rdbuf();
    return 0;
}

int cmd_uninstall(int argc, char* argv[], std::ostream& out, std::ostream& err) {
    if (argc - 2 != 1) {
        print_usage(err);
        return 1;
    }
    std::string name{argv[2]};
    std::filesystem::path home = install_dir();
    if (home.empty()) {
        err << "cannot determine install directory: set SPUDPLATE_HOME, "
           "XDG_DATA_HOME, or HOME\n";
        return 1;
    }
    std::filesystem::path target = home / name;
    if (!std::filesystem::is_directory(target)) {
        err << name << ": not installed\n";
        return 5;
    }
    std::error_code ec;
    std::filesystem::remove_all(target, ec);
    if (ec) {
        err << name << ": cannot remove: " << ec.message() << "\n";
        return 1;
    }
    out << "uninstalled " << name << "\n";
    return 0;
}

int cmd_run(int argc, char* argv[], std::ostream& out, std::ostream& err,
            Prompter& prompter) {
    bool dry_run_mode = false;
    bool skip_authorization = false;
    int positional_start = 2;
    while (positional_start < argc) {
        std::string arg{argv[positional_start]};
        if (arg == "--dry-run") {
            dry_run_mode = true;
            ++positional_start;
        } else if (arg == "--yes" || arg == "-y") {
            skip_authorization = true;
            ++positional_start;
        } else {
            break;
        }
    }
    if (argc - positional_start != 1) {
        print_usage(err);
        return 1;
    }

    std::string raw_arg{argv[positional_start]};
    std::filesystem::path file_path = resolve_template_arg(raw_arg, err);
    if (file_path.empty()) {
        return 1;
    }
    std::ifstream in(file_path);
    if (!in) {
        err << file_path.string() << ": cannot open: " << std::strerror(errno)
            << "\n";
        return 5;
    }
    std::stringstream buffer;
    buffer << in.rdbuf();
    std::string source = buffer.str();

    Program program;
    try {
        Lexer lexer(source);
        Parser parser(std::move(lexer));
        program = parser.parse();
    } catch (const ParseError& e) {
        print_error(err, file_path.string(), "parse error", e.line(),
                    e.column(), e.what());
        return 2;
    }

    try {
        validate(program);
    } catch (const SemanticError& e) {
        print_error(err, file_path.string(), "semantic error", e.line(),
                    e.column(), e.what());
        return 3;
    }

    try {
        if (dry_run_mode) {
            dry_run(program, prompter, out, /*ascii_only=*/!locale_is_utf8());
        } else {
            run(program, prompter, skip_authorization);
        }
    } catch (const RuntimeError& e) {
        print_error(err, file_path.string(), "runtime error", e.line(),
                    e.column(), e.what());
        return 4;
    }

    return 0;
}

}  // namespace

int cli_main(int argc, char* argv[], std::ostream& out, std::ostream& err,
             Prompter& prompter) {
    if (argc < 2) {
        print_usage(err);
        return 1;
    }
    std::string subcommand{argv[1]};
    if (subcommand == "run") {
        return cmd_run(argc, argv, out, err, prompter);
    }
    if (subcommand == "install") {
        return cmd_install(argc, argv, out, err);
    }
    if (subcommand == "list") {
        return cmd_list(argc, argv, out, err);
    }
    if (subcommand == "inspect") {
        return cmd_inspect(argc, argv, out, err);
    }
    if (subcommand == "uninstall") {
        return cmd_uninstall(argc, argv, out, err);
    }
    print_usage(err);
    return 1;
}

}  // namespace spudplate
