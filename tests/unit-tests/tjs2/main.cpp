#include <catch2/catch_session.hpp>

#include <spdlog/spdlog.h>
#include <spdlog/sinks/stdout_color_sinks.h>

int main(int argc, char *argv[]) {
    if(!spdlog::get("tjs2"))
        spdlog::stdout_color_mt("tjs2");
    return Catch::Session().run(argc, argv);
}
