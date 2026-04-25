#ifndef SPUDPLATE_CLI_H
#define SPUDPLATE_CLI_H

#include <ostream>

#include "spudplate/interpreter.h"

namespace spudplate {

/**
 * @brief Command-line entry point factored out of `main` for testability.
 *
 * Recognised invocations:
 *   spudplate run <file.spud>     run a template (exit codes below)
 *
 * Exit codes:
 *   0 — success
 *   1 — unrecognised invocation (usage printed to `err`)
 *   2 — parse error
 *   3 — semantic error
 *   4 — runtime error
 *   5 — file not found or unreadable
 *
 * Errors are formatted as `<file>:<line>:<col>: <kind>: <message>` on `err`.
 */
int cli_main(int argc, char* argv[], std::ostream& out, std::ostream& err,
             Prompter& prompter);

}  // namespace spudplate

#endif  // SPUDPLATE_CLI_H
