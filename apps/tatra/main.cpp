#include <tatra/version.hpp>
#include <iostream>
#include <string_view>

int main(int argc, char** argv) {
    if (argc == 1 || (argc == 2 && std::string_view{argv[1]} == "--help")) {
        std::cout << "TatraPeak " << tatra::version << " (project scaffold)\n"
                  << "Usage: tatra --help | --version\n"
                  << "Planned commands: render (T08-T14), replay (T16), evaluate (T17).\n"
                  << "The mountain engine is not implemented yet. See docs/MVP_TASKS.md.\n";
        return 0;
    }
    if (argc == 2 && std::string_view{argv[1]} == "--version") {
        std::cout << tatra::version << '\n';
        return 0;
    }
    const std::string_view command{argv[1]};
    if (command == "render" || command == "replay" || command == "evaluate") {
        std::cerr << command << " is not implemented yet; see docs/MVP_TASKS.md.\n";
        return 4;
    }
    std::cerr << "Unknown arguments. Run tatra --help.\n";
    return 2;
}
