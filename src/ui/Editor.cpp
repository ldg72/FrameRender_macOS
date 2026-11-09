#include "ui/Editor.hpp"
#include "ui/CutOverview.hpp"
#include "core/Project.hpp"
#include "media/Media.hpp"
#include "media/Render.hpp"
#include "media/Transport.hpp"
#include "platform/Platform.hpp"
#include "imgui.h"
#include "imgui_internal.h"
#include "imnodes.h"
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <future>
#include <map>
#include <memory>
#include <optional>
#include <sstream>

namespace fr {
namespace {
constexpr ImVec4 accent{.36f,.80f,.68f,1};
constexpr ImVec4 muted{.51f,.55f,.60f,1};
// Keep existing IDs so saved docking layouts still belong to the same workspaces.
struct Workspace { const char* name; int id; };
constexpr std::array<Workspace,6> pages{{{"Media",0},{"Edit",2},{"Fusion",3},{"Color",4},{"Fairlight",5},{"Deliver",6}}};
ImU32 rgba(int r,int g,int b,int a=255){return IM_COL32(r,g,b,a);}
void label(const char* text){ImGui::PushStyleColor(ImGuiCol_Text,muted);ImGui::TextUnformatted(text);ImGui::PopStyleColor();}
void tip(const char* text){if(ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal))ImGui::SetTooltip("%s",text);}
bool button(const char* text,bool active=false,ImVec2 size={0,0}){
    if(active){ImGui::PushStyleColor(ImGuiCol_Button,ImVec4(.23f,.42f,.37f,1));ImGui::PushStyleColor(ImGuiCol_Text,accent);}
    const bool pressed=ImGui::Button(text,size);
    if(active)ImGui::PopStyleColor(2);return pressed;
}
void sliderTrack(ImDrawList* draw,float start,float end,float y,float knob,bool highlighted){
    draw->AddLine({start,y},{end,y},ImGui::GetColorU32(ImGuiCol_SeparatorHovered,.35f),1);
    draw->AddLine({start,y},{knob,y},ImGui::GetColorU32(ImGuiCol_CheckMark,.7f),1);
    draw->AddCircleFilled({knob,y},highlighted?4.f:3.f,ImGui::GetColorU32(ImGuiCol_CheckMark));
}
bool minimalSlider(const char* name,float* value,float minimum,float maximum,const char* format="%.2f",ImGuiSliderFlags flags=0){
    auto* window=ImGui::GetCurrentWindow();if(window->SkipItems)return false;
    auto& g=*GImGui;const ImGuiID id=window->GetID(name);
    const float width=std::max(1.f,ImGui::CalcItemWidth()),height=std::max(10.f,ImGui::GetTextLineHeight());
    char text[64]{},lo[64]{},hi[64]{};
    const bool showValue=format && *format;
    float valueWidth=0;
    if(showValue){
        std::snprintf(text,sizeof(text),format,*value);std::snprintf(lo,sizeof(lo),format,minimum);std::snprintf(hi,sizeof(hi),format,maximum);
        valueWidth=std::max({ImGui::CalcTextSize(text).x,ImGui::CalcTextSize(lo).x,ImGui::CalcTextSize(hi).x});
    }
    const bool fitsValue=showValue && width>valueWidth+60;
    const ImVec2 cursor=ImGui::GetCursorScreenPos();
    const ImVec2 pos{cursor.x,cursor.y+std::max(0.f,window->DC.CurrLineTextBaseOffset)};
    const ImRect bounds(pos,{pos.x+width,pos.y+height});
    const ImRect track(pos,{pos.x+std::max(10.f,width-(fitsValue?valueWidth+8:0)),pos.y+height});
    const bool inputAllowed=(flags&ImGuiSliderFlags_NoInput)==0;
    ImGui::ItemSize(bounds,0);
    if(!ImGui::ItemAdd(bounds,id,&track,inputAllowed?ImGuiItemFlags_Inputable:0))return false;
    const bool hovered=ImGui::ItemHoverable(track,id,g.LastItemData.ItemFlags);
    bool inputActive=inputAllowed && ImGui::TempInputIsActive(id);
    if(!inputActive){
        const bool clicked=hovered && ImGui::IsMouseClicked(0,ImGuiInputFlags_None,id);
        const bool activate=clicked || g.NavActivateId==id;
        if(clicked)ImGui::SetKeyOwner(ImGuiKey_MouseLeft,id);
        if(activate && inputAllowed && ((clicked && g.IO.KeyCtrl) || (g.NavActivateId==id && (g.NavActivateFlags&ImGuiActivateFlags_PreferInput))))inputActive=true;
        if(activate)std::memcpy(&g.ActiveIdValueOnActivation,value,sizeof(*value));
        if(activate && !inputActive){
            ImGui::SetActiveID(id,window);ImGui::SetFocusID(id,window);ImGui::FocusWindow(window);
            g.ActiveIdUsingNavDirMask|=(1<<ImGuiDir_Left)|(1<<ImGuiDir_Right);
        }
    }
    if(inputActive){
        const bool clamp=(flags&ImGuiSliderFlags_ClampOnInput)!=0;
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding,{4,0});
        const bool edited=ImGui::TempInputScalar(track,id,name,ImGuiDataType_Float,value,format,clamp?&minimum:nullptr,clamp?&maximum:nullptr);
        ImGui::PopStyleVar();return edited;
    }
    // Keep ImGui's native navigation, logarithmic scaling, edit tracking and undo activation.
    ImRect grab;ImGui::PushStyleVar(ImGuiStyleVar_GrabMinSize,6);
    const bool edited=ImGui::SliderBehavior(track,id,ImGuiDataType_Float,value,&minimum,&maximum,format,flags,&grab);
    ImGui::PopStyleVar();if(edited)ImGui::MarkItemEdited(id);
    ImGui::RenderNavCursor(bounds,id);
    const float start=track.Min.x+5,end=std::max(start,track.Max.x-5),knob=std::clamp((grab.Min.x+grab.Max.x)*.5f,start,end);
    sliderTrack(window->DrawList,start,end,(track.Min.y+track.Max.y)*.5f,knob,hovered || g.ActiveId==id);
    if(showValue)std::snprintf(text,sizeof(text),format,*value);
    if(fitsValue){
        window->DrawList->AddText({bounds.Max.x-ImGui::CalcTextSize(text).x,pos.y},ImGui::GetColorU32(ImGuiCol_TextDisabled),text);
    }
    if(hovered){
        ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
        if(showValue && (!fitsValue || inputAllowed)){
            const char* hint=g.IO.ConfigMacOSXBehaviors?"%s\nCmd+click to enter a value":"%s\nCtrl+click to enter a value";
            ImGui::SetTooltip(inputAllowed?hint:"%s",text);
        }
    }
    return edited;
}
SDL_Texture* texture(SDL_Renderer* renderer,const VideoFrame& frame){
    if(frame.rgba.empty())return nullptr;
    SDL_Texture* value=SDL_CreateTexture(renderer,SDL_PIXELFORMAT_RGBA32,SDL_TEXTUREACCESS_STATIC,frame.width,frame.height);
    if(value){SDL_UpdateTexture(value,nullptr,frame.rgba.data(),frame.width*4);SDL_SetTextureScaleMode(value,SDL_SCALEMODE_LINEAR);}
    return value;
}
void drawThumbnail(ImDrawList* draw,SDL_Texture* image,ImVec2 lo,ImVec2 hi,bool selected){
    if(hi.x<=lo.x || hi.y<=lo.y)return;
    const float thickness=selected?4.f:2.f;
    draw->AddRectFilled({lo.x-thickness,lo.y-thickness},{hi.x+thickness,hi.y+thickness},ImGui::GetColorU32(ImGuiCol_CheckMark));
    draw->AddRectFilled(lo,hi,rgba(13,16,20));
    float width=0,height=0;
    if(image && SDL_GetTextureSize(image,&width,&height) && width>0 && height>0){
        const float scale=std::min((hi.x-lo.x)/width,(hi.y-lo.y)/height);
        const ImVec2 size{width*scale,height*scale};
        const ImVec2 start{lo.x+(hi.x-lo.x-size.x)*.5f,lo.y+(hi.y-lo.y-size.y)*.5f};
        draw->AddImage(ImTextureRef(image),start,{start.x+size.x,start.y+size.y});
    }
}
void drawTimelineClip(ImDrawList* draw,SDL_Texture* image,const char* name,ImVec2 lo,ImVec2 hi,ImU32 color,bool selected){
    if(hi.x<=lo.x || hi.y<=lo.y)return;
    draw->AddRectFilled(lo,hi,color,3);
    const ImVec2 innerLo{lo.x+1,lo.y+1},innerHi{hi.x-1,hi.y-1};
    if(innerHi.x>innerLo.x && innerHi.y>innerLo.y){
        draw->PushClipRect(innerLo,innerHi,true);
        float width=0,height=0;
        if(image && SDL_GetTextureSize(image,&width,&height) && width>0 && height>0){
            const float imageHeight=innerHi.y-innerLo.y;
            // A narrow clip crops the thumbnail horizontally instead of squeezing it.
            draw->AddImage(ImTextureRef(image),innerLo,{innerLo.x+imageHeight*width/height,innerHi.y});
        }
        const ImVec2 textSize=ImGui::CalcTextSize(name);
        if(textSize.x+12<=innerHi.x-innerLo.x && textSize.y+2<=innerHi.y-innerLo.y){
            const ImVec2 textPos{lo.x+(hi.x-lo.x-textSize.x)*.5f,lo.y+(hi.y-lo.y-textSize.y)*.5f};
            draw->AddText({textPos.x+1,textPos.y+1},rgba(0,0,0,200),name);
            draw->AddText(textPos,rgba(232,239,241),name);
        }
        draw->PopClipRect();
    }
    draw->AddRect(lo,hi,selected?rgba(165,241,213):rgba(12,20,24),3,0,selected?2.f:1.f);
}
struct Imported {Media media;VideoFrame thumbnail;};
struct ImportResult {std::vector<Imported> assets;std::string errors;};
void theme(){
    ImGui::StyleColorsDark();auto& s=ImGui::GetStyle();
    s.FontSizeBase=15;s.WindowPadding={12,10};s.FramePadding={8,5};s.ItemSpacing={8,7};
    s.WindowRounding=0;s.ChildRounding=3;s.FrameRounding=3;s.PopupRounding=5;s.TabRounding=3;s.GrabRounding=2;
    s.WindowBorderSize=1;s.FrameBorderSize=0;s.DockingSeparatorSize=4;s.ScrollbarSize=11;
    auto* c=s.Colors;
    c[ImGuiCol_Text]={.84f,.86f,.88f,1};c[ImGuiCol_TextDisabled]=muted;
    c[ImGuiCol_WindowBg]={.115f,.125f,.14f,1};c[ImGuiCol_ChildBg]={.105f,.115f,.13f,1};
    c[ImGuiCol_PopupBg]={.14f,.15f,.17f,1};c[ImGuiCol_Border]={.07f,.08f,.095f,1};
    c[ImGuiCol_FrameBg]={.075f,.085f,.10f,1};c[ImGuiCol_FrameBgHovered]={.20f,.23f,.25f,1};c[ImGuiCol_FrameBgActive]={.23f,.31f,.30f,1};
    c[ImGuiCol_TitleBg]={.09f,.10f,.115f,1};c[ImGuiCol_TitleBgActive]={.15f,.17f,.19f,1};
    c[ImGuiCol_MenuBarBg]={.085f,.095f,.11f,1};c[ImGuiCol_Button]={.19f,.21f,.235f,1};c[ImGuiCol_ButtonHovered]={.25f,.29f,.32f,1};c[ImGuiCol_ButtonActive]={.25f,.42f,.36f,1};
    c[ImGuiCol_Header]={.22f,.32f,.30f,1};c[ImGuiCol_HeaderHovered]={.23f,.35f,.32f,1};c[ImGuiCol_HeaderActive]={.25f,.43f,.37f,1};
    c[ImGuiCol_CheckMark]=accent;c[ImGuiCol_SliderGrab]=accent;c[ImGuiCol_SliderGrabActive]={.55f,.95f,.80f,1};
    c[ImGuiCol_Separator]={.065f,.075f,.085f,1};c[ImGuiCol_SeparatorHovered]=accent;c[ImGuiCol_SeparatorActive]=accent;
    c[ImGuiCol_Tab]={.12f,.14f,.16f,1};c[ImGuiCol_TabHovered]={.25f,.34f,.32f,1};c[ImGuiCol_TabSelected]={.22f,.27f,.28f,1};
    c[ImGuiCol_TabSelectedOverline]=accent;c[ImGuiCol_DockingPreview]={accent.x,accent.y,accent.z,.25f};
    c[ImGuiCol_ScrollbarBg]={.09f,.10f,.115f,1};c[ImGuiCol_ScrollbarGrab]={.25f,.28f,.31f,1};
    c[ImGuiCol_NavCursor]=accent;
}
}
struct Editor::Impl {
    SDL_Window* window;
    SDL_Renderer* renderer;
    Project project;
    History history;
    using PreviewTexture=std::shared_ptr<SDL_Texture>;
    struct PreviewLayer {
        PreviewTexture image;
        int width=0,height=0;
        double alpha=1;
    };
    struct PreviewScene {PreviewLayer upper,lower;} timelineScene;
    struct ViewerPreview {
        Preview decoder;
        PreviewTexture image,spare;
        VideoFrame raw;
        std::filesystem::path requestedPath,displayedPath;
        double requestedTime=-1;
        Id requestedClip=0,displayedClip=0;
        std::uint64_t generation=1;
        Grade displayedGrade;
        std::string error;
        void invalidate(){requestedPath.clear();requestedTime=-1;requestedClip=displayedClip=0;displayedPath.clear();error.clear();++generation;}
    } sourcePreview,timelinePreview,underlayPreview;
    Transport transport;
    Exporter exporter;
    ExportSettings exportSettings{VideoCodec::H264,RateControl::Bitrate};
    std::filesystem::path projectPath,exportPath,support;
    std::string iniPath,status="Ready. Import footage to start your first sequence.",error;
    std::future<ImportResult> importing;
    std::vector<std::filesystem::path> importQueue;
    std::map<Id,SDL_Texture*> thumbnails;
    Id selectedClip=0,selectedMedia=0;
    bool sourceMode=false,dirty=false,wasPlaying=false,razor=false,snapping=true,guides=false;
    bool showAbout=false,resetLayout=false,dualViewer=false,restoreViewerMode=false,restoreTimelineTab=false,restoreMediaTab=false;
    int page=2,exportSize=0,viewerPage=-1;
    double playhead=0,sourceTime=0;
    float zoom=55;
    float thumbnailScale=.75f,transitionDuration=.5f;
    bool mediaGrid=false;
    std::optional<Project> dragBefore;
    Id dragged=0;
    Clip dragOriginal;
    int dragMode=0;
    double dragMouseX=0;
    std::chrono::steady_clock::time_point lastAutosave=std::chrono::steady_clock::now();

