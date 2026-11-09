#pragma once
#include "core/Project.hpp"
#include "media/Process.hpp"
#include <SDL3/SDL.h>
#include <atomic>
#include <thread>

namespace fr {
class Transport {
    SDL_AudioStream* stream_=nullptr;
    Process process_;
    std::thread thread_;
    std::atomic<bool> stop_{false},playing_{false},ended_{false};
    std::atomic<std::uint64_t> queuedFrames_{0};
    std::atomic<float> left_{0},right_{0};
    double start_=0,end_=0;
    mutable std::mutex mutex_;
    std::string error_;
public:
    Transport();
    ~Transport();
    void play(const Project& project,double position);
    void stop();
    bool playing() const {return playing_;}
    bool ended() const {return ended_;}
    double position() const;
    float left() const{return left_;}
    float right() const{return right_;}
    std::string error() const;
};
}
