#include "app.h"

#include <argparse/argparse.hpp>
#include <vips/vips.h>

#include <filesystem>


int main(int argc, char** argv) {
    vips_init(argv[0]);

    argparse::ArgumentParser parser(argv[0]);
    parser.add_argument("root")
        .help("where are the generated resource files located")
        .default_value(std::string("../res"));

    try {
        parser.parse_args(argc, argv);
    } catch (const std::exception& err) {
        std::cerr << err.what() << std::endl;
        std::cerr << parser;
        return 1;
    }
    
    std::filesystem::path path = parser.get<std::string>("root");
    if (!exists(path)) {
        std::cerr << "root path does not exist: " << path.string() << std::endl;
        return 1;
    }

    AppRoot app(canonical(path).string());
    
    if (const int ec = app.init()) {
        return ec;
    }

    app.mainloop();
    return 0;
}
