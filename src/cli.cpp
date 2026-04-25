#include "spudplate/cli.h"

#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <system_error>

#include "spudplate/interpreter.h"
#include "spudplate/lexer.h"
#include "spudplate/parser.h"
#include "spudplate/validator.h"

namespace spudplate {

namespace {

void print_usage(std::ostream& err) {
    err << "usage: spudplate <command> [args...]\n"
        << "  install <file.spud>             validate and store a template\n"
        << "  run [--dry-run] [--yes] <name|file.spud>\n"
        << "                                  run an installed template by name,\n"
        << "                                  or run a .spud file directly\n"
        << "  list                            list installed templates\n"
        << "  inspect <name>                  print the source of an installed template\n"
        << "  uninstall <name>                remove an installed template\n";
}

// Resolve the directory templates are installed into. Honours
// `SPUDPLATE_HOME` for tests and ad-hoc overrides; otherwise falls back to
// `$HOME/.spudplate`. Returns an empty path if neither is set so the
// caller can surface a usable error.
std::filesystem::path install_dir() {
    if (const char* env = std::getenv("SPUDPLATE_HOME"); env != nullptr && *env != '\0') {
        return env;
    }
    if (const char* home = std::getenv("HOME"); home != nullptr && *home != '\0') {
        return std::filesystem::path(home) / ".spudplate";
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
        err << "cannot determine install directory: set SPUDPLATE_HOME or HOME\n";
        return {};
    }
    return dir / arg / "template.spud";
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
    print_usage(err);
    return 1;
}

}  // namespace spudplate
