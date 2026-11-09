#include "media/Media.hpp"
extern "C" {
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libavutil/display.h>
#include <libavutil/imgutils.h>
#include <libswscale/swscale.h>
}
#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace fr {
namespace {
std::string avError(int code) { char text[AV_ERROR_MAX_STRING_SIZE]; av_strerror(code,text,sizeof(text)); return text; }
void checked(int code, const std::string& operation) { if(code<0) throw std::runtime_error(operation+": "+avError(code)); }
int rotationOf(const AVStream* stream) {
    const auto* side = av_packet_side_data_get(stream->codecpar->coded_side_data,stream->codecpar->nb_coded_side_data,AV_PKT_DATA_DISPLAYMATRIX);
    if(!side || side->size < 9*sizeof(int32_t)) return 0;
    const double value = av_display_rotation_get(reinterpret_cast<const int32_t*>(side->data));
    if(!std::isfinite(value)) return 0;
    return (static_cast<int>(std::round(-value/90.0))*90 % 360 + 360)%360;
}
struct Format {
    AVFormatContext* value = nullptr;
    ~Format() { avformat_close_input(&value); }
    void open(const std::filesystem::path& path) {
        checked(avformat_open_input(&value,path.c_str(),nullptr,nullptr),"Cannot open "+path.filename().string());
        checked(avformat_find_stream_info(value,nullptr),"Cannot read media information");
    }
};
}
Media probe(const std::filesystem::path& path) {
    if(!std::filesystem::is_regular_file(path)) throw std::runtime_error("File not found: "+path.string());
    Format format; format.open(path);
    const int index = av_find_best_stream(format.value,AVMEDIA_TYPE_VIDEO,-1,-1,nullptr,0);
    checked(index,"No supported video stream in "+path.filename().string());
    const auto* s = format.value->streams[index];
    Media a; a.path=std::filesystem::weakly_canonical(path); a.name=path.filename().string();
    a.width=s->codecpar->width; a.height=s->codecpar->height; a.rotation=rotationOf(s);
    if(a.rotation == 90 || a.rotation == 270) std::swap(a.width,a.height);
    a.duration=s->duration != AV_NOPTS_VALUE ? s->duration*av_q2d(s->time_base) : format.value->duration/static_cast<double>(AV_TIME_BASE);
    const auto rate=av_guess_frame_rate(format.value,format.value->streams[index],nullptr);
    a.fps=rate.num && rate.den ? av_q2d(rate) : 25;
    a.hasAudio=av_find_best_stream(format.value,AVMEDIA_TYPE_AUDIO,-1,-1,nullptr,0)>=0;
    if(!std::isfinite(a.duration) || a.duration<=0 || a.width<=0 || a.height<=0) throw std::runtime_error("Could not determine the video duration or size.");
    return a;
}
struct VideoReader::Impl {
    Format format;
    AVCodecContext* codec=nullptr;
    AVFrame* frame=nullptr;
    AVFrame* incoming=nullptr;
    AVPacket* packet=nullptr;
    SwsContext* scaler=nullptr;
    int stream=-1, rotation=0;
    bool draining=false;
    double decoded=-1, origin=0, fps=25;
    VideoFrame cached;
    ~Impl() { sws_freeContext(scaler); av_packet_free(&packet); av_frame_free(&frame); av_frame_free(&incoming); avcodec_free_context(&codec); }
    void open(const std::filesystem::path& path) {
        format.open(path);
        const AVCodec* decoder=nullptr;
        stream=av_find_best_stream(format.value,AVMEDIA_TYPE_VIDEO,-1,-1,&decoder,0);
        checked(stream,"No video decoder available");
        auto* s=format.value->streams[stream];
        rotation=rotationOf(s);
        origin=s->start_time == AV_NOPTS_VALUE ? 0 : s->start_time*av_q2d(s->time_base);
        const auto rate=av_guess_frame_rate(format.value,s,nullptr); if(rate.num && rate.den) fps=av_q2d(rate);
        codec=avcodec_alloc_context3(decoder);
        if(!codec) throw std::bad_alloc();
        checked(avcodec_parameters_to_context(codec,s->codecpar),"Cannot configure decoder");
        codec->thread_count=2;
        checked(avcodec_open2(codec,decoder,nullptr),"Cannot start decoder");
        frame=av_frame_alloc(); incoming=av_frame_alloc(); packet=av_packet_alloc();
        if(!frame || !incoming || !packet) throw std::bad_alloc();
    }
    bool next() {
        for(;;) {
            const int received=avcodec_receive_frame(codec,incoming);
            if(received>=0) {av_frame_unref(frame);av_frame_move_ref(frame,incoming);return true;}
            if(received==AVERROR_EOF) return false;
            checked(received==AVERROR(EAGAIN)?0:received,"Video decode failed");
            if(draining) return false;
            for(;;) {
                const int read=av_read_frame(format.value,packet);
                if(read==AVERROR_EOF) { checked(avcodec_send_packet(codec,nullptr),"Cannot flush decoder"); draining=true; break; }
                checked(read,"Cannot read video packet");
                if(packet->stream_index!=stream) { av_packet_unref(packet); continue; }
                const int sent=avcodec_send_packet(codec,packet); av_packet_unref(packet);
                checked(sent,"Cannot decode video packet"); break;
            }
        }
    }
    VideoFrame read(double target,int maxWidth) {
        target=std::max(0.0,target);
        if(!cached.rgba.empty() && std::abs(target-decoded)<0.45/fps) return cached;
        auto* s=format.value->streams[stream];
        if(decoded<0 || target < decoded-0.45/fps || target > decoded+0.75) {
            const auto timestamp=static_cast<int64_t>((target+origin)/av_q2d(s->time_base));
            checked(av_seek_frame(format.value,stream,timestamp,AVSEEK_FLAG_BACKWARD),"Cannot seek in video");
            avcodec_flush_buffers(codec); draining=false;
        }
        bool found=false;
        for(int count=0;count<100000;++count) {
            if(!next()) break;
            decoded=frame->best_effort_timestamp==AV_NOPTS_VALUE ? target : frame->best_effort_timestamp*av_q2d(s->time_base)-origin;
            found=true;
            if(decoded+0.5/fps>=target) break;
        }
        if(!found) { if(!cached.rgba.empty()) return cached; throw std::runtime_error("No decodable frame at this position."); }
        const int outWidth=std::min(frame->width,maxWidth);
        const int outHeight=std::max(1,static_cast<int>(std::round(frame->height*static_cast<double>(outWidth)/frame->width)));
        VideoFrame output; output.width=outWidth; output.height=outHeight; output.time=decoded;
        output.rgba.resize(static_cast<size_t>(outWidth)*outHeight*4);
        scaler=sws_getCachedContext(scaler,frame->width,frame->height,static_cast<AVPixelFormat>(frame->format),outWidth,outHeight,AV_PIX_FMT_RGBA,SWS_BILINEAR,nullptr,nullptr,nullptr);
        if(!scaler) throw std::runtime_error("Could not create the video colour converter.");
        std::uint8_t* dest[4]={output.rgba.data(),nullptr,nullptr,nullptr}; int lines[4]={outWidth*4,0,0,0};
        sws_scale(scaler,frame->data,frame->linesize,0,frame->height,dest,lines);
        if(rotation) {
            VideoFrame rotated; rotated.time=decoded;
            rotated.width=(rotation==90 || rotation==270)?outHeight:outWidth;
            rotated.height=(rotation==90 || rotation==270)?outWidth:outHeight;
            rotated.rgba.resize(output.rgba.size());
            for(int y=0;y<outHeight;++y) for(int x=0;x<outWidth;++x) {
                int dx=x,dy=y;
                if(rotation==90) {dx=outHeight-1-y;dy=x;}
                if(rotation==180) {dx=outWidth-1-x;dy=outHeight-1-y;}
                if(rotation==270) {dx=y;dy=outWidth-1-x;}
                std::copy_n(output.rgba.data()+(static_cast<size_t>(y)*outWidth+x)*4,4,rotated.rgba.data()+(static_cast<size_t>(dy)*rotated.width+dx)*4);
            }
            output=std::move(rotated);
        }
        cached=output; return output;
    }
};
VideoReader::VideoReader(const std::filesystem::path& path):impl_(std::make_unique<Impl>()) { impl_->open(path); }
VideoReader::~VideoReader()=default;
VideoFrame VideoReader::read(double time,int maxWidth) { return impl_->read(time,maxWidth); }
Preview::Preview():thread_([this]{run();}) {}
Preview::~Preview() { {std::lock_guard<std::mutex> lock(mutex_);stop_=true;} wake_.notify_one();thread_.join(); }
void Preview::request(const std::filesystem::path& path,double time,std::uint64_t generation) { {std::lock_guard<std::mutex> lock(mutex_);request_=Request{path,time,generation};} wake_.notify_one(); }
std::optional<PreviewResult> Preview::take() { std::lock_guard<std::mutex> lock(mutex_);auto value=std::move(result_);result_.reset();return value; }
void Preview::run() {
    std::unique_ptr<VideoReader> reader; std::filesystem::path current;
    for(;;) {
        Request r;
        { std::unique_lock<std::mutex> lock(mutex_);wake_.wait(lock,[this]{return stop_ || request_.has_value();});if(stop_) return;r=std::move(*request_);request_.reset(); }
        PreviewResult result;result.path=r.path;result.requestedTime=r.time;result.generation=r.generation;
        try {
            if(current!=r.path || !reader) {reader=std::make_unique<VideoReader>(r.path);current=r.path;}
            result.frame=reader->read(r.time);
        } catch(const std::exception& error) {result.error=error.what();reader.reset();current.clear();}
        {std::lock_guard<std::mutex> lock(mutex_);result_=std::move(result);}
    }
}
void applyGrade(VideoFrame& frame,const Grade& grade) {
    if(grade.brightness==0 && grade.contrast==1 && grade.saturation==1) return;
    // Display-referred approximation. Export uses FFmpeg's eq filter.
    for(size_t i=0;i+3<frame.rgba.size();i+=4) {
        float r=frame.rgba[i]/255.f,g=frame.rgba[i+1]/255.f,b=frame.rgba[i+2]/255.f;
        const float l=.2126f*r+.7152f*g+.0722f*b;
        const float channels[3]={r,g,b};
        for(int c=0;c<3;++c) frame.rgba[i+c]=static_cast<std::uint8_t>(std::clamp(((l+(channels[c]-l)*grade.saturation)-.5f)*grade.contrast+.5f+grade.brightness,0.f,1.f)*255.f);
    }
}
}
