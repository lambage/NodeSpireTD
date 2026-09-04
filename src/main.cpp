#include "AppController.hpp"

#include <spdlog/sinks/rotating_file_sink.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/spdlog.h>

#include <algorithm>
#include <iostream>
#include <memory>
#include <string_view>
#include <vector>

namespace {

constexpr std::size_t kMaxLogFileSizeBytes = 5 * 1024 * 1024;
constexpr std::size_t kMaxRotatedLogFiles = 3;

spdlog::level::level_enum parseRequestedLogLevel(int argc, char** argv) {
    spdlog::level::level_enum level = spdlog::level::info;
    for (int i = 1; i < argc; ++i) {
        const std::string_view arg = argv[i];
        if (arg == "--extra-verbose") {
            level = spdlog::level::trace;
        } else if (arg == "-v" || arg == "--verbose") {
            level = std::min(level, spdlog::level::debug);
        }
    }
    return level;
}

void initLogging(spdlog::level::level_enum level) {
    std::vector<spdlog::sink_ptr> sinks;
    sinks.push_back(std::make_shared<spdlog::sinks::stdout_color_sink_mt>());

    try {
        sinks.push_back(std::make_shared<spdlog::sinks::rotating_file_sink_mt>(
            "logs/nodespiretd.log", kMaxLogFileSizeBytes, kMaxRotatedLogFiles));
    } catch (const spdlog::spdlog_ex& ex) {
        std::cerr << "Failed to initialize log file sink: " << ex.what() << '\n';
    }

    auto logger = std::make_shared<spdlog::logger>("nodespiretd", sinks.begin(), sinks.end());
    logger->set_level(level);
    logger->flush_on(spdlog::level::warn);
    spdlog::set_default_logger(logger);
    spdlog::set_level(level);
}

} // namespace

int main(int argc, char** argv) {
    initLogging(parseRequestedLogLevel(argc, argv));

    AppController app;
    return app.run();
}
