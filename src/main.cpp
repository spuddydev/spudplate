#include <iostream>

#include "spudplate/cli.h"
#include "spudplate/interpreter.h"

int main(int argc, char* argv[]) {
    spudplate::StdinPrompter prompter;
    return spudplate::cli_main(argc, argv, std::cout, std::cerr, prompter);
}
