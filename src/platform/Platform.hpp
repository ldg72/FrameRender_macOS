#pragma once
#include <filesystem>
#include <string>
#include <vector>

namespace fr::platform {
void initialize();
std::vector<std::filesystem::path> openVideos();
std::filesystem::path openProject();
std::filesystem::path saveProject(const std::string& name);
std::filesystem::path saveVideo(const std::string& name,const std::string& extension="mp4");
std::filesystem::path applicationSupport();
bool confirmDiscard();
void reveal(const std::filesystem::path& path);
}
