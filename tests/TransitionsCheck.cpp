#include "core/Project.hpp"
#include "media/Media.hpp"
#include "media/Render.hpp"
#include <array>
#include <chrono>
#include <cmath>
#include <fstream>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <thread>

using namespace fr;
void require(bool ok,const std::string& message){if(!ok)throw std::runtime_error(message);}
void run(const std::vector<std::string>& args,const std::filesystem::path& log){
    Process process;process.start(args,log);char buffer[4096];while(process.read(buffer,sizeof(buffer))>0){}
    require(process.wait()==0,readText(log));
}
void render(const Project& p,const std::filesystem::path& path,VideoCodec codec){
    ExportSettings settings;settings.codec=codec;
    Exporter exporter;exporter.start(p,path,320,180,settings);
    const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(30);
    while(exporter.running() && std::chrono::steady_clock::now()<deadline)std::this_thread::sleep_for(std::chrono::milliseconds(10));
    if(exporter.running()){exporter.cancel();throw std::runtime_error("Transition export timed out");}
    require(exporter.successful(),exporter.message());
    require(std::abs(probe(path).duration-p.duration())<.08,"Transition changed export duration");
}
std::array<double,3> pixel(VideoReader& reader,double time){
    const auto image=reader.read(time);const auto i=(image.height/2*image.width+image.width/2)*4;
    return {static_cast<double>(image.rgba[i]),static_cast<double>(image.rgba[i+1]),static_cast<double>(image.rgba[i+2])};
}
void mixed(const std::array<double,3>& p){require(p[0]>100 && p[0]<155 && p[1]<10 && p[2]>100 && p[2]<155,"Cross dissolve midpoint must blend red and blue without darkening");}
int main(int argc,char** argv){
    if(argc!=2)return 2;
    try{
        const std::filesystem::path dir=argv[1];std::filesystem::create_directories(dir);
        for(const auto* color:{"red","blue"})run({ffmpegPath(),"-hide_banner","-loglevel","error","-f","lavfi","-i",std::string("color=c=")+color+":s=320x180:r=25:d=3","-c:v","libx264","-pix_fmt","yuv420p","-y",(dir/(std::string(color)+".mp4")).string()},dir/"generate.log");
        Project p;p.width=320;p.height=180;
        const auto red=p.addMedia(probe(dir/"red.mp4")),blue=p.addMedia(probe(dir/"blue.mp4"));
        const auto a=p.append(red),b=p.append(blue,1);require(p.trim(a,0,2) && p.trim(b,0,2) && p.move(b,1,1),"Cannot prepare overlap");
        require(p.crossDissolve(a),"Cannot apply dissolve from outgoing clip");
        require(p.clip(b)->fadeIn==1 && p.clip(a)->fadeOut==0,"Dissolve must fade the upper picture only");
        require(clipOpacity(*p.clip(b),1)==0 && clipOpacity(*p.clip(b),1.5)==.5 && clipOpacity(*p.clip(b),2)==1,"Wrong preview dissolve opacity");
        p.validate();p.save(dir/"Dissolve.frp");auto loaded=Project::load(dir/"Dissolve.frp");
        require(loaded.clip(b)->fadeIn==1 && loaded.clip(b)->start==1 && loaded.clip(a)->end()==2,"Dissolve persistence changed timing");
        History history;history.checkpoint(p);p.setFades(b,0,0);require(history.undo(p) && p.clip(b)->fadeIn==1,"Undo lost transition");require(history.redo(p) && p.clip(b)->fadeIn==0,"Redo lost transition");p=loaded;
        for(auto codec:{VideoCodec::H264,VideoCodec::H265,VideoCodec::ProRes}){
            const auto path=dir/(std::string("Dissolve-")+std::to_string(static_cast<int>(codec))+"."+exportExtension(codec));render(p,path,codec);VideoReader reader(path);
            auto redPixel=pixel(reader,.5);require(redPixel[0]>235 && redPixel[2]<10,"Outgoing clip is not red");
            mixed(pixel(reader,1.48));auto bluePixel=pixel(reader,2.4);require(bluePixel[2]>235 && bluePixel[0]<10,"Incoming clip is not blue");
        }
        auto cuts=p;cuts.setFades(b,0,0);require(cuts.move(b,2,1),"Cannot prepare adjacent clips");
        for(auto codec:{VideoCodec::H264,VideoCodec::H265,VideoCodec::ProRes}){
            const auto path=dir/(std::string("Adjacent-")+std::to_string(static_cast<int>(codec))+"."+exportExtension(codec));
            render(cuts,path,codec);VideoReader reader(path);
            for(int frame=45;frame<56;++frame){const auto color=pixel(reader,frame/25.0);require(color[0]>230 || color[2]>230,"Unintended black frame at an adjacent cut");}
        }
        auto fractional=cuts;fractional.fps=30000.0/1001;
        fractional.clips[0].start=19/fractional.fps;fractional.clips[0].duration=89/fractional.fps;
        fractional.clips[1].start=108/fractional.fps;fractional.clips[1].duration=88/fractional.fps;
        for(auto codec:{VideoCodec::H264,VideoCodec::H265,VideoCodec::ProRes}){
            const auto path=dir/(std::string("Fractional-cuts-")+std::to_string(static_cast<int>(codec))+"."+exportExtension(codec));
            render(fractional,path,codec);VideoReader fractionalReader(path);
            const auto gap=pixel(fractionalReader,18/fractional.fps);require(gap[0]<8 && gap[2]<8,"Real leading gap was filled by a retained frame");
            require(pixel(fractionalReader,19/fractional.fps)[0]>230,"Black first frame at fractional clip start");
            for(int frame=102;frame<115;++frame){const auto color=pixel(fractionalReader,frame/fractional.fps);require(frame<108?color[0]>230:color[2]>230,"Black or displaced frame at fractional-fps cut, frame "+std::to_string(frame));}
        }
        auto reverse=p;reverse.clip(a)->track=1;reverse.clip(b)->track=0;reverse.setFades(b,0,0);require(reverse.crossDissolve(b),"Reverse track dissolve failed");
        require(reverse.clip(a)->fadeOut==1,"Reverse dissolve must fade the outgoing upper clip");render(reverse,dir/"Reverse.mp4",VideoCodec::H264);VideoReader reverseReader(dir/"Reverse.mp4");mixed(pixel(reverseReader,1.48));
        Project fade;fade.width=320;fade.height=180;const auto media=fade.addMedia(probe(dir/"red.mp4")),id=fade.append(media);fade.setFades(id,1,1);fade.save(dir/"Fades.frp");render(fade,dir/"Fades.mp4",VideoCodec::H264);VideoReader reader(dir/"Fades.mp4");
        require(pixel(reader,0)[0]<8 && pixel(reader,1.48)[0]>235 && pixel(reader,2.96)[0]<20,"Fade endpoints or steady picture are wrong");
        auto in=pixel(reader,.48),out=pixel(reader,2.52);require(in[0]>105 && in[0]<145 && out[0]>105 && out[0]<145,"Fade in/out ramps are wrong");
        auto shortened=fade;require(shortened.trim(id,0,.6),"Cannot shorten faded clip");shortened.validate();require(shortened.clip(id)->fadeIn+shortened.clip(id)->fadeOut<=.600001,"Trim left fades longer than clip");
        auto split=fade;require(split.split(id,1.5),"Cannot split faded clip");split.validate();require(split.clips[0].fadeIn==1 && split.clips[0].fadeOut==0 && split.clips[1].fadeIn==0 && split.clips[1].fadeOut==1,"Split did not preserve outer fades");
        auto invalid=fade;invalid.clip(id)->fadeIn=std::numeric_limits<double>::quiet_NaN();bool rejected=false;try{invalid.validate();}catch(...){rejected=true;}require(rejected,"Non-finite fade accepted");
        require(!fade.setFades(id,-1,0) && !fade.crossDissolve(id),"Invalid fades or missing overlap accepted");
        std::ofstream(dir/"Legacy.frp")<<"FRAME_RENDER 1\n\"Legacy\"\n320 180 25 3\n1\n1 \"red.mp4\" \"Red\" 3 25 320 180 0 0\n1\n2 1 0 0 3 0 1 0 0 1 1\n";
        require(Project::load(dir/"Legacy.frp").clips[0].fadeIn==0,"Legacy project did not load without fades");
        std::cout<<"PASS: fade/dissolve pixels in H.264/HEVC/ProRes, reverse tracks, persistence, undo, trim/split and legacy projects\n";
    }catch(const std::exception& error){std::cerr<<"FAIL: "<<error.what()<<'\n';return 1;}
}
