#pragma once
#include <filesystem>
#include <mutex>
#include <string>
#include <vector>

namespace fr {
std::string ffmpegPath();
std::filesystem::path temporaryPath(const std::string& extension);
std::string readText(const std::filesystem::path& path, size_t limit=8192);
class Process {
    mutable std::mutex mutex_;
    int pid_=0, output_=-1, exit_=-1;
public:
    ~Process();
    void start(const std::vector<std::string>& args,const std::filesystem::path& errors);
    long read(void* buffer,size_t bytes);
    int wait();
    void cancel();
};
}
