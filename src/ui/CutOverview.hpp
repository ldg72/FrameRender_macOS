#pragma once
#include "core/Project.hpp"
#include <algorithm>
#include <array>
#include <vector>

namespace fr {
struct OverviewSegment {
    const Clip* clip;
    double start, end;
};

// Display-only projection. Pointers remain valid while project.clips is unchanged.
inline std::vector<OverviewSegment> cutOverviewSegments(const Project& project){
    struct Event { double time; const Clip* clip; bool entering; };
    std::vector<Event> events;events.reserve(project.clips.size()*2);
    for(const auto& clip:project.clips){
        events.push_back({clip.start,&clip,true});events.push_back({clip.end(),&clip,false});
    }
    std::sort(events.begin(),events.end(),[](const Event& a,const Event& b){
        if(a.time!=b.time)return a.time<b.time;
        return a.entering<b.entering; // End a track's previous clip before starting the next.
    });
    std::vector<OverviewSegment> segments;
    std::array<const Clip*,2> active{};
    double previous=0;
    for(size_t i=0;i<events.size();){
        const double time=events[i].time;
        const Clip* visible=active[1]?active[1]:active[0];
        if(time>previous){
            if(!segments.empty() && segments.back().clip==visible)segments.back().end=time;
            else segments.push_back({visible,previous,time});
        }
        do{
            const auto& event=events[i];
            active[event.clip->track]=event.entering?event.clip:nullptr;
            ++i;
        }while(i<events.size() && events[i].time==time);
        previous=time;
    }
    for(size_t i=1;i<segments.size();++i){
        auto& left=segments[i-1];auto& right=segments[i];
        if(!left.clip || !right.clip)continue;
        const auto& a=*left.clip;const auto& b=*right.clip;
        // Move a partial overlap's visual cut to its center. Contained overlays
        // keep their visible entry/exit so neither the insert nor its return disappears.
        if(a.start<b.start && b.start<a.end() && a.end()<b.end()){
            const double middle=b.start+(a.end()-b.start)*.5;
            if(middle>left.start && middle<right.end)left.end=right.start=middle;
        }
    }
    return segments;
}
}
