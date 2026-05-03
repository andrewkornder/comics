#include "app.h"

#include <filesystem>


int main(int argc, char** argv) {
    std::string_view program = argv[0];
    if (const auto i = program.find_last_of("/\\"); i != std::string_view::npos) {
        program = program.substr(0, i);
    } else {
        program = ".";
    }
    AppRoot app(std::filesystem::absolute(program).string());
    
    if (const int ec = app.init()) {
        return ec;
    }

    app.mainloop();
    return 0;
}
