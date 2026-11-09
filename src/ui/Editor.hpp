#pragma once
#include <SDL3/SDL.h>
#include <filesystem>
#include <memory>

namespace fr {
class Editor {
    struct Impl;
    std::unique_ptr<Impl> impl_;
public:
    Editor(SDL_Window* window,SDL_Renderer* renderer);
    ~Editor();
    void frame();
    void dropFile(const std::filesystem::path& path);
    void loadProject(const std::filesystem::path& path);
    bool requestQuit();
    void setPage(int page);
};
}
