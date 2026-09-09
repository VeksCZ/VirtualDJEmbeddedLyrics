#pragma once

#include "Lyrics.hpp"

#include <condition_variable>
#include <cstdint>
#include <filesystem>
#include <mutex>
#include <optional>
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
    void Run();

    std::mutex mutex_;
    std::condition_variable wake_;
    std::optional<std::filesystem::path> pending_;
    std::optional<Completed> completed_;
    std::uint64_t generation_{};
    bool stopping_{};
    std::thread worker_;
};
