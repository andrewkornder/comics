#include "app.h"

#include <argparse/argparse.hpp>

#include <filesystem>


int main(int argc, char** argv) {
    argparse::ArgumentParser parser(argv[0]);
    parser.add_argument("root")
        .help("where are the generated resource files located")
        .default_value(std::string("res"));

    try {
        parser.parse_args(argc, argv);
    } catch (const std::exception& err) {
        std::cerr << err.what() << std::endl;
        std::cerr << parser;
        return 1;
    }
    
    std::filesystem::path path = parser.get<std::string>("root");
    AppRoot app(canonical(path).string());
    
    if (const int ec = app.init()) {
        return ec;
    }

    app.mainloop();
    return 0;
}
