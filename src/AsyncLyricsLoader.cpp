#include "AsyncLyricsLoader.hpp"

#include "Diagnostics.hpp"

AsyncLyricsLoader::AsyncLyricsLoader() : worker_([this](std::stop_token stopToken) { Run(stopToken); }) {}

AsyncLyricsLoader::~AsyncLyricsLoader() {
    worker_.request_stop();
    wake_.notify_all();
}

void AsyncLyricsLoader::Request(std::filesystem::path path) {
    {
        std::scoped_lock lock{mutex_};
        pending_ = std::move(path);
        completed_.reset();
        ++generation_;
    }
    wake_.notify_one();
}

std::optional<AsyncLyricsLoader::Completed> AsyncLyricsLoader::Poll() {
    std::scoped_lock lock{mutex_};
    auto result = std::move(completed_);
    completed_.reset();
    return result;
}

void AsyncLyricsLoader::Run(std::stop_token stopToken) {
    while (!stopToken.stop_requested()) {
        std::filesystem::path path;
        std::uint64_t generation = 0;
        {
            std::unique_lock lock{mutex_};
            wake_.wait(lock, stopToken, [this] { return pending_.has_value(); });
            if (stopToken.stop_requested()) break;
            path = std::move(*pending_);
            pending_.reset();
            generation = generation_;
        }
        auto loaded = LoadLyricsForTrack(path);
        {
            std::scoped_lock lock{mutex_};
            if (generation == generation_) completed_ = Completed{path, std::move(loaded)};
        }
    }
}
