#include "AsyncLyricsLoader.hpp"

#include "Diagnostics.hpp"

#include <exception>
#include <string>

AsyncLyricsLoader::AsyncLyricsLoader() : worker_([this] { Run(); }) {}

AsyncLyricsLoader::~AsyncLyricsLoader() {
    {
        std::scoped_lock lock{mutex_};
        stopping_ = true;
    }
    wake_.notify_all();
    if (worker_.joinable()) worker_.join();
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

void AsyncLyricsLoader::Run() {
    while (true) {
        std::filesystem::path path;
        std::uint64_t generation = 0;
        {
            std::unique_lock lock{mutex_};
            wake_.wait(lock, [this] { return stopping_ || pending_.has_value(); });
            if (stopping_) break;
            path = std::move(*pending_);
            pending_.reset();
            generation = generation_;
        }
        // An exception escaping a std::thread calls std::terminate and would kill VirtualDJ
        // (corrupt tag, huge file, I/O error ...). Turn any failure into an empty result.
        LyricsLoadResult loaded;
        try {
            loaded = LoadLyricsForTrack(path);
        } catch (const std::exception& error) {
            loaded.error = L"Lyrics loader failed: " +
                std::wstring(error.what(), error.what() + std::char_traits<char>::length(error.what()));
        } catch (...) {
            loaded.error = L"Lyrics loader failed with an unknown error";
        }
        {
            std::scoped_lock lock{mutex_};
            if (generation == generation_) completed_ = Completed{path, std::move(loaded)};
        }
    }
}