    Impl(SDL_Window* w,SDL_Renderer* r):window(w),renderer(r){
        support=platform::applicationSupport();iniPath=(support/"layout.ini").string();
        ImGui::GetIO().IniFilename=iniPath.c_str();theme();
        ImGuiSettingsHandler settings;
        settings.TypeName="FrameRender";settings.TypeHash=ImHashStr(settings.TypeName);settings.UserData=this;
        settings.ReadOpenFn=[](ImGuiContext*,ImGuiSettingsHandler* handler,const char* name)->void*{return std::strcmp(name,"MediaPool")==0 || std::strcmp(name,"Viewer")==0?handler->UserData:nullptr;};
        settings.ReadLineFn=[](ImGuiContext*,ImGuiSettingsHandler*,void* entry,const char* line){
            auto& editor=*static_cast<Impl*>(entry);
            float scale=0;if(std::sscanf(line,"ThumbnailScale=%f",&scale)==1 && std::isfinite(scale))editor.thumbnailScale=std::clamp(scale,.45f,1.6f);
            int dual=0;if(std::sscanf(line,"DualViewer=%d",&dual)==1)editor.dualViewer=dual!=0;
            int grid=0;if(std::sscanf(line,"GridView=%d",&grid)==1)editor.mediaGrid=grid!=0;
        };
        settings.WriteAllFn=[](ImGuiContext*,ImGuiSettingsHandler* handler,ImGuiTextBuffer* out){const auto& editor=*static_cast<Impl*>(handler->UserData);out->appendf("[%s][MediaPool]\nThumbnailScale=%.3f\nGridView=%d\n\n[%s][Viewer]\nDualViewer=%d\n\n",handler->TypeName,editor.thumbnailScale,editor.mediaGrid?1:0,handler->TypeName,editor.dualViewer?1:0);};
        ImGui::AddSettingsHandler(&settings);
        ImGui::GetStyle().FontScaleDpi=SDL_GetDisplayContentScale(SDL_GetPrimaryDisplay());
        ImGui::GetIO().ConfigWindowsMoveFromTitleBarOnly=true;
        ImNodes::StyleColorsDark();
        auto& ns=ImNodes::GetStyle();ns.Colors[ImNodesCol_GridBackground]=rgba(23,27,31);ns.Colors[ImNodesCol_TitleBar]=rgba(44,82,75);ns.Colors[ImNodesCol_TitleBarSelected]=rgba(59,115,99);ns.Colors[ImNodesCol_Link]=rgba(99,205,173);
        ImNodes::SetNodeGridSpacePos(1,{70,90});ImNodes::SetNodeGridSpacePos(2,{390,130});
    }
    ~Impl(){
        // Save while the handler's state and the ini filename are still alive.
        ImGui::SaveIniSettingsToDisk(iniPath.c_str());ImGui::RemoveSettingsHandler("FrameRender");ImGui::GetIO().IniFilename=nullptr;
        transport.stop();for(auto& [id,t]:thumbnails){(void)id;SDL_DestroyTexture(t);}
    }
    std::string win(const char* title)const{
        const char* visible=page==2 && std::strcmp(title,"Viewer")==0?"Single viewer":title;
        return std::string(visible)+"###"+title+std::to_string(page);
    }
    void changed(){dirty=true;status="Project modified";}
    void fail(const std::exception& ex){error=ex.what();}
    double& time(){return sourceMode?sourceTime:playhead;}
    double duration()const{const auto* a=project.asset(selectedMedia);return sourceMode?(a?a->duration:0):project.duration();}
    void stop(){if(wasPlaying)time()=transport.position();transport.stop();wasPlaying=false;}
    void invalidatePreview(){sourcePreview.invalidate();timelinePreview.invalidate();underlayPreview.invalidate();}
    void activateViewer(bool source){if(sourceMode!=source){stop();sourceMode=source;}}
    void selectSource(Id id){stop();selectedClip=0;selectedMedia=id;sourceMode=true;sourceTime=0;sourcePreview.invalidate();}
    void seek(double value){stop();time()=std::clamp(snapFrame(value,project.fps),0.0,duration());}
    void play(){
        try{
            if(wasPlaying){stop();return;}
            if(duration()<=0)return;
            if(time()>=duration()-.5/project.fps)time()=0;
            if(sourceMode){
                Project source;const auto* a=project.asset(selectedMedia);if(!a)return;source.fps=project.fps;source.addMedia(*a);source.append(source.media.front().id);transport.play(source,sourceTime);
            }else transport.play(project,playhead);
            wasPlaying=true;
        }catch(const std::exception& ex){fail(ex);}
    }
    void import(const std::vector<std::filesystem::path>& paths){
        if(paths.empty())return;
        if(importing.valid()){importQueue.insert(importQueue.end(),paths.begin(),paths.end());status="Additional files queued for import.";return;}
        status="Reading media...";
        importing=std::async(std::launch::async,[paths]{
            ImportResult result;
            for(const auto& path:paths)try{Imported item;item.media=probe(path);VideoReader reader(path);item.thumbnail=reader.read(std::min(.4,item.media.duration*.2),300);result.assets.push_back(std::move(item));}catch(const std::exception& ex){result.errors+=std::string(ex.what())+"\n";}
            return result;
        });
    }
    void importFinished(){
        if(!importing.valid() || importing.wait_for(std::chrono::seconds(0))!=std::future_status::ready)return;
        auto result=importing.get();
        if(!result.assets.empty()){
            history.checkpoint(project);
            for(auto& item:result.assets){
                if(project.media.empty()){
                    project.width=std::clamp(item.media.width,16,8192)&~1;project.height=std::clamp(item.media.height,16,8192)&~1;
                    project.fps=std::clamp(item.media.fps,1.0,120.0);
                }
                const Id id=project.addMedia(std::move(item.media));
                if(thumbnails.count(id))SDL_DestroyTexture(thumbnails[id]);thumbnails[id]=texture(renderer,item.thumbnail);
                if(!selectedMedia)selectedMedia=id;
            }
            changed();status="Imported "+std::to_string(result.assets.size())+" video file(s). Double-click a clip to add it to the timeline.";
            if(project.clips.empty()){sourceMode=true;sourceTime=0;invalidatePreview();}
        }
        if(!result.errors.empty())error=result.errors;
    }
    void append(Id id,int track=0){
        stop();history.checkpoint(project);
        selectedClip=project.append(id,track);
        if(!selectedClip)return;
        if(selectedMedia!=id)sourceTime=0;
        selectedMedia=id;sourceMode=false;playhead=project.clip(selectedClip)->start;changed();invalidatePreview();
        status="Clip added to V"+std::to_string(track+1);
    }
    void split(){
        stop();sourceMode=false;
        Id id=selectedClip;
        if(!project.clip(id)){if(const auto* c=project.visibleAt(playhead))id=c->id;}
        Project next=project;if(next.split(id,playhead)){history.checkpoint(project);project=std::move(next);changed();status="Clip split at "+timecode(playhead,project.fps);}
        else status="Place the playhead inside the selected clip to split it.";
    }
    void erase(bool ripple=false){stop();if(project.clip(selectedClip)){history.checkpoint(project);project.erase(selectedClip,ripple);selectedClip=0;changed();invalidatePreview();}}
    void undo(bool redo=false){stop();if(redo?history.redo(project):history.undo(project)){changed();invalidatePreview();status=redo?"Redo":"Undo";}}
    bool discard(){return !dirty || platform::confirmDiscard();}
    void clear(){
        stop();project=Project{};history.clear();selectedMedia=selectedClip=0;playhead=sourceTime=0;sourceMode=false;dirty=false;projectPath.clear();
        for(auto& [id,t]:thumbnails){(void)id;SDL_DestroyTexture(t);}thumbnails.clear();invalidatePreview();timelineScene={};status="New project";
    }
    void load(const std::filesystem::path& path){
        try{
            Project loaded=Project::load(path);clear();project=std::move(loaded);projectPath=path;
            if(!project.media.empty())selectedMedia=project.media.front().id;
            status="Opened "+path.filename().string();
            // Thumbnails are regenerated asynchronously; loading the project itself does not edit it.
            thumbnailReload();
        }catch(const std::exception& ex){fail(ex);}
    }
    void thumbnailReload(){
        // Loading uses the same bounded import worker. Existing IDs are retained.
        if(importing.valid())return;
        std::vector<std::filesystem::path> paths;for(const auto& a:project.media)if(std::filesystem::is_regular_file(a.path))paths.push_back(a.path);
        if(!paths.empty()){
            importing=std::async(std::launch::async,[paths]{ImportResult result;for(const auto& p:paths)try{Imported i;i.media=probe(p);VideoReader r(p);i.thumbnail=r.read(std::min(.4,i.media.duration*.2),300);result.assets.push_back(std::move(i));}catch(...){}return result;});
            regenerating=true;
        }
    }
    bool regenerating=false;
    void save(bool saveAs=false){
        try{
            auto path=projectPath;if(path.empty() || saveAs)path=platform::saveProject(project.name);if(path.empty())return;
            project.save(path);projectPath=path;dirty=false;status="Saved "+path.filename().string();
        }catch(const std::exception& ex){fail(ex);}
    }
    void newProject(){if(importing.valid()){status="Wait for the current import to complete.";return;}if(discard())clear();}
    void openProject(){if(importing.valid()){status="Wait for the current import to complete.";return;}if(discard()){const auto path=platform::openProject();if(!path.empty())load(path);}}
    void startExport(){
        try{
            stop();const auto path=platform::saveVideo(project.name,exportExtension(exportSettings.codec));if(path.empty())return;
            int w=project.width,h=project.height;if(exportSize==1){w=1920;h=1080;}if(exportSize==2){w=1280;h=720;}
            exporter.start(project,path,w,h,exportSettings);exportPath=path;status="Exporting "+path.filename().string();
        }catch(const std::exception& ex){fail(ex);}
    }
    void menus(){
        if(ImGui::BeginMainMenuBar()){
            ImGui::TextColored(accent,"FR");ImGui::SameLine();
            if(ImGui::BeginMenu("File")){
                if(ImGui::MenuItem("New project","Cmd+N"))newProject();
                if(ImGui::MenuItem("Open project...","Cmd+O"))openProject();
                if(ImGui::MenuItem("Open recovery project",nullptr,false,std::filesystem::exists(support/"Recovery.frp") && !importing.valid())){if(discard())load(support/"Recovery.frp");}
                ImGui::Separator();if(ImGui::MenuItem("Import media...","Cmd+I",false,!importing.valid())){stop();import(platform::openVideos());}
                ImGui::Separator();if(ImGui::MenuItem("Save project","Cmd+S"))save();if(ImGui::MenuItem("Save project as...","Shift+Cmd+S"))save(true);
                ImGui::EndMenu();
            }
            if(ImGui::BeginMenu("Edit")){
                if(ImGui::MenuItem("Undo","Cmd+Z",false,history.canUndo()))undo();if(ImGui::MenuItem("Redo","Shift+Cmd+Z",false,history.canRedo()))undo(true);
                ImGui::Separator();if(ImGui::MenuItem("Split at playhead","Cmd+B",false,!project.clips.empty()))split();
                if(ImGui::MenuItem("Delete clip","Backspace",false,selectedClip!=0))erase();if(ImGui::MenuItem("Ripple delete","Shift+Backspace",false,selectedClip!=0))erase(true);
                ImGui::EndMenu();
            }
            if(ImGui::BeginMenu("Workspace")){for(const auto& workspace:pages)if(ImGui::MenuItem(workspace.name,nullptr,page==workspace.id))page=workspace.id;ImGui::Separator();if(ImGui::MenuItem("Reset this layout"))resetLayout=true;ImGui::EndMenu();}
            if(ImGui::BeginMenu("Help")){if(ImGui::MenuItem("About FrameRender"))showAbout=true;ImGui::EndMenu();}
            ImGui::EndMainMenuBar();
        }
    }
    void shortcuts(){
        auto& io=ImGui::GetIO();if(io.WantTextInput || ImGui::IsPopupOpen(nullptr,ImGuiPopupFlags_AnyPopupId))return;
        if(io.KeySuper){
            if(ImGui::IsKeyPressed(ImGuiKey_I) && !importing.valid()){stop();import(platform::openVideos());}
            if(ImGui::IsKeyPressed(ImGuiKey_O))openProject();if(ImGui::IsKeyPressed(ImGuiKey_N))newProject();
            if(ImGui::IsKeyPressed(ImGuiKey_S))save(io.KeyShift);
            if(ImGui::IsKeyPressed(ImGuiKey_Z))undo(io.KeyShift);
            if(ImGui::IsKeyPressed(ImGuiKey_B))split();
        }else{
            if(ImGui::IsKeyPressed(ImGuiKey_Space))play();
            if(ImGui::IsKeyPressed(ImGuiKey_LeftArrow))seek(time()-(io.KeyShift?1:1/project.fps));
            if(ImGui::IsKeyPressed(ImGuiKey_RightArrow))seek(time()+(io.KeyShift?1:1/project.fps));
            if(ImGui::IsKeyPressed(ImGuiKey_Home))seek(0);if(ImGui::IsKeyPressed(ImGuiKey_End))seek(duration());
            if(ImGui::IsKeyPressed(ImGuiKey_Backspace) || ImGui::IsKeyPressed(ImGuiKey_Delete))erase(io.KeyShift);
            if(ImGui::IsKeyPressed(ImGuiKey_B))razor=true;if(ImGui::IsKeyPressed(ImGuiKey_V))razor=false;
        }
    }
    void header(const ImVec2& position,float width){
        ImGui::SetNextWindowPos(position);ImGui::SetNextWindowSize({width,54});
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding,{16,10});
        ImGui::Begin("##header",nullptr,ImGuiWindowFlags_NoDecoration|ImGuiWindowFlags_NoMove|ImGuiWindowFlags_NoSavedSettings|ImGuiWindowFlags_NoDocking);
        ImGui::TextUnformatted("FrameRender");ImGui::SameLine();ImGui::TextDisabled(" / ");ImGui::SameLine();ImGui::TextUnformatted(project.name.c_str());if(dirty){ImGui::SameLine();ImGui::TextColored(accent,"*");}
        ImGui::SameLine();ImGui::TextDisabled("   %d x %d  /  %.3g fps",project.width,project.height,project.fps);
        ImGui::SameLine(std::max(550.f,width-290));
        ImGui::BeginDisabled(importing.valid());if(button(importing.valid()?"Importing...":"+ Import media")){stop();import(platform::openVideos());}ImGui::EndDisabled();
        ImGui::SameLine();if(button("Save"))save();
        ImGui::SameLine();if(button("Export",page==6))page=6;
        ImGui::End();ImGui::PopStyleVar();
    }
    float navigationHeight()const{return std::max(32.f,ImGui::GetTextLineHeight()+16);}
    void navigation(const ImVec2& position,float width){
        const float barHeight=navigationHeight(),buttonHeight=ImGui::GetTextLineHeight()+6;
        ImGui::SetNextWindowPos(position);ImGui::SetNextWindowSize({width,barHeight});
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding,{12,(barHeight-buttonHeight)*.5f});
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding,{8,3});
        ImGui::Begin("##pages",nullptr,ImGuiWindowFlags_NoDecoration|ImGuiWindowFlags_NoMove|ImGuiWindowFlags_NoSavedSettings|ImGuiWindowFlags_NoDocking);
        const float count=static_cast<float>(pages.size());
        const float itemWidth=std::min(136.f,(width-40)/count);const float total=itemWidth*count+6*(count-1);
        ImGui::SetCursorPosX(std::max(12.f,(width-total)*.5f));
        for(size_t i=0;i<pages.size();++i){
            const auto& workspace=pages[i];
            ImGui::PushID(workspace.id);if(i)ImGui::SameLine(0,6);
            if(button(workspace.name,page==workspace.id,{itemWidth,buttonHeight}))page=workspace.id;
            if(page==workspace.id){const auto lo=ImGui::GetItemRectMin(),hi=ImGui::GetItemRectMax();ImGui::GetWindowDrawList()->AddRectFilled({lo.x+12,hi.y-2},{hi.x-12,hi.y},ImGui::ColorConvertFloat4ToU32(accent),1);}
            ImGui::PopID();
        }
        ImGui::End();ImGui::PopStyleVar(2);
    }
    void statusbar(const ImVec2& position,float width){
        ImGui::SetNextWindowPos(position);ImGui::SetNextWindowSize({width,26});
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding,{12,4});
        ImGui::Begin("##status",nullptr,ImGuiWindowFlags_NoDecoration|ImGuiWindowFlags_NoMove|ImGuiWindowFlags_NoSavedSettings|ImGuiWindowFlags_NoDocking);
        ImGui::TextDisabled("%s",status.c_str());
        ImGui::SameLine(std::max(600.f,width-245));ImGui::TextDisabled("%s  /  %.0f fps  /  0.1",SDL_GetRendererName(renderer),ImGui::GetIO().Framerate);
        ImGui::End();ImGui::PopStyleVar();
    }
    bool hasTimeline()const{return page==2 || page==4 || page==5;}
    ImGuiID windowDock(const std::string& name)const{
        if(const auto* window=ImGui::FindWindowByName(name.c_str()))return window->DockId;
        if(const auto* settings=ImGui::FindWindowSettingsByID(ImHashStr(name.c_str())))return settings->DockId;
        return 0;
    }
    void attachViewers(){
        if(page!=2)return;
        const ImGuiID dock=windowDock(win("Viewer"));
        auto* node=ImGui::DockBuilderGetNode(dock);if(!node)return;
        node->SetLocalFlags(node->LocalFlags&~(ImGuiDockNodeFlags_HiddenTabBar|ImGuiDockNodeFlags_AutoHideTabBar));
        if(windowDock(win("Dual viewer"))!=dock){
            const ImGuiID root=ImGui::DockNodeGetRootNode(node)->ID;
            ImGui::DockBuilderDockWindow(win("Dual viewer").c_str(),dock);ImGui::DockBuilderFinish(root);
        }
    }
    void attachMediaTabs(){
        if(page!=2)return;
        const auto dock=windowDock(win("Media Pool"));auto* node=ImGui::DockBuilderGetNode(dock);if(!node)return;
        node->SetLocalFlags(node->LocalFlags&~(ImGuiDockNodeFlags_HiddenTabBar|ImGuiDockNodeFlags_AutoHideTabBar));
        bool added=false;
        for(const auto* title:{"Transitions","Text"})if(windowDock(win(title))!=dock){
            ImGui::DockBuilderDockWindow(win(title).c_str(),dock);added=true;
        }
        if(added){ImGui::DockBuilderFinish(ImGui::DockNodeGetRootNode(node)->ID);restoreMediaTab=true;}
    }
    void attachTimeline(ImGuiID root){
        if(!hasTimeline())return;
        const std::string name=win("Timeline");
        ImGuiID dock=0;
        if(const auto* window=ImGui::FindWindowByName(name.c_str()))dock=window->DockId;
        else if(const auto* settings=ImGui::FindWindowSettingsByID(ImHashStr(name.c_str())))dock=settings->DockId;
        auto* node=ImGui::DockBuilderGetNode(dock);
        if(!node || ImGui::DockNodeGetRootNode(node)->ID!=root){
            // Repair older layouts where the floating timeline covered the viewer.
            // Splitting the root preserves the user's other panel subdivisions.
            dock=ImGui::DockBuilderSplitNode(root,ImGuiDir_Down,.36f,nullptr,nullptr);
            ImGui::DockBuilderDockWindow(name.c_str(),dock);ImGui::DockBuilderFinish(root);
            node=ImGui::DockBuilderGetNode(dock);
        }
        if(node)node->SetLocalFlags(node->LocalFlags|ImGuiDockNodeFlags_NoUndocking);
        if(page==2 && node){
            node->SetLocalFlags(node->LocalFlags&~(ImGuiDockNodeFlags_HiddenTabBar|ImGuiDockNodeFlags_AutoHideTabBar));
            if(windowDock(win("Cut"))!=dock){
                ImGui::DockBuilderDockWindow(win("Cut").c_str(),dock);ImGui::DockBuilderFinish(root);
                restoreTimelineTab=true;
            }
        }
    }
    void layout(ImGuiID id,ImVec2 size){
        if(!resetLayout && ImGui::DockBuilderGetNode(id)){attachTimeline(id);attachViewers();attachMediaTabs();return;}
        resetLayout=false;
        ImGui::DockBuilderRemoveNode(id);ImGui::DockBuilderAddNode(id,ImGuiDockNodeFlags_DockSpace);ImGui::DockBuilderSetNodeSize(id,size);
        ImGuiID center=id,left=0,right=0,bottom=0;
        if(page==0){ImGui::DockBuilderSplitNode(center,ImGuiDir_Left,.40f,&left,&center);ImGui::DockBuilderDockWindow(win("Media Pool").c_str(),left);ImGui::DockBuilderDockWindow(win("Viewer").c_str(),center);}
        else if(page==6){ImGui::DockBuilderSplitNode(center,ImGuiDir_Right,.32f,&right,&center);ImGui::DockBuilderDockWindow(win("Render").c_str(),right);ImGui::DockBuilderDockWindow(win("Viewer").c_str(),center);}
        else{
            ImGui::DockBuilderSplitNode(center,ImGuiDir_Down,page==3?.43f:.36f,&bottom,&center);
            ImGui::DockBuilderSplitNode(center,ImGuiDir_Right,.25f,&right,&center);
            if(page==2){ImGui::DockBuilderSplitNode(center,ImGuiDir_Left,.29f,&left,&center);ImGui::DockBuilderDockWindow(win("Media Pool").c_str(),left);}
            ImGui::DockBuilderDockWindow(win("Viewer").c_str(),center);
            ImGui::DockBuilderDockWindow(win(page==4?"Color":page==5?"Mixer":"Inspector").c_str(),right);
            ImGui::DockBuilderDockWindow(win(page==3?"Nodes":"Timeline").c_str(),bottom);
        }
        ImGui::DockBuilderFinish(id);
        attachTimeline(id);attachViewers();attachMediaTabs();
    }
    void mediaPool(){
        if(!ImGui::Begin(win("Media Pool").c_str())){ImGui::End();return;}
        label("PROJECT BIN");ImGui::SameLine();ImGui::TextDisabled("  %zu files",project.media.size());
        const float iconSize=ImGui::GetTextLineHeight()+6;
        for(int grid=0;grid<2;++grid){
            if(grid)ImGui::SameLine(0,4);
            const bool clicked=button(grid?"##grid_view":"##list_view",mediaGrid==(grid!=0),{iconSize,iconSize});
            auto* draw=ImGui::GetWindowDrawList();const auto lo=ImGui::GetItemRectMin();
            const auto color=ImGui::GetColorU32(mediaGrid==(grid!=0)?ImGuiCol_CheckMark:ImGuiCol_Text);
            if(grid){
                for(int y=0;y<2;++y)for(int x=0;x<2;++x){const ImVec2 a{lo.x+5+x*6,lo.y+5+y*6};draw->AddRectFilled(a,{a.x+4,a.y+4},color,1);}
            }else{
                for(int y=0;y<3;++y){const float at=lo.y+5+y*5;draw->AddRectFilled({lo.x+5,at},{lo.x+7,at+2},color);draw->AddLine({lo.x+9,at+1},{lo.x+iconSize-4,at+1},color);}
            }
            if(clicked){mediaGrid=grid!=0;ImGui::MarkIniSettingsDirty();}
            tip(grid?"Thumbnail view":"List view");
        }
        ImGui::SameLine(0,8);label("Thumbnail size");ImGui::SameLine();ImGui::SetNextItemWidth(-1);
        if(minimalSlider("##thumbnail_size",&thumbnailScale,.45f,1.6f,"",ImGuiSliderFlags_AlwaysClamp|ImGuiSliderFlags_NoInput))ImGui::MarkIniSettingsDirty();
        if(ImGui::IsItemHovered())ImGui::SetTooltip("Thumbnail size: %.0f%%",thumbnailScale*100);
        ImGui::Separator();
        ImGui::BeginChild("##media_items",{0,0});
        if(project.media.empty()){
            ImGui::Dummy({0,35});ImGui::TextWrapped("Your story starts here.");label("Import videos or drop them into this window.");ImGui::Dummy({0,8});
            if(button("Import footage...")){stop();import(platform::openVideos());}
        }
        const float available=std::max(1.f,ImGui::GetContentRegionAvail().x);
        const bool grid=mediaGrid;
        const float cardWidth=std::min(available,128*thumbnailScale+12);
        const int columns=grid?std::max(1,static_cast<int>((available+8)/(cardWidth+8))):1;
        int index=0;
            for(const auto& a:project.media){
                if(grid && index%columns!=0)ImGui::SameLine(0,8);
                ++index;ImGui::PushID(static_cast<int>(a.id));
                const bool selected=selectedMedia==a.id;
                const float width=grid?cardWidth:available;
                const auto thumbnail=thumbnails.find(a.id);SDL_Texture* image=thumbnail!=thumbnails.end()?thumbnail->second:nullptr;
                if(grid){
                    const ImVec2 pos=ImGui::GetCursorScreenPos();
                    const float imageHeight=(width-10)*9/16;
                    const float cardHeight=imageHeight+10;
                    ImGui::PushStyleColor(ImGuiCol_Header,{0,0,0,0});
                    ImGui::PushStyleColor(ImGuiCol_HeaderHovered,{0,0,0,0});
                    ImGui::PushStyleColor(ImGuiCol_HeaderActive,{0,0,0,0});
                    ImGui::Selectable("##card",selected,ImGuiSelectableFlags_AllowDoubleClick,{width,cardHeight});
                    ImGui::PopStyleColor(3);
                    auto* draw=ImGui::GetWindowDrawList();
                    drawThumbnail(draw,image,{pos.x+5,pos.y+5},{pos.x+width-5,pos.y+5+imageHeight},selected);

                }else{
                    const float imageWidth=std::min(84*thumbnailScale,std::max(1.f,width-65)),imageHeight=imageWidth*9/16;
                    const float lineHeight=ImGui::GetTextLineHeight(),rowHeight=std::max(imageHeight+12,lineHeight*2+12);
                    const ImVec2 pos=ImGui::GetCursorScreenPos();ImGui::Selectable("##asset",selected,ImGuiSelectableFlags_AllowDoubleClick,{width,rowHeight});
                    auto* draw=ImGui::GetWindowDrawList();
                    drawThumbnail(draw,image,{pos.x+4,pos.y+(rowHeight-imageHeight)*.5f},{pos.x+4+imageWidth,pos.y+(rowHeight+imageHeight)*.5f},selected);
                    const float textX=pos.x+imageWidth+14,textY=pos.y+(rowHeight-lineHeight*2-4)*.5f;
                    draw->PushClipRect({textX,pos.y},{pos.x+width-4,pos.y+rowHeight},true);
                    draw->AddText({textX,textY},rgba(220,226,230),a.name.c_str());
                    draw->AddText({textX,textY+lineHeight+4},rgba(132,143,154),(timecode(a.duration,project.fps)+"  /  "+std::to_string(a.width)+"x"+std::to_string(a.height)).c_str());draw->PopClipRect();
                }
                if(ImGui::IsItemClicked())selectSource(a.id);
                if(ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))append(a.id);
                if(ImGui::BeginDragDropSource()){const Id id=a.id;ImGui::SetDragDropPayload("FR_MEDIA",&id,sizeof(id));ImGui::TextUnformatted(a.name.c_str());ImGui::EndDragDropSource();}
                if(ImGui::BeginPopupContextItem()){
                    if(ImGui::MenuItem("Add to V1"))append(a.id,0);if(ImGui::MenuItem("Add to V2"))append(a.id,1);if(ImGui::MenuItem("Show in Finder"))platform::reveal(a.path);ImGui::EndPopup();
                }
                if(ImGui::IsItemHovered())ImGui::SetTooltip("%s\n%.3g fps%s",a.path.c_str(),a.fps,a.hasAudio?" / audio":" / silent");
                ImGui::PopID();
            }
        ImGui::EndChild();
        ImGui::End();
    }
    const Clip* previewClip(bool underlay=false)const{
        const double at=std::min(playhead,std::max(0.0,project.duration()-.5/project.fps));
        const auto* c=project.visibleAt(at);if(!c)return nullptr;
        if(!underlay)return c;
        if(c->track!=1 || clipOpacity(*c,at)>=1)return nullptr;
        for(const auto& item:project.clips)if(item.track==0 && at>=item.start && at<item.end())return &item;
        return nullptr;
    }
    const Media* previewSource(bool source,double& timestamp,Grade& grade,bool underlay=false)const{
        if(source){const auto* a=project.asset(selectedMedia);if(a)timestamp=std::min(sourceTime,std::max(0.0,a->duration-.5/project.fps));return a;}
        const double at=std::min(playhead,std::max(0.0,project.duration()-.5/project.fps));
        const auto* c=previewClip(underlay);if(!c)return nullptr;
        timestamp=c->in+at-c->start;grade=c->grade;return project.asset(c->media);
    }
    void updatePreview(ViewerPreview& preview,bool source,bool underlay=false){
        double timestamp=0;Grade grade;const auto* a=previewSource(source,timestamp,grade,underlay);
        const auto* clip=source?nullptr:previewClip(underlay);
        const Id clipId=clip?clip->id:0;
        if(a && (clipId!=preview.requestedClip || a->path!=preview.requestedPath || std::abs(timestamp-preview.requestedTime)>=.45/project.fps)){
            if(clipId!=preview.requestedClip || a->path!=preview.requestedPath)preview.invalidate();
            preview.requestedClip=clipId;preview.requestedPath=a->path;preview.requestedTime=timestamp;preview.decoder.request(a->path,timestamp,preview.generation);
        }
        if(!a && !preview.requestedPath.empty())preview.invalidate();
        bool upload=false;
        if(auto result=preview.decoder.take()){
            if(result->generation==preview.generation && a && result->path==a->path){
                preview.error=result->error;
                if(result->error.empty()){preview.raw=std::move(result->frame);preview.displayedPath=result->path;preview.displayedClip=clipId;upload=true;}
            }
        }
        if(a && preview.displayedPath==a->path && preview.displayedClip==clipId && (grade.brightness!=preview.displayedGrade.brightness || grade.contrast!=preview.displayedGrade.contrast || grade.saturation!=preview.displayedGrade.saturation))upload=true;
        if(upload && !preview.raw.rgba.empty()){
            VideoFrame graded=preview.raw;applyGrade(graded,grade);
            // A displayed scene owns its textures until the complete replacement is ready.
            if(preview.image.use_count()>1)std::swap(preview.image,preview.spare);
            float tw=0,th=0;if(preview.image)SDL_GetTextureSize(preview.image.get(),&tw,&th);
            if(!preview.image || preview.image.use_count()>1 || tw!=graded.width || th!=graded.height){
                preview.image=PreviewTexture(SDL_CreateTexture(renderer,SDL_PIXELFORMAT_RGBA32,SDL_TEXTUREACCESS_STREAMING,graded.width,graded.height),SDL_DestroyTexture);
                if(preview.image)SDL_SetTextureScaleMode(preview.image.get(),SDL_SCALEMODE_LINEAR);
            }
            if(!preview.image || !SDL_UpdateTexture(preview.image.get(),nullptr,graded.rgba.data(),graded.width*4))preview.error=SDL_GetError();
            preview.displayedGrade=grade;
        }
    }
    void updateTimelineScene(){
        const auto* upper=previewClip();const auto* lower=previewClip(true);
        if(!upper || !timelinePreview.error.empty() || !underlayPreview.error.empty()){timelineScene={};return;}
        const auto ready=[&](const ViewerPreview& preview,const Clip* clip){
            const auto* media=project.asset(clip->media);
            return media && preview.image && preview.displayedClip==clip->id && preview.displayedPath==media->path;
        };
        // Keep the last complete picture during decoder hand-off, never across real gaps.
        if(!ready(timelinePreview,upper) || (lower && !ready(underlayPreview,lower)))return;
        const double at=std::min(playhead,std::max(0.0,project.duration()-.5/project.fps));
        const auto layer=[&](const ViewerPreview& preview,const Clip& clip){return PreviewLayer{preview.image,preview.raw.width,preview.raw.height,clipOpacity(clip,at)};};
        timelineScene.upper=layer(timelinePreview,*upper);
        timelineScene.lower=lower?layer(underlayPreview,*lower):PreviewLayer{};
    }
    void updatePreviews(){
        if((dualViewer && page==2) || sourceMode)updatePreview(sourcePreview,true);
        if((dualViewer && page==2) || !sourceMode){updatePreview(timelinePreview,false);updatePreview(underlayPreview,false,true);updateTimelineScene();}
    }
    void viewerPane(bool source){
        auto& preview=source?sourcePreview:timelinePreview;
        if(ImGui::IsWindowHovered() && ImGui::IsMouseClicked(ImGuiMouseButton_Left))activateViewer(source);
        if(ImGui::BeginPopupContextWindow("##monitor_options")){
            ImGui::MenuItem("Guides",nullptr,&guides);ImGui::EndPopup();
        }
        const bool active=sourceMode==source;
        double timestamp=0;Grade grade;const auto* a=previewSource(source,timestamp,grade);
        auto* draw=ImGui::GetWindowDrawList();
        const ImVec2 pos=ImGui::GetCursorScreenPos(),avail=ImGui::GetContentRegionAvail();
        const double monitorTime=source?sourceTime:playhead;
        const std::string clock=timecode(monitorTime,project.fps);
        const float spacing=std::min(6.f,avail.x*.02f);
        const float playWidth=50,sideWidth=std::min(24.f,std::max(12.f,(avail.x-playWidth-4*spacing)/4));
        const float controlsWidth=4*sideWidth+playWidth+4*spacing;
        const float clockWidth=ImGui::CalcTextSize(clock.c_str()).x+spacing;
        const bool compact=avail.x<clockWidth+controlsWidth;
        const float scrubHeight=10,verticalSpacing=ImGui::GetStyle().ItemSpacing.y;
        const float controlsHeight=scrubHeight+ImGui::GetFrameHeight()+2*verticalSpacing+(compact?ImGui::GetTextLineHeight()+verticalSpacing:0);
        std::string previewError=preview.error;
        if(!source && !underlayPreview.error.empty())previewError+=(previewError.empty()?"":"\n")+underlayPreview.error;
        const float errorHeight=previewError.empty()?0.f:std::max(0.f,std::min(ImGui::GetTextLineHeight()*3,avail.y-controlsHeight-32));
        const float footerHeight=controlsHeight+(errorHeight>0?errorHeight+ImGui::GetStyle().ItemSpacing.y:0);
        const ImVec2 canvas{std::max(1.f,avail.x),std::max(1.f,avail.y-footerHeight)};
        draw->PushClipRect(pos,{pos.x+canvas.x,pos.y+canvas.y},true);
        draw->AddRectFilled(pos,{pos.x+canvas.x,pos.y+canvas.y},rgba(12,14,17),3);
        const bool valid=a && preview.error.empty() && (source?(preview.image && preview.displayedPath==a->path):static_cast<bool>(timelineScene.upper.image));
        if(valid){
            const float aspect=source?static_cast<float>(a->width)/a->height:static_cast<float>(project.width)/project.height;
            const float padding=std::min(4.f,std::min(canvas.x,canvas.y)*.1f);
            float w=canvas.x-2*padding,h=w/aspect;if(h>canvas.y-2*padding){h=canvas.y-2*padding;w=h*aspect;}
            const ImVec2 lo{pos.x+(canvas.x-w)*.5f,pos.y+(canvas.y-h)*.5f};
            draw->AddRectFilled(lo,{lo.x+w,lo.y+h},rgba(0,0,0));
            const auto drawLayer=[&](const PreviewLayer& layer){
                if(!layer.image || layer.width<=0 || layer.height<=0)return;
                const float videoAspect=static_cast<float>(layer.width)/layer.height;
                float vw=w,vh=w/videoAspect;if(vh>h){vh=h;vw=h*videoAspect;}
                const ImVec2 vl{lo.x+(w-vw)*.5f,lo.y+(h-vh)*.5f};
                const int opacity=static_cast<int>(std::clamp(layer.alpha,0.0,1.0)*255);
                // Fade the letterbox with the picture, without blending twice inside it.
                const auto black=rgba(0,0,0,opacity);
                draw->AddRectFilled(lo,{vl.x,lo.y+h},black);
                draw->AddRectFilled({vl.x+vw,lo.y},{lo.x+w,lo.y+h},black);
                draw->AddRectFilled({vl.x,lo.y},{vl.x+vw,vl.y},black);
                draw->AddRectFilled({vl.x,vl.y+vh},{vl.x+vw,lo.y+h},black);
                draw->AddImage(ImTextureRef(layer.image.get()),vl,{vl.x+vw,vl.y+vh},{0,0},{1,1},rgba(255,255,255,opacity));
            };
            if(source)drawLayer({preview.image,preview.raw.width,preview.raw.height,1});
            else{drawLayer(timelineScene.lower);drawLayer(timelineScene.upper);}
            if(guides){
                draw->AddRect({lo.x+w*.05f,lo.y+h*.05f},{lo.x+w*.95f,lo.y+h*.95f},rgba(255,255,255,80));
                draw->AddRect({lo.x+w*.1f,lo.y+h*.1f},{lo.x+w*.9f,lo.y+h*.9f},rgba(255,255,255,50));
                draw->AddLine({lo.x+w*.5f-7,lo.y+h*.5f},{lo.x+w*.5f+7,lo.y+h*.5f},rgba(255,255,255,100));
                draw->AddLine({lo.x+w*.5f,lo.y+h*.5f-7},{lo.x+w*.5f,lo.y+h*.5f+7},rgba(255,255,255,100));
            }
        }else{
            const char* message=!previewError.empty()?"Media unavailable":a?"Preparing preview...":source?"Select a source video":"No clip at this position";
            const ImVec2 text=ImGui::CalcTextSize(message);draw->AddText({pos.x+std::max(4.f,(canvas.x-text.x)*.5f),pos.y+canvas.y*.46f},rgba(128,143,153),message);
        }
        if(active)draw->AddRect(pos,{pos.x+canvas.x,pos.y+canvas.y},rgba(92,204,173,130),3);
        draw->PopClipRect();ImGui::Dummy(canvas);
        const double monitorDuration=source?(a?a->duration:0):project.duration();
        const auto monitorSeek=[&](double value){activateViewer(source);seek(value);};
        ImGui::BeginDisabled(monitorDuration<=0);
        const ImVec2 scrubPos=ImGui::GetCursorScreenPos();
        ImGui::InvisibleButton("##scrub",{canvas.x,scrubHeight});
        const bool scrubbing=ImGui::IsItemActive() && ImGui::IsMouseDown(ImGuiMouseButton_Left);
        const float radius=3,trackStart=scrubPos.x+radius,trackEnd=scrubPos.x+std::max(radius+1,canvas.x-radius);
        if(scrubbing){
            const double fraction=std::clamp(static_cast<double>((ImGui::GetIO().MousePos.x-trackStart)/(trackEnd-trackStart)),0.0,1.0);
            monitorSeek(fraction*monitorDuration);
        }
        const float fraction=monitorDuration>0?static_cast<float>(std::clamp((source?sourceTime:playhead)/monitorDuration,0.0,1.0)):0;
        const float knob=trackStart+(trackEnd-trackStart)*fraction,y=scrubPos.y+scrubHeight*.5f;
        sliderTrack(draw,trackStart,trackEnd,y,knob,ImGui::IsItemHovered() || scrubbing);
        if(ImGui::IsItemHovered()){
            ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
            const double at=std::clamp(static_cast<double>((ImGui::GetIO().MousePos.x-trackStart)/(trackEnd-trackStart)),0.0,1.0)*monitorDuration;
            ImGui::SetTooltip("%s",timecode(at,project.fps).c_str());
        }
        if(!compact)ImGui::AlignTextToFramePadding();
        ImGui::TextColored(active?accent:muted,"%s",clock.c_str());
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing,{spacing,ImGui::GetStyle().ItemSpacing.y});
        if(!compact)ImGui::SameLine(ImGui::GetWindowContentRegionMin().x+canvas.x-controlsWidth);
        else ImGui::SetCursorPosX(ImGui::GetCursorPosX()+std::max(0.f,(canvas.x-controlsWidth)*.5f));
        if(button("|<",false,{sideWidth,0}))monitorSeek(0);tip("Go to start (Home)");ImGui::SameLine();
        if(button("<",false,{sideWidth,0}))monitorSeek(monitorTime-1/project.fps);tip("Previous frame (Left arrow)");ImGui::SameLine();
        const bool playing=wasPlaying && sourceMode==source;
        if(button(playing?"Pause":"Play",playing,{playWidth,0})){activateViewer(source);play();}tip("Play / pause (Space)");ImGui::SameLine();
        if(button(">",false,{sideWidth,0}))monitorSeek(monitorTime+1/project.fps);tip("Next frame (Right arrow)");ImGui::SameLine();
        if(button(">|",false,{sideWidth,0}))monitorSeek(monitorDuration);
        ImGui::PopStyleVar();ImGui::EndDisabled();
        if(errorHeight>0){ImGui::BeginChild("##preview_error",{0,errorHeight});ImGui::TextWrapped("%s",previewError.c_str());ImGui::EndChild();}
    }
    bool viewerWindow(bool dual){
        ImGui::SetNextWindowScroll({0,0});
        const bool visible=ImGui::Begin(win(dual?"Dual viewer":"Viewer").c_str(),nullptr,ImGuiWindowFlags_NoScrollbar|ImGuiWindowFlags_NoScrollWithMouse);
        if(visible){
            if(ImGui::BeginPopupContextWindow("##viewer_options")){
                ImGui::MenuItem("Guides",nullptr,&guides);ImGui::EndPopup();
            }
            const float width=std::max(1.f,ImGui::GetContentRegionAvail().x);
            const ImGuiWindowFlags flags=ImGuiWindowFlags_NoScrollbar|ImGuiWindowFlags_NoScrollWithMouse;
            if(dual){
                const float gap=ImGui::GetStyle().ItemSpacing.x,paneWidth=std::max(1.f,(width-gap)*.5f);
                if(ImGui::BeginChild("##source_monitor",{paneWidth,0},0,flags))viewerPane(true);ImGui::EndChild();ImGui::SameLine(0,gap);
                if(ImGui::BeginChild("##timeline_monitor",{paneWidth,0},0,flags))viewerPane(false);ImGui::EndChild();
            }else{
                if(ImGui::BeginChild("##single_monitor",{0,0},0,flags))viewerPane(sourceMode);ImGui::EndChild();
            }
        }
        ImGui::End();return visible;
    }
    void viewer(){
        const bool changingPage=viewerPage!=page;viewerPage=page;
        if(changingPage)restoreViewerMode=page==2;
        // Both views are windows in the same dock tab bar; there is no second row of tabs.
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding,{6,4});
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding,{5,2});
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing,{6,4});
        if(page==2){
            bool selectedDual=dualViewer;
            if(viewerWindow(false))selectedDual=false;
            if(viewerWindow(true))selectedDual=true;
            if(restoreViewerMode){
                auto* target=ImGui::FindWindowByName(win(dualViewer?"Dual viewer":"Viewer").c_str());
                if(target && target->DockTabIsVisible)restoreViewerMode=false;
                else if(target && target->DockNode && target->DockNode->TabBar){
                    auto* bar=target->DockNode->TabBar;
                    if(auto* tab=ImGui::TabBarFindTabByID(bar,target->TabId))ImGui::TabBarQueueFocus(bar,tab);
                }
            }else if(dualViewer!=selectedDual){dualViewer=selectedDual;ImGui::MarkIniSettingsDirty();}
        }else viewerWindow(false);
        ImGui::PopStyleVar(3);
        if(auto* node=ImGui::DockBuilderGetNode(windowDock(win("Viewer")))){
            if(ImGui::DockNodeBeginAmendTabBar(node)){
                if(ImGui::TabItemButton("Guides",ImGuiTabItemFlags_Trailing))guides=!guides;
                tip("Toggle safe area guides");ImGui::DockNodeEndAmendTabBar();
            }
        }
    }
    void inspector(){
        ImGui::Begin(win("Inspector").c_str());
        auto* c=project.clip(selectedClip);const auto* a=c?project.asset(c->media):project.asset(selectedMedia);
        if(!a){label("PROJECT");projectSettings();ImGui::End();return;}
        ImGui::TextWrapped("%s",a->name.c_str());label(c?"TIMELINE CLIP":"SOURCE MEDIA");ImGui::Separator();
        ImGui::Text("%d x %d",a->width,a->height);ImGui::TextDisabled("%.3g fps  /  %s",a->fps,a->hasAudio?"Video + audio":"Video only");
        ImGui::TextDisabled("Duration  %s",timecode(a->duration,project.fps).c_str());
        if(!c){ImGui::Dummy({0,10});if(button("+ Add to timeline",true))append(a->id);ImGui::Separator();projectSettings();ImGui::End();return;}
        ImGui::Dummy({0,8});label("TIMING");
        label("Start");ImGui::SetNextItemWidth(-1);double start=c->start;
        if(ImGui::InputDouble("##start",&start,1/project.fps,1,"%.3f",ImGuiInputTextFlags_EnterReturnsTrue)){
            stop();Project next=project;if(next.move(c->id,start,c->track)){history.checkpoint(project);project=std::move(next);changed();}else status="That position overlaps another clip.";
        }
        c=project.clip(selectedClip);if(!c){ImGui::End();return;}
        double in=c->in,length=c->duration;
        label("Source in");ImGui::SetNextItemWidth(-1);bool timing=ImGui::InputDouble("##sourcein",&in,1/project.fps,1,"%.3f",ImGuiInputTextFlags_EnterReturnsTrue);
        label("Duration");ImGui::SetNextItemWidth(-1);timing|=ImGui::InputDouble("##duration",&length,1/project.fps,1,"%.3f",ImGuiInputTextFlags_EnterReturnsTrue);
        if(timing){stop();Project next=project;if(next.trim(c->id,in,length)){history.checkpoint(project);project=std::move(next);changed();invalidatePreview();}else status="Trim exceeds the source or overlaps another clip.";}
        c=project.clip(selectedClip);if(!c){ImGui::End();return;}
        ImGui::Dummy({0,7});label("AUDIO");
        float gain=c->gain;label("Gain");ImGui::SetNextItemWidth(-1);
        const bool gainChanged=minimalSlider("##gain",&gain,0,2,"%.2fx");
        if(ImGui::IsItemActivated()){stop();history.checkpoint(project);}if(gainChanged){c->gain=gain;changed();}
        bool mute=c->muted;if(ImGui::Checkbox("Mute clip",&mute)){stop();history.checkpoint(project);c->muted=mute;changed();}
        ImGui::Dummy({0,8});ImGui::Separator();if(button("Split at playhead"))split();if(button("Delete clip"))erase();
        ImGui::End();
    }
    void applyTransition(int type){
        if(!project.clip(selectedClip)){status="Select a timeline clip first.";return;}
        Project next=project;const auto* current=next.clip(selectedClip);bool applied=false;
        if(type==0)applied=next.setFades(selectedClip,transitionDuration,current->fadeOut);
        else if(type==1)applied=next.setFades(selectedClip,current->fadeIn,transitionDuration);
        else if(type==2)applied=next.crossDissolve(selectedClip);
        else applied=next.setFades(selectedClip,0,0);
        if(!applied){status="Cross Dissolve needs a partial overlap between clips on V1 and V2.";return;}
        Id edited=0;
        for(size_t i=0;i<next.clips.size();++i)if(next.clips[i].fadeIn!=project.clips[i].fadeIn || next.clips[i].fadeOut!=project.clips[i].fadeOut)edited=next.clips[i].id;
        if(!edited){status=type==3?"No video transitions to remove.":"The transition is already applied.";return;}
        stop();history.checkpoint(project);project=std::move(next);selectedClip=edited;sourceMode=false;changed();invalidatePreview();
        status=type==3?"Video transitions removed.":"Video transition applied.";
    }
    void transitions(){
        if(ImGui::Begin(win("Transitions").c_str())){
            label("VIDEO TRANSITIONS");
            const auto* c=project.clip(selectedClip);
            if(!c)ImGui::TextWrapped("Select a timeline clip to apply a transition.");
            label("Fade duration");ImGui::SetNextItemWidth(-1);
            minimalSlider("##transition_duration",&transitionDuration,static_cast<float>(1/project.fps),2,"%.2f s",ImGuiSliderFlags_AlwaysClamp);
            ImGui::BeginDisabled(!c);
            const float width=ImGui::GetContentRegionAvail().x;
            if(button("Fade In",false,{width,0}))applyTransition(0);
            tip("Fade the selected clip in from black or the video below.");
            if(button("Fade Out",false,{width,0}))applyTransition(1);
            tip("Fade the selected clip out to black or the video below.");
            if(button("Cross Dissolve",false,{width,0}))applyTransition(2);
            tip("Blend two partially overlapping clips on V1 and V2 for the full overlap duration.");
            if(button("Remove Transitions",false,{width,0}))applyTransition(3);
            ImGui::EndDisabled();
            ImGui::Separator();
            ImGui::TextWrapped("Cross Dissolve uses the existing overlap between two clips. Audio is unchanged.");
            c=project.clip(selectedClip);
            if(c){
                double in=c->fadeIn,out=c->fadeOut;
                label("Fade in");ImGui::SetNextItemWidth(-1);
                bool edited=ImGui::InputDouble("##fade_in",&in,1/project.fps,.2,"%.3f s",ImGuiInputTextFlags_EnterReturnsTrue);
                label("Fade out");ImGui::SetNextItemWidth(-1);
                edited|=ImGui::InputDouble("##fade_out",&out,1/project.fps,.2,"%.3f s",ImGuiInputTextFlags_EnterReturnsTrue);
                if(edited && (in!=c->fadeIn || out!=c->fadeOut)){
                    Project next=project;
                    if(next.setFades(selectedClip,in,out)){stop();sourceMode=false;history.checkpoint(project);project=std::move(next);changed();invalidatePreview();}
                    else status="Fade durations must be finite and non-negative.";
                }
            }
        }
        ImGui::End();
    }
    void textPanel(){
        if(ImGui::Begin(win("Text").c_str()))ImGui::TextDisabled("Text tools are coming next.");
        ImGui::End();
        if(restoreMediaTab){
            auto* target=ImGui::FindWindowByName(win("Media Pool").c_str());
            if(target && target->DockTabIsVisible)restoreMediaTab=false;
            else if(target && target->DockNode && target->DockNode->TabBar){
                auto* bar=target->DockNode->TabBar;
                if(auto* tab=ImGui::TabBarFindTabByID(bar,target->TabId))ImGui::TabBarQueueFocus(bar,tab);
            }
        }
    }
    void projectSettings(){
        std::array<char,256> name{};std::snprintf(name.data(),name.size(),"%s",project.name.c_str());label("Project name");ImGui::SetNextItemWidth(-1);
        const bool renamed=ImGui::InputText("##projectname",name.data(),name.size());
        if(ImGui::IsItemActivated())history.checkpoint(project);
        if(renamed){project.name=name.data();changed();}
        ImGui::TextDisabled("%d x %d  /  %.3g fps",project.width,project.height,project.fps);
        ImGui::TextWrapped("Project format follows the first imported video. Original files remain untouched.");
    }
    void timeline(){
        if(!ImGui::Begin(win("Timeline").c_str())){ImGui::End();return;}
        if(button("Select",!razor))razor=false;tip("Selection tool (V)");ImGui::SameLine();if(button("Blade",razor))razor=true;tip("Blade tool (B)");
        ImGui::SameLine();if(button("Snap",snapping))snapping=!snapping;ImGui::SameLine();if(button("Split"))split();
        ImGui::SameLine();ImGui::BeginDisabled(!selectedClip);if(button("Delete"))erase();ImGui::EndDisabled();
        ImGui::SameLine();if(button("Fit")){zoom=std::clamp((ImGui::GetContentRegionAvail().x+400)/static_cast<float>(std::max(5.0,project.duration())),8.f,200.f);}
        ImGui::SameLine();ImGui::SetNextItemWidth(105);minimalSlider("##zoom",&zoom,8,240,"%.0f px/s",ImGuiSliderFlags_Logarithmic);
        ImGui::SameLine();ImGui::TextDisabled("%s",timecode(project.duration(),project.fps).c_str());
        ImGui::Separator();
        const float height=std::max(125.f,ImGui::GetContentRegionAvail().y-17);
        const float row=std::max(25.f,(height-30)/4);
        ImGui::BeginChild("track labels",{70,height},ImGuiChildFlags_None,ImGuiWindowFlags_NoScrollbar);
        ImGui::Dummy({0,25});
        const char* names[]={"V2","V1","A1","A2"};for(int i=0;i<4;++i){ImGui::TextColored(i<2?accent:ImVec4(.68f,.63f,.86f,1),"%s",names[i]);ImGui::SameLine();ImGui::TextDisabled(i<2?"Video":"Audio");ImGui::Dummy({0,std::max(0.f,row-26)});}
        ImGui::EndChild();ImGui::SameLine(0,0);
        ImGui::BeginChild("timeline canvas",{0,height+17},ImGuiChildFlags_None,ImGuiWindowFlags_HorizontalScrollbar);
        const ImVec2 origin=ImGui::GetCursorScreenPos();
        const float width=std::max(ImGui::GetContentRegionAvail().x,static_cast<float>(std::max(15.0,project.duration()+3))*zoom);
        ImGui::InvisibleButton("##canvas",{width,height},ImGuiButtonFlags_MouseButtonLeft|ImGuiButtonFlags_MouseButtonRight);
        const bool hovered=ImGui::IsItemHovered();auto* draw=ImGui::GetWindowDrawList();
        draw->AddRectFilled(origin,{origin.x+width,origin.y+height},rgba(24,27,31));
        draw->AddRectFilled(origin,{origin.x+width,origin.y+27},rgba(31,35,40));
        for(int i=0;i<4;++i){const float y=origin.y+29+i*row;draw->AddRectFilled({origin.x,y},{origin.x+width,y+row-2},i<2?rgba(29,33,38):rgba(28,30,37));}
        const double tick=zoom>90?1:zoom>35?2:zoom>16?5:10;
        const double visibleStart=std::max(0.f,(-origin.x+ImGui::GetWindowPos().x)/zoom);
        const double visibleEnd=visibleStart+ImGui::GetWindowWidth()/zoom+tick;
        for(double t=std::floor(visibleStart/tick)*tick;t<visibleEnd;t+=tick){
            const float x=origin.x+static_cast<float>(t)*zoom;draw->AddLine({x,origin.y+22},{x,origin.y+height},rgba(55,62,70),1);
            auto text=timecode(t,project.fps);text=text.substr(3);draw->AddText({x+5,origin.y+5},rgba(145,154,165),text.c_str());
        }
        const auto mouse=ImGui::GetIO().MousePos;
        Id hit=0;int hitMode=0;
        for(const auto& c:project.clips){
            const auto* a=project.asset(c.media);if(!a)continue;
            const float x=origin.x+static_cast<float>(c.start)*zoom,w=static_cast<float>(c.duration)*zoom;
            if(x+w<ImGui::GetWindowPos().x || x>ImGui::GetWindowPos().x+ImGui::GetWindowWidth())continue;
            for(int audio=0;audio<2;++audio){
                if(audio && !a->hasAudio)continue;
                const int lane=audio?2+c.track:1-c.track;
                const float y=origin.y+30+lane*row;
                const ImVec2 lo{x+1,y+2},hi{x+w-1,y+row-4};const bool selected=c.id==selectedClip;
                const auto color=audio?(c.muted?rgba(66,60,76):rgba(91,76,126)):(c.track==0?rgba(51,108,103):rgba(66,105,131));
                const auto thumbnail=thumbnails.find(a->id);
                SDL_Texture* image=!audio && thumbnail!=thumbnails.end()?thumbnail->second:nullptr;
                drawTimelineClip(draw,image,a->name.c_str(),lo,hi,color,selected);
                if(!audio && hi.x>lo.x && hi.y>lo.y){
                    draw->PushClipRect(lo,hi,true);
                    if(c.fadeIn>0){const float edge=std::min(hi.x,lo.x+static_cast<float>(c.fadeIn)*zoom);draw->AddTriangleFilled(lo,{edge,lo.y},{lo.x,hi.y},rgba(7,15,19,110));draw->AddLine({lo.x,hi.y},{edge,lo.y},rgba(222,238,236,180));}
                    if(c.fadeOut>0){const float edge=std::max(lo.x,hi.x-static_cast<float>(c.fadeOut)*zoom);draw->AddTriangleFilled({edge,lo.y},hi,{hi.x,lo.y},rgba(7,15,19,110));draw->AddLine({edge,lo.y},hi,rgba(222,238,236,180));}
                    draw->PopClipRect();
                }
                if(hovered && mouse.x>=lo.x && mouse.x<=hi.x && mouse.y>=lo.y && mouse.y<=hi.y){hit=c.id;hitMode=mouse.x-lo.x<7?1:hi.x-mouse.x<7?2:0;}
            }
        }
        const float px=origin.x+static_cast<float>(playhead)*zoom;
        draw->AddLine({px,origin.y},{px,origin.y+height},rgba(237,128,94),1.5f);
        draw->AddTriangleFilled({px-6,origin.y},{px+6,origin.y},{px,origin.y+8},rgba(237,128,94));
        if(project.clips.empty()){const char* text="Drop a video here to start your sequence";draw->AddText({origin.x+35,origin.y+65},rgba(105,119,130),text);}
        if(hovered && hitMode)ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);
        const double mouseTime=std::max(0.0,static_cast<double>(mouse.x-origin.x)/zoom);
        if(hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left)){
            stop();sourceMode=false;
            if(mouse.y<origin.y+29 || !hit){playhead=std::clamp(snapFrame(mouseTime,project.fps),0.0,project.duration());if(!hit)selectedClip=0;}
            else{
                selectedClip=hit;
                if(razor){playhead=snapFrame(mouseTime,project.fps);split();}
                else{dragged=hit;dragBefore=project;dragOriginal=*project.clip(hit);dragMode=hitMode;dragMouseX=mouse.x;}
            }
            timelinePreview.invalidate();
        }
        if(dragged && ImGui::IsMouseDragging(ImGuiMouseButton_Left,3)){
            Project candidate=project;auto* c=candidate.clip(dragged);
            const double delta=snapFrame((mouse.x-dragMouseX)/zoom,project.fps);
            if(c){
                *c=dragOriginal;
                if(dragMode==0){
                    double at=std::max(0.0,dragOriginal.start+delta);
                    if(snapping){
                        std::vector<double> edges{0,playhead};for(const auto& v:project.clips)if(v.id!=dragged){edges.push_back(v.start);edges.push_back(v.end());}
                        double closest=8/zoom;
                        for(double e:edges)for(double offset:{0.0,dragOriginal.duration})if(std::abs(at+offset-e)<closest){closest=std::abs(at+offset-e);at=e-offset;}
                    }
                    int track=dragOriginal.track;const int lane=static_cast<int>((mouse.y-origin.y-30)/row);if(lane==0 || lane==1)track=1-lane;
                    if(candidate.move(dragged,at,track))project=std::move(candidate);
                }else{
                    if(dragMode==1){c->start=dragOriginal.start+delta;c->in=dragOriginal.in+delta;c->duration=dragOriginal.duration-delta;}
                    else c->duration=dragOriginal.duration+delta;
                    fitFades(*c);
                    const auto* a=candidate.asset(c->media);
                    if(c->in>=0 && c->duration>=1/project.fps && c->in+c->duration<=a->duration+1e-6 && candidate.canPlace(c->track,c->start,c->duration,c->id))project=std::move(candidate);
                }
            }
        }
        if(dragged && ImGui::IsMouseReleased(ImGuiMouseButton_Left)){
            const auto* c=project.clip(dragged);
            if(c && dragBefore && (c->start!=dragOriginal.start || c->in!=dragOriginal.in || c->duration!=dragOriginal.duration || c->track!=dragOriginal.track)){history.checkpoint(*dragBefore);changed();invalidatePreview();}
            dragged=0;dragBefore.reset();
        }
        if(ImGui::BeginDragDropTarget()){
            if(const auto* payload=ImGui::AcceptDragDropPayload("FR_MEDIA")){
                const Id id=*static_cast<const Id*>(payload->Data);int lane=static_cast<int>((mouse.y-origin.y-30)/row);const int track=lane==0?1:0;
                append(id,track);if(auto* c=project.clip(selectedClip)){project.move(c->id,snapFrame(mouseTime,project.fps),track);playhead=project.clip(selectedClip)->start;}
            }
            ImGui::EndDragDropTarget();
        }
        ImGui::EndChild();ImGui::End();
    }
    void cutOverview(){
        const ImVec2 available=ImGui::GetContentRegionAvail();
        if(available.x<1 || available.y<8)return;
        const float ruler=ImGui::GetTextLineHeight()+6;
        const float height=std::min(available.y,ruler+ImGui::GetTextLineHeight()+14);
        const float width=available.x;
        const ImVec2 origin=ImGui::GetCursorScreenPos(),end{origin.x+width,origin.y+height};
        ImGui::InvisibleButton("##cut_overview",{width,height},ImGuiButtonFlags_MouseButtonLeft);
        const bool hovered=ImGui::IsItemHovered(),active=ImGui::IsItemActive();
        auto* draw=ImGui::GetWindowDrawList();
        draw->PushClipRect(origin,end,true);
        draw->AddRectFilled(origin,end,rgba(24,27,31));
        const double length=project.duration();
        if(length<=0){
            const char* text="Add clips from the Media Pool to start your sequence";
            draw->AddText({origin.x+8,origin.y+(height-ImGui::GetTextLineHeight())*.5f},rgba(130,140,153),text);
            draw->PopClipRect();return;
        }
        // Fit actual project time, including gaps, without changing the editing zoom.
        const auto xAt=[&](double time){return origin.x+static_cast<float>(std::clamp(time/length,0.0,1.0))*width;};
        const float timeWidth=ImGui::CalcTextSize(timecode(length,project.fps).c_str()).x;
        const double rawTick=length/std::max(1.f,std::floor(width/(timeWidth+40)));
        const double power=std::pow(10.0,std::floor(std::log10(rawTick)));
        const double normalized=rawTick/power;
        const double tick=(normalized<=1?1:normalized<=2?2:normalized<=5?5:10)*power;
        for(double t=0;t<length;t+=tick){
            const float x=xAt(t);
            if(x+timeWidth+12>end.x-timeWidth-8 && t>0)break;
            draw->AddLine({x,origin.y+ruler-4},{x,origin.y+ruler},rgba(77,85,96));
            draw->AddText({x+3,origin.y},rgba(145,154,165),timecode(t,project.fps).c_str());
        }
        if(width>timeWidth*2+16)
            draw->AddText({end.x-timeWidth-3,origin.y},rgba(145,154,165),timecode(length,project.fps).c_str());
        const float top=std::min(end.y,origin.y+ruler);
        const ImVec2 mouse=ImGui::GetIO().MousePos;
        const double at=std::clamp(static_cast<double>((mouse.x-origin.x)/width),0.0,1.0)*length;
        Id hit=0;
        for(const auto& segment:cutOverviewSegments(project)){
            if(!segment.clip)continue;
            const auto& c=*segment.clip;
            const auto* a=project.asset(c.media);if(!a)continue;
            const ImVec2 lo{xAt(segment.start),top},hi{xAt(segment.end),end.y-3};
            const ImU32 color=c.track==0?rgba(51,108,103):rgba(66,105,131);
            if(hi.x-lo.x>=2){
                const auto image=thumbnails.find(a->id);
                drawTimelineClip(draw,image!=thumbnails.end()?image->second:nullptr,a->name.c_str(),lo,hi,color,c.id==selectedClip);
            }else if(hi.y>lo.y)
                draw->AddLine(lo,{lo.x,hi.y},c.id==selectedClip?rgba(165,241,213):color);
            if(at>=segment.start && at<segment.end && mouse.y>=lo.y && mouse.y<hi.y)hit=c.id;
        }
        if(hovered || active)ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
        if(active && ImGui::IsMouseDown(ImGuiMouseButton_Left)){
            activateViewer(false);seek(at);
            if(mouse.y>=top && mouse.y<end.y)selectedClip=hit;
        }
        const float inset=std::min(.75f,width*.5f);
        const float px=std::clamp(xAt(playhead),origin.x+inset,end.x-inset);
        draw->AddLine({px,origin.y},{px,end.y},rgba(237,128,94),1.5f);
        draw->AddTriangleFilled({px-4,origin.y},{px+4,origin.y},{px,origin.y+6},rgba(237,128,94));
        draw->PopClipRect();
        if(hovered && !active && hit){
            const auto* c=project.clip(hit);const auto* a=c?project.asset(c->media):nullptr;
            if(a)ImGui::SetTooltip("%s",a->name.c_str());
        }
    }
    void cut(){
        if(ImGui::Begin(win("Cut").c_str(),nullptr,ImGuiWindowFlags_NoScrollbar|ImGuiWindowFlags_NoScrollWithMouse))
            cutOverview();
        ImGui::End();
        // Keep Timeline selected when first adding the Cut dock tab.
        if(restoreTimelineTab){
            auto* target=ImGui::FindWindowByName(win("Timeline").c_str());
            if(target && target->DockTabIsVisible)restoreTimelineTab=false;
            else if(target && target->DockNode && target->DockNode->TabBar){
                auto* bar=target->DockNode->TabBar;
                if(auto* tab=ImGui::TabBarFindTabByID(bar,target->TabId))ImGui::TabBarQueueFocus(bar,tab);
            }
        }
    }
    void nodes(){
        ImGui::Begin(win("Nodes").c_str());
        ImGui::TextDisabled("Compositing workspace  /  pass-through preview");tip("The first build includes the node workspace. Effect evaluation is still to be implemented.");
        ImNodes::BeginNodeEditor();
        ImNodes::BeginNode(1);ImNodes::BeginNodeTitleBar();ImGui::TextUnformatted("Media In");ImNodes::EndNodeTitleBar();ImGui::TextDisabled("Timeline frame");
        ImNodes::BeginOutputAttribute(10);ImGui::TextUnformatted("Image");ImNodes::EndOutputAttribute();ImNodes::EndNode();
        ImNodes::BeginNode(2);ImNodes::BeginNodeTitleBar();ImGui::TextUnformatted("Media Out");ImNodes::EndNodeTitleBar();
        ImNodes::BeginInputAttribute(20);ImGui::TextUnformatted("Image");ImNodes::EndInputAttribute();ImGui::TextDisabled("Viewer output");ImNodes::EndNode();
        ImNodes::Link(100,10,20);ImNodes::MiniMap(.15f,ImNodesMiniMapLocation_BottomRight);ImNodes::EndNodeEditor();
        ImGui::End();
    }
    void color(){
        ImGui::Begin(win("Color").c_str());label("PRIMARY CORRECTION");
        auto* c=project.clip(selectedClip);if(!c){ImGui::TextWrapped("Select a clip in the timeline to adjust its look.");ImGui::End();return;}
        ImGui::TextWrapped("%s",project.asset(c->media)->name.c_str());ImGui::Separator();
        const char* names[]={"Brightness","Contrast","Saturation"};
        float* values[]={&c->grade.brightness,&c->grade.contrast,&c->grade.saturation};
        const float mins[]={-.5f,0,0},maxs[]={.5f,2,2};
        for(int i=0;i<3;++i){float value=*values[i];label(names[i]);ImGui::PushID(i);ImGui::SetNextItemWidth(-1);const bool edited=minimalSlider("##grade",&value,mins[i],maxs[i],"%.2f");if(ImGui::IsItemActivated()){stop();history.checkpoint(project);}if(edited){*values[i]=value;sourceMode=false;changed();}ImGui::PopID();}
        ImGui::Dummy({0,10});if(button("Reset correction")){stop();history.checkpoint(project);c->grade=Grade{};changed();}
        ImGui::Dummy({0,8});ImGui::TextDisabled("Basic SDR correction");ImGui::TextWrapped("Preview is approximate. Export uses FFmpeg colour filters; calibrated grading and HDR are not available yet.");
        ImGui::End();
    }
    void mixer(){
        ImGui::Begin(win("Mixer").c_str());label("MASTER OUTPUT");ImGui::TextDisabled("Stereo  /  48 kHz");ImGui::Separator();
        const float levels[]={transport.left(),transport.right()};const char* channels[]={"L","R"};
        for(int i=0;i<2;++i){ImGui::TextUnformatted(channels[i]);ImGui::SameLine();ImGui::PushStyleColor(ImGuiCol_PlotHistogram,accent);ImGui::ProgressBar(std::clamp(levels[i],0.f,1.f),{-1,17},"");ImGui::PopStyleColor();}
        ImGui::Dummy({0,13});label("CLIP MIX");
        for(auto& c:project.clips){
            const auto* a=project.asset(c.media);if(!a || !a->hasAudio)continue;
            ImGui::PushID(static_cast<int>(c.id));ImGui::TextWrapped("%s",a->name.c_str());
            float gain=c.gain;ImGui::SetNextItemWidth(-1);const bool edited=minimalSlider("##gain",&gain,0,2,"%.2fx");if(ImGui::IsItemActivated()){stop();history.checkpoint(project);}if(edited){c.gain=gain;changed();}
            bool mute=c.muted;if(ImGui::Checkbox("Mute",&mute)){stop();history.checkpoint(project);c.muted=mute;changed();}ImGui::Separator();ImGui::PopID();
        }
        if(project.clips.empty())ImGui::TextWrapped("Add a video with audio to the timeline to start mixing.");
        ImGui::End();
    }
    void renderPanel(){
        ImGui::Begin(win("Render").c_str());label("EXPORT VIDEO");ImGui::Separator();
        ImGui::BeginDisabled(exporter.running());
        const char* codecs[]={"H.264 / AVC","H.265 / HEVC","Apple ProRes"};int codec=static_cast<int>(exportSettings.codec);
        label("Video codec");ImGui::SetNextItemWidth(-1);if(ImGui::Combo("##export_codec",&codec,codecs,3))exportSettings.codec=static_cast<VideoCodec>(codec);
        if(exportSettings.codec==VideoCodec::ProRes){
            const char* profiles[]={"422 Proxy","422 LT","422","422 HQ"};int profile=static_cast<int>(exportSettings.proResProfile);
            label("ProRes profile");ImGui::SetNextItemWidth(-1);if(ImGui::Combo("##prores_profile",&profile,profiles,4))exportSettings.proResProfile=static_cast<ProResProfile>(profile);
            ImGui::TextDisabled("MOV / 10-bit 4:2:2 / PCM 24-bit");
            ImGui::TextWrapped("Bitrate follows the selected ProRes profile, resolution and frame rate.");
        }else{
            ImGui::TextDisabled("MP4 / AAC 192 kbps");
            const char* modes[]={"Automatic quality","Target bitrate"};int mode=static_cast<int>(exportSettings.rateControl);
            label("Rate control");ImGui::SetNextItemWidth(-1);if(ImGui::Combo("##rate_control",&mode,modes,2))exportSettings.rateControl=static_cast<RateControl>(mode);
            if(exportSettings.rateControl==RateControl::Bitrate){
                label("Target video bitrate");ImGui::SetNextItemWidth(-1);
                minimalSlider("##video_bitrate",&exportSettings.videoBitrateMbps,minVideoBitrateMbps,maxVideoBitrateMbps,"%.1f Mbps",ImGuiSliderFlags_Logarithmic|ImGuiSliderFlags_AlwaysClamp);
                ImGui::TextWrapped("Average target; actual bitrate varies with the video content.");
            }
        }
        ImGui::Dummy({0,8});
        const char* sizes[]={"Project resolution","1920 x 1080","1280 x 720"};label("Resolution");ImGui::SetNextItemWidth(-1);ImGui::Combo("##resolution",&exportSize,sizes,3);
        ImGui::EndDisabled();
        ImGui::TextDisabled("%.3g fps  /  Stereo 48 kHz",project.fps);ImGui::TextDisabled("Duration  %s",timecode(project.duration(),project.fps).c_str());
        ImGui::Dummy({0,15});ImGui::BeginDisabled(project.clips.empty() || exporter.running());if(button("Export video...",true,{-1,38}))startExport();ImGui::EndDisabled();
        if(exporter.running()){
            ImGui::Dummy({0,10});ImGui::ProgressBar(exporter.progress(),{-1,8},"");ImGui::Text("Rendering  %.0f%%",exporter.progress()*100);if(button("Cancel export"))exporter.cancel();
        }
        const auto message=exporter.message();if(!message.empty()){ImGui::Dummy({0,8});ImGui::TextWrapped("%s",message.c_str());}
        if(!exporter.running() && exporter.successful() && button("Show exported video"))platform::reveal(exportPath);
        ImGui::Dummy({0,12});ImGui::Separator();ImGui::TextWrapped("The export uses a snapshot of your timeline. You can keep editing while it renders.");
        ImGui::End();
    }
    void popups(){
        if(!error.empty())ImGui::OpenPopup("FrameRender message");
        if(ImGui::BeginPopupModal("FrameRender message",nullptr,ImGuiWindowFlags_AlwaysAutoResize)){
            ImGui::PushTextWrapPos(ImGui::GetCursorPosX()+520);ImGui::TextUnformatted(error.c_str());ImGui::PopTextWrapPos();
            if(button("OK",false,{100,0})){error.clear();ImGui::CloseCurrentPopup();}ImGui::EndPopup();
        }
        if(showAbout){ImGui::OpenPopup("About FrameRender");showAbout=false;}
        if(ImGui::BeginPopupModal("About FrameRender",nullptr,ImGuiWindowFlags_AlwaysAutoResize)){
            ImGui::TextColored(accent,"FrameRender 0.1");ImGui::TextUnformatted("A local video editor for macOS.");ImGui::Separator();
            ImGui::TextDisabled("C++ / Dear ImGui / ImNodes / SDL3 / FFmpeg");
            ImGui::TextUnformatted("Space: play    Cmd+B: split    Cmd+Z: undo");if(button("Close"))ImGui::CloseCurrentPopup();ImGui::EndPopup();
        }
    }
    void frame(){
        if(regenerating && importing.valid() && importing.wait_for(std::chrono::seconds(0))==std::future_status::ready){
            auto result=importing.get();for(auto& item:result.assets)for(const auto& a:project.media)if(a.path==item.media.path){if(thumbnails.count(a.id))SDL_DestroyTexture(thumbnails[a.id]);thumbnails[a.id]=texture(renderer,item.thumbnail);}
            regenerating=false;
        }else if(!regenerating)importFinished();
        if(!importing.valid() && !importQueue.empty()){auto queued=std::move(importQueue);importQueue.clear();import(queued);}
        if(wasPlaying){time()=transport.position();if(!transport.playing()){wasPlaying=false;const auto err=transport.error();if(!err.empty())error=err;}}
        updatePreviews();menus();shortcuts();
        auto* viewport=ImGui::GetMainViewport();const ImVec2 top=viewport->WorkPos;const float width=viewport->WorkSize.x,height=viewport->WorkSize.y;
        header(top,width);
        const float navHeight=navigationHeight();
        const ImVec2 dockPos{top.x,top.y+54},dockSize{width,std::max(100.f,height-54-navHeight-26)};
        ImGui::SetNextWindowPos(dockPos);ImGui::SetNextWindowSize(dockSize);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding,{0,0});ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize,0);
        ImGui::Begin("##dockhost",nullptr,ImGuiWindowFlags_NoDecoration|ImGuiWindowFlags_NoMove|ImGuiWindowFlags_NoDocking|ImGuiWindowFlags_NoSavedSettings|ImGuiWindowFlags_NoBringToFrontOnFocus);
        const auto id=ImGui::GetID(("Workspace"+std::to_string(page)).c_str());layout(id,dockSize);ImGui::DockSpace(id,{0,0});
        for(const auto& workspace:pages)if(workspace.id!=page){const auto other=ImGui::GetID(("Workspace"+std::to_string(workspace.id)).c_str());if(ImGui::DockBuilderGetNode(other))ImGui::DockSpace(other,{0,0},ImGuiDockNodeFlags_KeepAliveOnly);}
        ImGui::End();ImGui::PopStyleVar(2);
        if(page==0 || page==2)mediaPool();viewer();
        if(page==2 || page==3)inspector();
        if(page==2 || page==4 || page==5)timeline();
        if(page==2){cut();transitions();textPanel();}
        if(page==3)nodes();if(page==4)color();if(page==5)mixer();if(page==6)renderPanel();
        navigation({top.x,top.y+height-navHeight-26},width);statusbar({top.x,top.y+height-26},width);popups();
        if(dirty && std::chrono::steady_clock::now()-lastAutosave>std::chrono::seconds(30) && !importing.valid()){
            lastAutosave=std::chrono::steady_clock::now();try{project.save(support/"Recovery.frp");}catch(const std::exception& ex){status=std::string("Recovery save failed: ")+ex.what();}
        }
        std::string title=project.name+(dirty?" *":"")+" - FrameRender";SDL_SetWindowTitle(window,title.c_str());
    }
};
Editor::Editor(SDL_Window* w,SDL_Renderer* r):impl_(std::make_unique<Impl>(w,r)){}
Editor::~Editor()=default;
void Editor::frame(){impl_->frame();}
void Editor::dropFile(const std::filesystem::path& path){if(path.extension()==".frp"){if(impl_->importing.valid()){impl_->status="Wait for the current import to complete.";return;}if(impl_->discard())impl_->load(path);}else impl_->import({path});}
void Editor::loadProject(const std::filesystem::path& path){impl_->load(path);}
bool Editor::requestQuit(){return impl_->discard();}
void Editor::setPage(int page){impl_->page=page==1?2:std::clamp(page,0,6);}
}
