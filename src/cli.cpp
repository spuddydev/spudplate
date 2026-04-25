#include "spudplate/cli.h"

#include <cerrno>
#include <cstring>
#include <fstream>
#include <sstream>
#include <string>

#include "spudplate/interpreter.h"
#include "spudplate/lexer.h"
#include "spudplate/parser.h"
#include "spudplate/validator.h"

namespace spudplate {

namespace {

void print_usage(std::ostream& err) {
    err << "usage: spudplate run [--dry-run] <file.spud>\n";
}

void print_error(std::ostream& err, const std::string& file, const char* kind,
                 int line, int column, const char* message) {
    err << file << ":" << line << ":" << column << ": " << kind << ": "
        << message << "\n";
}

}  // namespace

int cli_main(int argc, char* argv[], std::ostream& out, std::ostream& err,
             Prompter& prompter) {
    if (argc < 2) {
        print_usage(err);
        return 1;
    }
    std::string subcommand{argv[1]};
    if (subcommand != "run") {
        print_usage(err);
        return 1;
    }

    bool dry_run_mode = false;
    int positional_start = 2;
    if (argc >= 3 && std::string{argv[2]} == "--dry-run") {
        dry_run_mode = true;
        positional_start = 3;
    }
    if (argc - positional_start != 1) {
        print_usage(err);
        return 1;
    }

    std::string file_path{argv[positional_start]};
    std::ifstream in(file_path);
    if (!in) {
        err << file_path << ": cannot open: " << std::strerror(errno) << "\n";
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
        print_error(err, file_path, "parse error", e.line(), e.column(),
                    e.what());
        return 2;
    }

    try {
        validate(program);
    } catch (const SemanticError& e) {
        print_error(err, file_path, "semantic error", e.line(), e.column(),
                    e.what());
        return 3;
    }

    try {
        if (dry_run_mode) {
            dry_run(program, prompter, out, /*ascii_only=*/!locale_is_utf8());
        } else {
            run(program, prompter);
        }
    } catch (const RuntimeError& e) {
        print_error(err, file_path, "runtime error", e.line(), e.column(),
                    e.what());
        return 4;
    }

    return 0;
}

}  // namespace spudplate
