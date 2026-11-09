#include "core/Project.hpp"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <iomanip>
#include <set>
#include <sstream>
#include <stdexcept>
#include <chrono>

namespace fr {
const Media* Project::asset(Id id) const { for (const auto& a : media) if(a.id == id) return &a; return nullptr; }
Media* Project::asset(Id id) { for(auto& a : media) if(a.id == id) return &a; return nullptr; }
Clip* Project::clip(Id id) { for(auto& c : clips) if(c.id == id) return &c; return nullptr; }
const Clip* Project::clip(Id id) const { for(const auto& c : clips) if(c.id == id) return &c; return nullptr; }
double Project::duration() const { double result = 0; for(const auto& c : clips) result = std::max(result, c.end()); return result; }
double snapFrame(double t, double fps) { return std::round(t * fps) / fps; }
double clipOpacity(const Clip& c,double time){
    if(time<c.start || time>=c.end())return 0;
    double alpha=1;
    if(c.fadeIn>0)alpha*=std::clamp((time-c.start)/c.fadeIn,0.0,1.0);
    if(c.fadeOut>0)alpha*=std::clamp((c.end()-time)/c.fadeOut,0.0,1.0);
    return alpha;
}
void fitFades(Clip& c){
    const double sum=c.fadeIn+c.fadeOut;
    if(sum>c.duration){const double scale=c.duration/sum;c.fadeIn*=scale;c.fadeOut*=scale;}
}
const Clip* Project::visibleAt(double time) const {
    const Clip* result = nullptr;
    for(const auto& c : clips) if(time >= c.start && time < c.end() && (!result || c.track > result->track)) result = &c;
    return result;
}
bool Project::canPlace(int track, double start, double length, Id except) const {
    if(track < 0 || track > 1 || !std::isfinite(start) || !std::isfinite(length) || start < 0 || length <= 0) return false;
    for(const auto& c : clips) if(c.id != except && c.track == track && start < c.end()-1e-7 && start+length > c.start+1e-7) return false;
    return true;
}
Id Project::addMedia(Media source) {
    for(const auto& a : media) if(a.path == source.path) return a.id;
    source.id = nextId++;
    media.push_back(std::move(source));
    return media.back().id;
}
Id Project::append(Id mediaId, int track) {
    const auto* a = asset(mediaId);
    if(!a || track < 0 || track > 1) return 0;
    double start = 0;
    for(const auto& c : clips) if(c.track == track) start = std::max(start, c.end());
    const double length = std::floor(a->duration * fps + 1e-6) / fps;
    if(length < 1/fps) return 0;
    Clip c; c.id = nextId++; c.media = mediaId; c.start = start; c.duration = length; c.track = track;
    clips.push_back(c);
    return c.id;
}
bool Project::split(Id id, double at) {
    if(!std::isfinite(at))return false;
    auto* c = clip(id);
    at = snapFrame(at, fps);
    if(!c || at-c->start < 1/fps-1e-6 || c->end()-at < 1/fps-1e-6) return false;
    Clip right = *c;
    const double leftLength = at - c->start;
    right.id = nextId++; right.start = at; right.in += leftLength; right.duration -= leftLength;
    c->duration = leftLength;
    c->fadeOut=0;right.fadeIn=0;fitFades(*c);fitFades(right);
    clips.push_back(right);
    return true;
}
bool Project::move(Id id, double start, int track) {
    if(!std::isfinite(start))return false;
    auto* c = clip(id);
    start = snapFrame(std::max(0.0, start), fps);
    if(!c || !canPlace(track, start, c->duration, id)) return false;
    c->start = start; c->track = track; return true;
}
bool Project::trim(Id id, double sourceIn, double length) {
    if(!std::isfinite(sourceIn) || !std::isfinite(length))return false;
    auto* c = clip(id);
    if(!c) return false;
    const auto* a = asset(c->media);
    sourceIn = snapFrame(sourceIn, fps); length = snapFrame(length, fps);
    if(!a || sourceIn < 0 || length < 1/fps-1e-6 || sourceIn + length > a->duration+1e-6 || !canPlace(c->track,c->start,length,id)) return false;
    c->in = sourceIn; c->duration = length;fitFades(*c);return true;
}
bool Project::erase(Id id, bool ripple) {
    const auto* c = clip(id);
    if(!c) return false;
    const Clip removed = *c;
    clips.erase(std::remove_if(clips.begin(),clips.end(),[id](const Clip& v){return v.id == id;}),clips.end());
    if(ripple) for(auto& other : clips) if(other.track == removed.track && other.start >= removed.end()-1e-6) other.start -= removed.duration;
    return true;
}
bool Project::setFades(Id id,double in,double out){
    auto* c=clip(id);
    if(!c || !std::isfinite(in) || !std::isfinite(out) || in<0 || out<0)return false;
    c->fadeIn=std::min(snapFrame(in,fps),c->duration);
    c->fadeOut=std::min(snapFrame(out,fps),c->duration);
    fitFades(*c);return true;
}
bool Project::crossDissolve(Id id){
    const auto* selected=clip(id);if(!selected)return false;
    const Clip* before=nullptr;const Clip* after=nullptr;
    for(const auto& other:clips){
        if(other.track==selected->track)continue;
        if(other.start<selected->start && other.end()>selected->start && other.end()<selected->end())before=&other;
        if(other.start>selected->start && other.start<selected->end() && other.end()>selected->end())after=&other;
    }
    const auto* a=before?before:selected;const auto* b=before?selected:after;
    if(!b)return false;
    auto* upper=clip(a->track>b->track?a->id:b->id);
    const double overlap=a->end()-b->start;
    if(upper==b){upper->fadeIn=overlap;upper->fadeOut=std::min(upper->fadeOut,std::max(0.0,upper->duration-overlap));}
    else{upper->fadeOut=overlap;upper->fadeIn=std::min(upper->fadeIn,std::max(0.0,upper->duration-overlap));}
    return true;
}
void Project::validate() const {
    auto require = [](bool ok){if(!ok) throw std::runtime_error("Invalid or unsupported FrameRender project.");};
    require(width >= 16 && width <= 8192 && height >= 16 && height <= 8192 && width % 2 == 0 && height % 2 == 0);
    require(std::isfinite(fps) && fps >= 1 && fps <= 120 && media.size() <= 10000 && clips.size() <= 100000);
    std::set<Id> ids; Id maxId = 0;
    for(const auto& a : media) {
        require(a.id && ids.insert(a.id).second && !a.path.empty() && std::isfinite(a.duration) && a.duration > 0 && a.duration <= 86400*7);
        require(a.width > 0 && a.height > 0 && a.width <= 32768 && a.height <= 32768 && std::isfinite(a.fps) && a.fps > 0 && a.fps <= 1000);
        require(a.rotation == 0 || a.rotation == 90 || a.rotation == 180 || a.rotation == 270);
        maxId = std::max(maxId,a.id);
    }
    std::vector<const Clip*> ordered[2];
    for(const auto& c : clips) {
        const auto* a = asset(c.media);
        require(c.id && ids.insert(c.id).second && a && std::isfinite(c.in) && c.in >= 0 && c.in+c.duration <= a->duration+1e-5);
        require(c.track>=0 && c.track<=1 && std::isfinite(c.start) && c.start>=0 && std::isfinite(c.duration) && c.duration>0 && c.end() <= 86400*7);
        require(std::isfinite(c.fadeIn) && std::isfinite(c.fadeOut) && c.fadeIn>=0 && c.fadeOut>=0 && c.fadeIn+c.fadeOut<=c.duration+1e-7);
        require(std::isfinite(c.gain) && c.gain >= 0 && c.gain <= 4);
        require(std::isfinite(c.grade.brightness) && std::abs(c.grade.brightness) <= 1 && std::isfinite(c.grade.contrast) && c.grade.contrast >= 0 && c.grade.contrast <= 3 && std::isfinite(c.grade.saturation) && c.grade.saturation >= 0 && c.grade.saturation <= 3);
        maxId = std::max(maxId,c.id);
        ordered[c.track].push_back(&c);
    }
    for(auto& track:ordered){
        std::sort(track.begin(),track.end(),[](const Clip* a,const Clip* b){return a->start<b->start;});
        double end=0;for(const auto* c:track){require(c->start>=end-1e-7);end=c->end();}
    }
    require(nextId > maxId);
}
void Project::save(const std::filesystem::path& path) const {
    validate();
    const auto temp = path.string()+".tmp-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
    try {
        std::ofstream out(temp,std::ios::binary);
        out << std::setprecision(17) << "FRAME_RENDER 2\n" << std::quoted(name) << '\n' << width << ' ' << height << ' ' << fps << ' ' << nextId << '\n' << media.size() << '\n';
        for(const auto& a : media) {
            std::error_code ec;
            auto relative = std::filesystem::relative(a.path, std::filesystem::absolute(path).parent_path(),ec);
            const auto stored = !ec && !relative.empty() ? relative : a.path;
            out << a.id << ' ' << std::quoted(stored.string()) << ' ' << std::quoted(a.name) << ' ' << a.duration << ' ' << a.fps << ' ' << a.width << ' ' << a.height << ' ' << a.rotation << ' ' << a.hasAudio << '\n';
        }
        out << clips.size() << '\n';
        for(const auto& c : clips) out << c.id << ' ' << c.media << ' ' << c.start << ' ' << c.in << ' ' << c.duration << ' ' << c.track << ' ' << c.gain << ' ' << c.muted << ' ' << c.grade.brightness << ' ' << c.grade.contrast << ' ' << c.grade.saturation << ' ' << c.fadeIn << ' ' << c.fadeOut << '\n';
        out.flush();
        if(!out) throw std::runtime_error("Could not save project. Check disk space and folder permissions.");
        out.close();
        if(!out)throw std::runtime_error("Could not finish writing the project.");
        std::filesystem::rename(temp,path);
    } catch(...) { std::error_code ec; std::filesystem::remove(temp,ec); throw; }
}
Project Project::load(const std::filesystem::path& path) {
    if(std::filesystem::file_size(path) > 32*1024*1024) throw std::runtime_error("Project exceeds the 32 MB limit.");
    std::ifstream in(path,std::ios::binary);
    std::string magic; int version = 0;
    if(!(in >> magic >> version) || magic != "FRAME_RENDER" || (version != 1 && version != 2)) throw std::runtime_error("This is not a supported FrameRender project.");
    Project p; size_t count = 0;
    if(!(in >> std::quoted(p.name) >> p.width >> p.height >> p.fps >> p.nextId >> count) || count > 10000) throw std::runtime_error("Invalid project header.");
    for(size_t i=0;i<count;++i) {
        Media a; std::string source;
        if(!(in >> a.id >> std::quoted(source) >> std::quoted(a.name) >> a.duration >> a.fps >> a.width >> a.height >> a.rotation >> a.hasAudio)) throw std::runtime_error("Invalid media entry.");
        a.path = std::filesystem::path(source);
        if(a.path.is_relative()) a.path = std::filesystem::absolute(path).parent_path()/a.path;
        a.path = a.path.lexically_normal();
        p.media.push_back(a);
    }
    if(!(in >> count) || count > 100000) throw std::runtime_error("Invalid timeline.");
    for(size_t i=0;i<count;++i) {
        Clip c;
        if(!(in >> c.id >> c.media >> c.start >> c.in >> c.duration >> c.track >> c.gain >> c.muted >> c.grade.brightness >> c.grade.contrast >> c.grade.saturation)) throw std::runtime_error("Invalid clip entry.");
        if(version>=2 && !(in >> c.fadeIn >> c.fadeOut))throw std::runtime_error("Invalid clip fades.");
        p.clips.push_back(c);
    }
    in >> std::ws;
    if(!in.eof()) throw std::runtime_error("Unexpected data at the end of the project.");
    p.validate(); return p;
}
void History::checkpoint(const Project& p) { undo_.push_back(p); if(undo_.size()>100) undo_.erase(undo_.begin()); redo_.clear(); }
bool History::undo(Project& p) { if(undo_.empty()) return false; redo_.push_back(p); p=std::move(undo_.back()); undo_.pop_back(); return true; }
bool History::redo(Project& p) { if(redo_.empty()) return false; undo_.push_back(p); p=std::move(redo_.back()); redo_.pop_back(); return true; }
std::string timecode(double t, double fps) {
    const auto f = static_cast<long long>(std::round(std::max(0.0,t)*fps));
    const int nominal = static_cast<int>(std::round(fps));
    const auto s = f / nominal;
    char buf[48]; std::snprintf(buf,sizeof(buf),"%02lld:%02lld:%02lld:%02lld",s/3600,(s/60)%60,s%60,f%nominal); return buf;
}
}
