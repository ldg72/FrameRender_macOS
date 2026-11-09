#include "media/Media.hpp"
#include "media/Render.hpp"
extern "C" {
#include <libavformat/avformat.h>
#include <libavutil/pixfmt.h>
}
#include <chrono>
#include <cmath>
#include <fstream>
#include <iostream>
#include <limits>
#include <memory>
#include <stdexcept>
#include <thread>

namespace {
void require(bool ok,const std::string& message){if(!ok)throw std::runtime_error(message);}
struct Metadata {
    AVCodecID video=AV_CODEC_ID_NONE,audio=AV_CODEC_ID_NONE;
    int profile=-1,pixelFormat=-1,sampleRate=0,channels=0;
    unsigned tag=0;
    std::int64_t videoBitrate=0;
};
Metadata inspect(const std::filesystem::path& path){
    AVFormatContext* raw=nullptr;
    require(avformat_open_input(&raw,path.c_str(),nullptr,nullptr)>=0,"Cannot open exported file.");
    const auto close=[](AVFormatContext* context){avformat_close_input(&context);};
    std::unique_ptr<AVFormatContext,decltype(close)> context(raw,close);
    require(avformat_find_stream_info(raw,nullptr)>=0,"Cannot inspect exported streams.");Metadata result;
    for(unsigned i=0;i<raw->nb_streams;++i){
        const auto* p=raw->streams[i]->codecpar;
        if(p->codec_type==AVMEDIA_TYPE_VIDEO){result.video=p->codec_id;result.profile=p->profile;result.pixelFormat=p->format;result.tag=p->codec_tag;result.videoBitrate=p->bit_rate;}
        if(p->codec_type==AVMEDIA_TYPE_AUDIO){result.audio=p->codec_id;result.sampleRate=p->sample_rate;result.channels=p->ch_layout.nb_channels;}
    }
    return result;
}
void wait(fr::Exporter& exporter){
    const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(35);
    while(exporter.running() && std::chrono::steady_clock::now()<deadline)std::this_thread::sleep_for(std::chrono::milliseconds(15));
    if(exporter.running()){exporter.cancel();throw std::runtime_error("Export timed out.");}
}
double brightness(const fr::VideoFrame& frame){
    double sum=0;for(size_t i=0;i<frame.rgba.size();i+=4)sum+=frame.rgba[i]+frame.rgba[i+1]+frame.rgba[i+2];
    return sum/(frame.width*frame.height*3);
}
Metadata render(const fr::Project& project,const std::filesystem::path& path,const fr::ExportSettings& settings){
    fr::Exporter exporter;exporter.start(project,path,640,360,settings);wait(exporter);
    require(exporter.successful(),exporter.message());
    const auto media=fr::probe(path);require(media.width==640 && media.height==360 && media.hasAudio && std::abs(media.duration-project.duration())<.1,"Export dimensions, audio or duration differ from the timeline.");
    fr::VideoReader reader(path);require(brightness(reader.read(.4))>20,"Exported picture is missing.");
    require(brightness(reader.read(1.24))<5,"Timeline gap is not black.");
    const auto metadata=inspect(path);require(metadata.channels==2 && metadata.sampleRate==48000,"Audio is not stereo 48 kHz.");
    return metadata;
}
void rejected(const fr::Project& project,const std::filesystem::path& path,const fr::ExportSettings& settings){
    bool threw=false;try{fr::renderCommand(project,path,640,360,settings);}catch(const std::exception&){threw=true;}
    require(threw,"Invalid export settings were accepted.");
}
}
int main(int argc,char** argv){
    if(argc!=3)return 2;
    try{
        const std::filesystem::path folder=argv[2];std::filesystem::create_directories(folder);
        fr::Project project;const auto media=project.addMedia(fr::probe(argv[1])),first=project.append(media);
        require(project.split(first,1) && project.move(project.clips.back().id,1.5,0),"Cannot build test timeline.");
        auto upper=*project.clip(first);upper.id=project.nextId++;upper.start=.2;upper.in=.4;upper.duration=.4;upper.track=1;upper.gain=0;upper.grade.brightness=.1f;project.clips.push_back(upper);
        fr::ExportSettings settings;settings.rateControl=fr::RateControl::Bitrate;settings.videoBitrateMbps=.25f;
        const auto low=render(project,folder/"H264 low.mp4",settings);
        settings.videoBitrateMbps=2;
        const auto high=render(project,folder/"H264 high.mp4",settings);
        require(low.video==AV_CODEC_ID_H264 && high.audio==AV_CODEC_ID_AAC,"H.264 codec or AAC audio mismatch.");
        require(low.videoBitrate>0 && high.videoBitrate>low.videoBitrate*1.5,"Video bitrate setting does not affect the encoded stream.");
        std::cout<<"PASS: H.264 target bitrate changes the encoded stream\n";
        settings.codec=fr::VideoCodec::H265;
        for(const auto mode:{fr::RateControl::Bitrate,fr::RateControl::Quality}){
            settings.rateControl=mode;const auto encoded=render(project,folder/(mode==fr::RateControl::Bitrate?"H265 bitrate.mp4":"H265 quality.mp4"),settings);
            require(encoded.video==AV_CODEC_ID_HEVC && encoded.audio==AV_CODEC_ID_AAC,"HEVC codec or AAC audio mismatch.");
            constexpr unsigned hvc1=unsigned('h')|(unsigned('v')<<8)|(unsigned('c')<<16)|(unsigned('1')<<24);
            require(encoded.tag==hvc1,"HEVC is missing its Apple-compatible sample entry.");
        }
        std::cout<<"PASS: HEVC quality/bitrate modes and hvc1 MP4\n";
        settings.codec=fr::VideoCodec::ProRes;
        for(int profile=0;profile<4;++profile){
            settings.proResProfile=static_cast<fr::ProResProfile>(profile);
            const auto encoded=render(project,folder/("ProRes "+std::to_string(profile)+".mov"),settings);
            require(encoded.video==AV_CODEC_ID_PRORES && encoded.profile==profile,"ProRes profile mismatch.");
            require(encoded.pixelFormat==AV_PIX_FMT_YUV422P10LE && encoded.audio==AV_CODEC_ID_PCM_S24LE,"ProRes pixel format or PCM audio mismatch.");
        }
        std::cout<<"PASS: ProRes Proxy/LT/422/HQ, 10-bit 4:2:2 and PCM 24-bit\n";
        for(const auto codec:{fr::VideoCodec::H265,fr::VideoCodec::ProRes}){
            settings.codec=codec;const auto protectedFile=folder/(std::string("Protected.")+fr::exportExtension(codec));
            const auto invalid=folder/"Invalid.mp4";std::ofstream(protectedFile)<<"keep this file";std::ofstream(invalid)<<"invalid media";
            auto broken=project;broken.media.front().path=invalid;
            fr::Exporter exporter;exporter.start(broken,protectedFile,640,360,settings);wait(exporter);
            require(!exporter.successful() && fr::readText(protectedFile)=="keep this file","Failed export replaced an existing destination.");
            exporter.start(project,protectedFile,1920,1080,settings);exporter.cancel();wait(exporter);
            require(!exporter.successful() && fr::readText(protectedFile)=="keep this file","Cancelled export replaced an existing destination.");
            std::filesystem::remove(protectedFile);std::filesystem::remove(invalid);
        }
        settings.codec=fr::VideoCodec::H264;settings.rateControl=fr::RateControl::Bitrate;
        for(float value:{0.f,-1.f,1001.f,std::numeric_limits<float>::quiet_NaN(),std::numeric_limits<float>::infinity()}){settings.videoBitrateMbps=value;rejected(project,folder/"Rejected.mp4",settings);}
        settings.videoBitrateMbps=20;rejected(project,folder/"Wrong.mov",settings);
        settings.codec=fr::VideoCodec::ProRes;rejected(project,folder/"Wrong.mp4",settings);
        settings.proResProfile=static_cast<fr::ProResProfile>(4);rejected(project,folder/"Rejected.mov",settings);
        settings.codec=static_cast<fr::VideoCodec>(99);rejected(project,folder/"Rejected.mp4",settings);
        for(const auto& entry:std::filesystem::directory_iterator(folder))require(entry.path().filename().string().front()!='.',"Export left a temporary file behind.");
        std::cout<<"PASS: destination protection, cancellation, settings validation and temporary cleanup\n";
    }catch(const std::exception& error){std::cerr<<"FAIL: "<<error.what()<<'\n';return 1;}
}
