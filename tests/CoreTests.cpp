#include "core/Project.hpp"
#include <cmath>
#include <fstream>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <unistd.h>

void check(bool ok,const char* message){if(!ok)throw std::runtime_error(message);}
int main(){
    using namespace fr;
    const auto dir=std::filesystem::temp_directory_path()/("framerender-core-"+std::to_string(getpid()));
    try{
        std::filesystem::create_directories(dir);
        Project p;Media a;a.path=dir/"clip with spaces and 'quotes'.mp4";a.name="Test clip";a.duration=10;a.width=1920;a.height=1080;a.hasAudio=true;
        const Id asset=p.addMedia(a),clip=p.append(asset);p.validate();
        check(p.split(clip,4),"split should succeed");check(p.clips.size()==2,"split should create two clips");
        check(p.clips[1].in==4 && p.clips[1].start==4 && p.clips[1].duration==6,"split must preserve source position and length");
        check(!p.move(p.clips[1].id,2,0),"same-track overlap must be rejected");
        check(p.move(p.clips[1].id,2,1),"clips may overlap on different tracks");
        check(p.visibleAt(3)->track==1,"upper video track should be visible");
        check(!p.trim(clip,-1,4),"negative source in must be rejected");
        check(!p.trim(clip,8,4),"trim cannot exceed the source");
        check(!p.trim(clip,std::numeric_limits<double>::quiet_NaN(),2),"NaN source in must be rejected");
        check(!p.move(clip,std::numeric_limits<double>::infinity(),0),"infinite positions must be rejected");
        check(!p.split(clip,std::numeric_limits<double>::quiet_NaN()),"NaN split time must be rejected");
        check(p.trim(clip,1,3),"valid trim should succeed");
        History h;h.checkpoint(p);const auto before=p.clips.size();p.erase(clip);
        check(h.undo(p) && p.clips.size()==before,"undo must restore clips");check(h.redo(p) && p.clips.size()==before-1,"redo must restore deletion");
        p.save(dir/"project.frp");auto loaded=Project::load(dir/"project.frp");
        check(loaded.media.front().path==a.path,"relative media paths must round-trip");check(loaded.clips.front().in==4,"source in must persist");
        p.clips.front().grade.saturation=1.7f;p.clips.front().gain=.4f;p.save(dir/"project.frp");loaded=Project::load(dir/"project.frp");
        check(std::abs(loaded.clips.front().gain-.4f)<1e-5,"audio gain must persist");check(std::abs(loaded.clips.front().grade.saturation-1.7f)<1e-5,"grade must persist");
        std::ofstream(dir/"bad.frp")<<"FRAME_RENDER 1\n\"Broken\"\n1920 1080 25 1\n999999999\n";
        bool rejected=false;try{Project::load(dir/"bad.frp");}catch(...){rejected=true;}check(rejected,"malformed project must be rejected");
        p.clips.front().duration=-1;rejected=false;try{p.validate();}catch(...){rejected=true;}check(rejected,"negative clip length must be rejected");
        Project ripple;const auto ra=ripple.addMedia(a);ripple.append(ra);ripple.append(ra);const auto first=ripple.clips.front().id;ripple.erase(first,true);
        check(ripple.clips.front().start==0,"ripple delete must close the gap");
        check(timecode(1.2,25)=="00:00:01:05","timecode should use project frame rate");
        std::filesystem::remove_all(dir);std::cout<<"PASS: editing, track overlap, source trim, history, atomic project round-trip, malformed input, ripple delete\n";
    }catch(const std::exception& error){std::cerr<<"FAIL: "<<error.what()<<'\n';std::filesystem::remove_all(dir);return 1;}
}
