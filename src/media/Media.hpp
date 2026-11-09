#pragma once
#include "core/Project.hpp"
#include <condition_variable>
#include <memory>
#include <mutex>
#include <optional>
#include <thread>

namespace fr {
Media probe(const std::filesystem::path& path);
struct VideoFrame {
    int width = 0, height = 0;
    double time = 0;
    std::vector<std::uint8_t> rgba;
};
class VideoReader {
    struct Impl;
    std::unique_ptr<Impl> impl_;
public:
    explicit VideoReader(const std::filesystem::path& path);
    ~VideoReader();
    VideoReader(const VideoReader&) = delete;
    VideoReader& operator=(const VideoReader&) = delete;
    VideoFrame read(double time, int maxWidth = 1280);
};
struct PreviewResult {
    std::filesystem::path path;
    double requestedTime = 0;
    VideoFrame frame;
    std::string error;
    std::uint64_t generation = 0;
};
class Preview {
    struct Request { std::filesystem::path path; double time; std::uint64_t generation; };
    std::mutex mutex_;
    std::condition_variable wake_;
    std::optional<Request> request_;
    std::optional<PreviewResult> result_;
    bool stop_ = false;
    std::thread thread_;
    void run();
public:
    Preview();
    ~Preview();
    void request(const std::filesystem::path& path, double time, std::uint64_t generation);
    std::optional<PreviewResult> take();
};
void applyGrade(VideoFrame& frame, const Grade& grade);
}
