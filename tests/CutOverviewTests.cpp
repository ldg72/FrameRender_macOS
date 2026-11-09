#include "ui/CutOverview.hpp"
#include <cmath>
#include <iostream>
#include <stdexcept>

using namespace fr;
void check(bool ok,const char* message){if(!ok)throw std::runtime_error(message);}
Clip clip(Id id,double start,double end,int track=0){Clip c;c.id=id;c.start=start;c.duration=end-start;c.track=track;return c;}
void expect(const std::vector<OverviewSegment>& result,size_t index,Id id,double start,double end){
    check(index<result.size(),"Missing overview segment");const auto& s=result[index];
    check((s.clip?s.clip->id:0)==id,"Wrong clip in overview");
    check(std::abs(s.start-start)<1e-9 && std::abs(s.end-end)<1e-9,"Wrong visual boundary");
    check(s.end>s.start,"Empty or inverted overview segment");
}
int main(){
    try{
        Project p;check(cutOverviewSegments(p).empty(),"Empty montage must have no segments");
        p.clips={clip(1,0,10)};auto s=cutOverviewSegments(p);check(s.size()==1,"Single clip split unexpectedly");expect(s,0,1,0,10);
        p.clips={clip(2,8,18,1),clip(1,0,10)};
        s=cutOverviewSegments(p);check(s.size()==2,"Partial overlap should yield two segments");expect(s,0,1,0,9);expect(s,1,2,9,18);
        check(p.clips[0].start==8 && p.clips[1].end()==10 && p.duration()==18,"Projection changed the montage");
        check(p.visibleAt(8.5)->id==2,"Visual midpoint must not change actual playback precedence");
        p.clips[0].track=0;p.clips[1].track=1;
        s=cutOverviewSegments(p);expect(s,0,1,0,9);expect(s,1,2,9,18);
        p.clips={clip(3,16,26),clip(1,0,10),clip(2,8,18,1)};
        s=cutOverviewSegments(p);check(s.size()==3,"Chained overlaps should stay on one line");expect(s,0,1,0,9);expect(s,1,2,9,17);expect(s,2,3,17,26);
        p.clips={clip(3,12,14,1),clip(2,6,10),clip(1,2,6)};
        s=cutOverviewSegments(p);check(s.size()==5,"Real gaps were removed or touching clips merged");expect(s,0,0,0,2);expect(s,1,1,2,6);expect(s,2,2,6,10);expect(s,3,0,10,12);expect(s,4,3,12,14);
        p.clips={clip(1,0,10),clip(2,2,4,1)};
        s=cutOverviewSegments(p);check(s.size()==3,"Contained insert lost its return");expect(s,0,1,0,2);expect(s,1,2,2,4);expect(s,2,1,4,10);
        p.clips={clip(1,0,10,1),clip(2,2,4)};
        s=cutOverviewSegments(p);check(s.size()==1,"Hidden clip created a false visual cut");expect(s,0,1,0,10);
        p.clips={clip(1,0,10),clip(2,0,10,1)};
        s=cutOverviewSegments(p);check(s.size()==1,"Identical intervals created an empty segment");expect(s,0,2,0,10);
        p.clips={clip(1,0,3),clip(2,0,1,1),clip(3,1,3,1)};
        s=cutOverviewSegments(p);check(s.size()==2,"Same-time starts/ends corrupted the track state");expect(s,0,2,0,1);expect(s,1,3,1,3);
        std::cout<<"PASS: midpoint cuts, track order, chained overlaps, gaps, contained/hidden clips and unchanged playback\n";
    }catch(const std::exception& error){std::cerr<<"FAIL: "<<error.what()<<'\n';return 1;}
}
