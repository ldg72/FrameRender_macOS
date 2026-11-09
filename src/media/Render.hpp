#pragma once
#include "core/Project.hpp"
#include "media/Process.hpp"
#include <atomic>
#include <thread>

namespace fr {
enum class VideoCodec { H264, H265, ProRes };
enum class RateControl { Quality, Bitrate };
enum class ProResProfile { Proxy, LT, Standard, HQ };
constexpr float minVideoBitrateMbps=.1f,maxVideoBitrateMbps=1000.f;
struct ExportSettings {
    VideoCodec codec=VideoCodec::H264;
    RateControl rateControl=RateControl::Quality;
    float videoBitrateMbps=20;
    ProResProfile proResProfile=ProResProfile::Standard;
};
const char* exportExtension(VideoCodec codec);
std::vector<std::string> audioCommand(const Project& project,double start);
std::vector<std::string> renderCommand(const Project& project,const std::filesystem::path& output,int width,int height,const ExportSettings& settings={});
class Exporter {
    std::thread thread_;
    Process process_;
    std::atomic<bool> running_{false},cancelled_{false};
    std::atomic<float> progress_{0};
    mutable std::mutex mutex_;
    std::string message_;
    bool successful_=false;
public:
    ~Exporter();
    void start(Project project,const std::filesystem::path& output,int width,int height,const ExportSettings& settings={});
    void cancel();
    bool running() const {return running_;}
    float progress() const {return progress_;}
    std::string message() const;
    bool successful() const;
};
}
