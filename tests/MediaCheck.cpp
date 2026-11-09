#include "core/Project.hpp"
#include "media/Media.hpp"
#include "media/Process.hpp"
#include "media/Render.hpp"
#include "media/Transport.hpp"
#include <chrono>
#include <cmath>
#include <cstring>
#include <iostream>
#include <fstream>
#include <stdexcept>
#include <thread>

int main(int argc,char** argv){
    if(argc!=3){std::cerr<<"Usage: fr_media_check input.mp4 output.mp4\n";return 2;}
    try{
        fr::Project p;auto source=fr::probe(argv[1]);if(!source.hasAudio || source.duration<1.9)throw std::runtime_error("Expected a fixture with video and audio.");
        fr::VideoReader reader(source.path);auto first=reader.read(0);auto later=reader.read(1);auto back=reader.read(.2);auto tail=reader.read(source.duration+.1);
        if(first.rgba.empty() || later.rgba.empty() || back.rgba.empty() || tail.rgba.empty())throw std::runtime_error("Frame decode, reverse seek or end-of-file handling failed.");
        const auto id=p.addMedia(source),clip=p.append(id);p.split(clip,1);p.move(p.clips.back().id,1.5,0);
        p.save(std::filesystem::path(argv[2]).parent_path()/"Demo.frp");
        fr::Process audio;const auto log=fr::temporaryPath(".check.log");audio.start(fr::audioCommand(p,0),log);
        char bytes[8192];std::uint64_t total=0;float tonePeak=0,gapPeak=0;
        while(const long n=audio.read(bytes,sizeof(bytes))){
            if(n<0 || n%8)throw std::runtime_error("Audio pipe read returned incomplete PCM frames.");
            for(long i=0;i<n;i+=8){float value=0;std::memcpy(&value,bytes+i,sizeof(value));const double at=static_cast<double>(total+i)/(48000*8);if(at>.15 && at<.7)tonePeak=std::max(tonePeak,std::abs(value));if(at>1.15 && at<1.35)gapPeak=std::max(gapPeak,std::abs(value));}
            total+=static_cast<std::uint64_t>(n);
        }
        if(audio.wait()!=0)throw std::runtime_error("Audio mixing failed: "+fr::readText(log));
        std::filesystem::remove(log);
        const double audioDuration=static_cast<double>(total)/(48000*8);
        if(std::abs(audioDuration-p.duration())>.02)throw std::runtime_error("Mixed audio duration does not match the timeline.");
        if(tonePeak<.02f || gapPeak>.0001f)throw std::runtime_error("Audio content or silence in the timeline gap is incorrect.");
        fr::Exporter exporter;exporter.start(p,argv[2],640,360);
        const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(60);
        while(exporter.running() && std::chrono::steady_clock::now()<deadline)std::this_thread::sleep_for(std::chrono::milliseconds(25));
        if(exporter.running()){exporter.cancel();throw std::runtime_error("Export timed out.");}
        if(!exporter.successful())throw std::runtime_error(exporter.message());
        auto rendered=fr::probe(argv[2]);
        if(rendered.width!=640 || rendered.height!=360 || !rendered.hasAudio || std::abs(rendered.duration-p.duration())>.1)throw std::runtime_error("Exported metadata does not match the timeline.");
        fr::VideoReader output(argv[2]);auto gap=output.read(1.2);
        long long brightness=0;for(size_t i=0;i<gap.rgba.size();i+=4)brightness+=gap.rgba[i]+gap.rgba[i+1]+gap.rgba[i+2];
        if(brightness>static_cast<long long>(gap.width)*gap.height*3*5)throw std::runtime_error("A timeline gap must render as black.");
        // A failed or cancelled export must preserve an existing destination.
        const auto existing=std::filesystem::path(argv[2]).parent_path()/"Protected.mp4";
        const auto invalid=std::filesystem::path(argv[2]).parent_path()/"Invalid.mp4";
        std::ofstream(existing)<<"keep this file";std::ofstream(invalid)<<"not a video";
        auto broken=p;broken.media.front().path=invalid;exporter.start(broken,existing,640,360);
        while(exporter.running())std::this_thread::sleep_for(std::chrono::milliseconds(10));
        if(exporter.successful() || fr::readText(existing)!="keep this file")throw std::runtime_error("Failed export overwrote an existing destination.");
        exporter.start(p,existing,1920,1080);exporter.cancel();
        while(exporter.running())std::this_thread::sleep_for(std::chrono::milliseconds(10));
        if(exporter.successful() || fr::readText(existing)!="keep this file")throw std::runtime_error("Cancelled export overwrote an existing destination.");
        std::filesystem::remove(existing);std::filesystem::remove(invalid);
        // Exercise the playback transport with a silent test output, never the user's speakers.
        SDL_SetHintWithPriority(SDL_HINT_AUDIO_DRIVER,"dummy",SDL_HINT_OVERRIDE);
        if(!SDL_Init(SDL_INIT_AUDIO))throw std::runtime_error(SDL_GetError());
        {
            fr::Transport transport;transport.play(p,0);
            auto until=std::chrono::steady_clock::now()+std::chrono::seconds(5);
            while(transport.playing() && transport.position()<.15 && std::chrono::steady_clock::now()<until)std::this_thread::sleep_for(std::chrono::milliseconds(10));
            if(transport.position()<.15)throw std::runtime_error("Playback clock did not advance: "+transport.error());
            transport.stop();transport.play(p,p.duration()-.2);
            until=std::chrono::steady_clock::now()+std::chrono::seconds(5);
            while(transport.playing() && std::chrono::steady_clock::now()<until)std::this_thread::sleep_for(std::chrono::milliseconds(10));
            if(!transport.ended() || !transport.error().empty())throw std::runtime_error("Playback seek/restart failed: "+transport.error());
        }
        SDL_Quit();
        std::cout<<"PASS: video probe, forward/reverse seek, PCM mix, split/gap export, protected destination, cancellation, playback clock and restart\n";
    }catch(const std::exception& error){std::cerr<<"FAIL: "<<error.what()<<'\n';return 1;}
}
