#include "media/Transport.hpp"
#include "media/Render.hpp"
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstring>
#include <stdexcept>

namespace fr {
Transport::Transport() {
    SDL_AudioSpec spec{};spec.format=SDL_AUDIO_F32;spec.channels=2;spec.freq=48000;
    stream_=SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK,&spec,nullptr,nullptr);
    if(!stream_)error_=SDL_GetError();
}
Transport::~Transport(){stop();if(stream_)SDL_DestroyAudioStream(stream_);}
std::string Transport::error()const{std::lock_guard<std::mutex> lock(mutex_);return error_;}
double Transport::position()const{
    if(ended_)return end_;
    const auto pending=stream_?static_cast<std::uint64_t>(std::max(0,SDL_GetAudioStreamQueued(stream_)))/8:0;
    const auto total=queuedFrames_.load();
    return std::min(end_,start_+static_cast<double>(total>pending?total-pending:0)/48000);
}
void Transport::stop(){
    stop_=true;process_.cancel();if(thread_.joinable())thread_.join();process_.wait();playing_=false;
    if(stream_){SDL_PauseAudioStreamDevice(stream_);SDL_ClearAudioStream(stream_);}
    left_=right_=0;
}
void Transport::play(const Project& project,double position){
    stop();
    if(!stream_)throw std::runtime_error("Cannot open the audio output: "+error());
    auto args=audioCommand(project,position);
    start_=position;end_=project.duration();queuedFrames_=0;stop_=false;ended_=false;playing_=true;
    {std::lock_guard<std::mutex> lock(mutex_);error_.clear();}
    thread_=std::thread([this,args=std::move(args)]{
        const auto errors=temporaryPath(".audio.log");
        try{
            process_.start(args,errors);if(stop_)process_.cancel();
            alignas(float) std::array<unsigned char,16384+8> buffer{};size_t carry=0;
            bool resumed=false;
            while(!stop_){
                if(SDL_GetAudioStreamQueued(stream_)>48000*8/6){std::this_thread::sleep_for(std::chrono::milliseconds(5));continue;}
                const long count=process_.read(buffer.data()+carry,16384);
                if(count<=0)break;
                const size_t available=carry+static_cast<size_t>(count),bytes=(available/8)*8;
                const auto* samples=reinterpret_cast<const float*>(buffer.data());float l=0,r=0;
                for(size_t i=0;i<bytes/4;i+=2){l=std::max(l,std::abs(samples[i]));r=std::max(r,std::abs(samples[i+1]));}
                left_=l;right_=r;
                if(bytes && !SDL_PutAudioStreamData(stream_,buffer.data(),static_cast<int>(bytes)))throw std::runtime_error(SDL_GetError());
                queuedFrames_.fetch_add(bytes/8);
                if(bytes && !resumed){SDL_ResumeAudioStreamDevice(stream_);resumed=true;}
                carry=available-bytes;if(carry)std::memmove(buffer.data(),buffer.data()+bytes,carry);
            }
            if(stop_)process_.cancel();
            const int code=process_.wait();
            if(!stop_ && code)throw std::runtime_error("Audio playback failed:\n"+readText(errors));
            while(!stop_ && SDL_GetAudioStreamQueued(stream_)>0)std::this_thread::sleep_for(std::chrono::milliseconds(5));
            if(!stop_)ended_=true;
        }catch(const std::exception& error){std::lock_guard<std::mutex> lock(mutex_);error_=error.what();}
        std::error_code ec;std::filesystem::remove(errors,ec);playing_=false;left_=right_=0;
    });
}
}
