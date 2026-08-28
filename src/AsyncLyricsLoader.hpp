#pragma once

#include "Lyrics.hpp"

#include <condition_variable>
#include <cstdint>
#include <filesystem>
#include <mutex>
#include <optional>
#include <stop_token>
#include <thread>

class AsyncLyricsLoader {
public:
    struct Completed {
        std::filesystem::path path;
        LyricsLoadResult result;
    };

    AsyncLyricsLoader();
    ~AsyncLyricsLoader();
    AsyncLyricsLoader(const AsyncLyricsLoader&) = delete;
    AsyncLyricsLoader& operator=(const AsyncLyricsLoader&) = delete;

    void Request(std::filesystem::path path);
    std::optional<Completed> Poll();

private:
    void Run(std::stop_token stopToken);

    std::mutex mutex_;
    std::condition_variable_any wake_;
    std::optional<std::filesystem::path> pending_;
    std::optional<Completed> completed_;
    std::uint64_t generation_{};
    std::jthread worker_;
};
