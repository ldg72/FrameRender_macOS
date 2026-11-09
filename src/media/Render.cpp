#include "media/Render.hpp"
#include <algorithm>
#include <cctype>
#include <cmath>
#include <iomanip>
#include <sstream>
#include <stdexcept>

namespace fr {
namespace {
std::string number(double value) {std::ostringstream out;out.imbue(std::locale::classic());out<<std::fixed<<std::setprecision(8)<<value;return out.str();}
void add(std::vector<std::string>& args,std::initializer_list<std::string> items) {args.insert(args.end(),items);}
std::vector<std::string> baseArgs() {return {ffmpegPath(),"-hide_banner","-loglevel","error","-nostdin","-filter_complex_threads","1"};}
void input(std::vector<std::string>& args,const Media& media,double in,double length) {add(args,{"-ss",number(in),"-t",number(length),"-i",media.path.string()});}
void checkProject(const Project& p) {
    p.validate();
    if(p.clips.empty() || p.duration()<=0) throw std::runtime_error("Add a clip to the timeline first.");
    if(p.clips.size()>128)throw std::runtime_error("This initial renderer supports up to 128 clips per export.");
    for(const auto& c:p.clips)if(!std::filesystem::is_regular_file(p.asset(c.media)->path))throw std::runtime_error("Missing media: "+p.asset(c.media)->name);
}
void checkSettings(const ExportSettings& settings,const std::filesystem::path& output){
    const std::string expected=std::string(".")+exportExtension(settings.codec);
    std::string extension=output.extension().string();
    std::transform(extension.begin(),extension.end(),extension.begin(),[](unsigned char c){return static_cast<char>(std::tolower(c));});
    if(extension!=expected)throw std::runtime_error("This codec requires a "+expected+" file.");
    if(settings.codec==VideoCodec::ProRes){
        if(settings.proResProfile<ProResProfile::Proxy || settings.proResProfile>ProResProfile::HQ)throw std::runtime_error("Invalid ProRes profile.");
    }else{
        if(settings.rateControl!=RateControl::Quality && settings.rateControl!=RateControl::Bitrate)throw std::runtime_error("Invalid bitrate mode.");
        if(settings.rateControl==RateControl::Bitrate && (!std::isfinite(settings.videoBitrateMbps) || settings.videoBitrateMbps<minVideoBitrateMbps || settings.videoBitrateMbps>maxVideoBitrateMbps))throw std::runtime_error("Video bitrate must be between 0.1 and 1000 Mbps.");
    }
}
}
const char* exportExtension(VideoCodec codec){
    switch(codec){case VideoCodec::H264:case VideoCodec::H265:return "mp4";case VideoCodec::ProRes:return "mov";}
    throw std::runtime_error("Unsupported export codec.");
}
std::vector<std::string> audioCommand(const Project& p,double start) {
    checkProject(p);
    auto args=baseArgs();std::string graph;std::vector<std::string> labels;int index=0;
    const double total=std::max(.001,p.duration()-start);
    for(const auto& c:p.clips) {
        const auto* a=p.asset(c.media);
        if(!a->hasAudio || c.muted || c.gain==0 || c.end()<=start)continue;
        const double skip=std::max(0.0,start-c.start),delay=std::max(0.0,c.start-start);
        input(args,*a,c.in+skip,c.duration-skip);
        const std::string label="a"+std::to_string(index);
        graph+="["+std::to_string(index)+":a:0]aresample=48000,aformat=sample_fmts=flt:channel_layouts=stereo,asetpts=PTS-STARTPTS,volume="+number(c.gain)+",adelay="+std::to_string(static_cast<long long>(std::round(delay*48000)))+"S:all=1["+label+"];";
        labels.push_back("["+label+"]");++index;
    }
    graph+="anullsrc=r=48000:cl=stereo,atrim=duration="+number(total)+"[silence];[silence]";
    for(const auto& label:labels)graph+=label;
    graph+="amix=inputs="+std::to_string(labels.size()+1)+":duration=first:normalize=0,alimiter=limit=0.95:level=0:latency=1[out]";
    add(args,{"-filter_complex",graph,"-map","[out]","-t",number(total),"-f","f32le","-acodec","pcm_f32le","-ac","2","-ar","48000","pipe:1"});
    return args;
}
std::vector<std::string> renderCommand(const Project& p,const std::filesystem::path& output,int width,int height,const ExportSettings& settings) {
    checkProject(p);checkSettings(settings,output);
    if(width<16 || width>8192 || height<16 || height>8192 || width%2 || height%2)throw std::runtime_error("Export dimensions must be even and between 16 and 8192.");
    auto args=baseArgs();std::string graph;std::vector<Clip> clips=p.clips;
    const bool proRes=settings.codec==VideoCodec::ProRes;
    std::stable_sort(clips.begin(),clips.end(),[](const Clip& a,const Clip& b){return a.track<b.track || (a.track==b.track && a.start<b.start);});
    graph="color=c=black:s="+std::to_string(width)+"x"+std::to_string(height)+":r="+number(p.fps)+":d="+number(p.duration())+(proRes?",format=yuv422p10le":"")+"[v0];";
    std::vector<std::string> audio;
    for(size_t i=0;i<clips.size();++i) {
        const auto& c=clips[i];const auto* a=p.asset(c.media);input(args,*a,c.in,c.duration);
        const auto n=std::to_string(i);
        graph+="["+n+":v:0]setpts=PTS-STARTPTS,scale="+std::to_string(width)+":"+std::to_string(height)+":force_original_aspect_ratio=decrease,pad="+std::to_string(width)+":"+std::to_string(height)+":(ow-iw)/2:(oh-ih)/2,setsar=1,fps="+number(p.fps);
        const bool graded=c.grade.brightness!=0 || c.grade.contrast!=1 || c.grade.saturation!=1;
        // Preserve 10-bit samples for ungraded ProRes media; eq remains the existing SDR correction.
        if(!proRes || graded)graph+=",eq=brightness="+number(c.grade.brightness)+":contrast="+number(c.grade.contrast)+":saturation="+number(c.grade.saturation);
        if(c.fadeIn>0 || c.fadeOut>0){
            graph+=proRes?",format=yuva422p10le":",format=yuva420p";
            if(c.fadeIn>0)graph+=",fade=t=in:st=0:d="+number(c.fadeIn)+":alpha=1";
            if(c.fadeOut>0)graph+=",fade=t=out:st="+number(c.duration-c.fadeOut)+":d="+number(c.fadeOut)+":alpha=1";
        }else if(proRes)graph+=",format=yuv422p10le";
        const auto firstFrame=std::llround(c.start*p.fps),endFrame=std::llround(c.end()*p.fps);
        // fps already uses one tick per project frame. Avoid truncating a floating
        // seconds/TB offset, and keep the final decoded picture through the clip's window.
        graph+=",setpts=PTS+"+std::to_string(firstFrame)+"[c"+n+"];";
        graph+="[v"+n+"][c"+n+"]overlay=eof_action=repeat:repeatlast=1:alpha=straight:"+(proRes?std::string("format=yuv422p10:"):std::string())+"enable='gte(n,"+std::to_string(firstFrame)+")*lt(n,"+std::to_string(endFrame)+")'[v"+std::to_string(i+1)+"];";
        if(a->hasAudio && !c.muted && c.gain>0) {
            graph+="["+n+":a:0]aresample=48000,aformat=channel_layouts=stereo,asetpts=PTS-STARTPTS,volume="+number(c.gain)+",adelay="+std::to_string(static_cast<long long>(std::round(c.start*48000)))+"S:all=1[a"+n+"];";
            audio.push_back("[a"+n+"]");
        }
    }
    graph+="anullsrc=r=48000:cl=stereo,atrim=duration="+number(p.duration())+"[silence];[silence]";
    for(const auto& a:audio)graph+=a;
    graph+="amix=inputs="+std::to_string(audio.size()+1)+":duration=first:normalize=0,alimiter=limit=0.95:level=0:latency=1[outa]";
    add(args,{"-filter_complex",graph,"-map","[v"+std::to_string(clips.size())+"]","-map","[outa]","-t",number(p.duration())});
    if(settings.codec==VideoCodec::ProRes){
        add(args,{"-c:v","prores_ks","-profile:v",std::to_string(static_cast<int>(settings.proResProfile)),"-pix_fmt","yuv422p10le","-c:a","pcm_s24le","-ac","2","-ar","48000","-f","mov"});
    }else{
        add(args,{"-c:v",settings.codec==VideoCodec::H265?"libx265":"libx264","-preset","veryfast"});
        if(settings.rateControl==RateControl::Bitrate)add(args,{"-b:v",std::to_string(std::llround(settings.videoBitrateMbps*1000000.0))});
        else add(args,{"-crf","20"});
        if(settings.codec==VideoCodec::H265)add(args,{"-tag:v","hvc1","-x265-params","log-level=error"});
        add(args,{"-pix_fmt","yuv420p","-c:a","aac","-b:a","192k","-ac","2","-ar","48000","-f","mp4"});
    }
    add(args,{"-movflags","+faststart","-progress","pipe:1","-nostats","-y",output.string()});
    return args;
}
Exporter::~Exporter(){cancel();if(thread_.joinable())thread_.join();}
void Exporter::cancel(){cancelled_=true;process_.cancel();}
std::string Exporter::message()const{std::lock_guard<std::mutex> lock(mutex_);return message_;}
bool Exporter::successful()const{std::lock_guard<std::mutex> lock(mutex_);return successful_;}
void Exporter::start(Project project,const std::filesystem::path& output,int width,int height,const ExportSettings& settings) {
    if(running_)return;
    if(thread_.joinable())thread_.join();
    checkSettings(settings,output);
    auto temp=output.parent_path()/("."+output.stem().string()+"-"+temporaryPath("").filename().string()+"."+exportExtension(settings.codec));
    auto args=renderCommand(project,temp,width,height,settings);
    cancelled_=false;progress_=0;
    {std::lock_guard<std::mutex> lock(mutex_);message_="Preparing export...";successful_=false;}
    running_=true;
    thread_=std::thread([this,args=std::move(args),output,temp,duration=project.duration()] {
        const auto errors=temporaryPath(".log");
        try {
            process_.start(args,errors);
            if(cancelled_)process_.cancel();
            std::string pending;char buffer[2048];
            for(long count;(count=process_.read(buffer,sizeof(buffer)))>0;) {
                pending.append(buffer,static_cast<size_t>(count));
                size_t newline;
                while((newline=pending.find('\n'))!=std::string::npos) {
                    const std::string line=pending.substr(0,newline);pending.erase(0,newline+1);
                    if(line.rfind("out_time_us=",0)==0)try{progress_=std::clamp(std::stod(line.substr(12))/1000000/duration,0.0,1.0);}catch(...){}
                }
            }
            const int code=process_.wait();
            if(cancelled_)throw std::runtime_error("Export cancelled.");
            if(code)throw std::runtime_error("Export failed:\n"+readText(errors));
            std::filesystem::rename(temp,output);
            progress_=1;
            {std::lock_guard<std::mutex> lock(mutex_);message_="Export complete: "+output.filename().string();successful_=true;}
        }catch(const std::exception& error){process_.cancel();process_.wait();std::lock_guard<std::mutex> lock(mutex_);message_=error.what();}
        std::error_code ec;std::filesystem::remove(temp,ec);std::filesystem::remove(errors,ec);running_=false;
    });
}
}
