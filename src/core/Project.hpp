#pragma once
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace fr {
using Id = std::uint64_t;
struct Media {
    Id id = 0;
    std::filesystem::path path;
    std::string name;
    double duration = 0;
    double fps = 25;
    int width = 0, height = 0, rotation = 0;
    bool hasAudio = false;
};
struct Grade { float brightness = 0, contrast = 1, saturation = 1; };
struct Clip {
    Id id = 0, media = 0;
    double start = 0, in = 0, duration = 0;
    int track = 0;
    float gain = 1;
    bool muted = false;
    Grade grade;
    double fadeIn = 0, fadeOut = 0;
    double end() const { return start + duration; }
};
struct Project {
    std::string name = "Untitled project";
    int width = 1920, height = 1080;
    double fps = 25;
    Id nextId = 1;
    std::vector<Media> media;
    std::vector<Clip> clips;

    const Media* asset(Id id) const;
    Media* asset(Id id);
    Clip* clip(Id id);
    const Clip* clip(Id id) const;
    double duration() const;
    const Clip* visibleAt(double time) const;
    bool canPlace(int track, double start, double duration, Id except = 0) const;
    Id addMedia(Media source);
    Id append(Id mediaId, int track = 0);
    bool split(Id clipId, double at);
    bool move(Id clipId, double start, int track);
    bool trim(Id clipId, double sourceIn, double length);
    bool erase(Id clipId, bool ripple = false);
    bool setFades(Id clipId, double fadeIn, double fadeOut);
    bool crossDissolve(Id clipId);
    void validate() const;
    void save(const std::filesystem::path& path) const;
    static Project load(const std::filesystem::path& path);
};
class History {
    std::vector<Project> undo_, redo_;
public:
    void checkpoint(const Project& project);
    bool undo(Project& project);
    bool redo(Project& project);
    bool canUndo() const { return !undo_.empty(); }
    bool canRedo() const { return !redo_.empty(); }
    void clear() { undo_.clear(); redo_.clear(); }
};
std::string timecode(double seconds, double fps = 25);
double snapFrame(double seconds, double fps);
double clipOpacity(const Clip& clip, double timelineTime);
void fitFades(Clip& clip);
}
