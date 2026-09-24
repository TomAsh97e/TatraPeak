#include <tatra/version.hpp>
#include <iostream>
#include <string_view>

int main(int argc, char** argv) {
    if (argc == 1 || (argc == 2 && std::string_view{argv[1]} == "--help")) {
        std::cout << "TatraPeak data preparation " << tatra::version << " (project scaffold)\n"
                  << "Usage: tatra-prepare --help | --version\n"
                  << "TODO(T11-T13): normalize DEM, import OSM and write a dataset.\n";
        return 0;
    }
    if (argc == 2 && std::string_view{argv[1]} == "--version") {
        std::cout << tatra::version << '\n';
        return 0;
    }
    std::cerr << "Dataset preparation is not implemented yet; see tasks T11-T13.\n";
    return 4;
}
