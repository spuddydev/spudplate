#include <iostream>

int main(int argc, char* argv[]) {
    if (argc < 2) {
        std::cerr << "Usage: spudplate <file.spud>" << '\n';
        return 1;
    }

    std::cout << "spudplate: " << argv[1] << '\n';
    return 0;
}
