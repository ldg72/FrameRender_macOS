#include "ui/Editor.hpp"
#include "platform/Platform.hpp"
#include "imgui.h"
#include "imgui_impl_sdl3.h"
#include "imgui_impl_sdlrenderer3.h"
#include "imnodes.h"
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>
#include <filesystem>
#include <algorithm>
#include <iostream>
#include <memory>
#include <string>

int main(int argc,char** argv){
    std::filesystem::path project,media,screenshot;int frameLimit=0,initialPage=2;
    for(int i=1;i<argc;++i){
        const std::string arg=argv[i];
        if(arg=="--smoke-frames" && i+1<argc)frameLimit=std::stoi(argv[++i]);
        else if(arg=="--screenshot" && i+1<argc)screenshot=argv[++i];
        else if(arg=="--page" && i+1<argc)initialPage=std::stoi(argv[++i]);
        else if(arg=="--open" && i+1<argc)project=argv[++i];
        else if(arg=="--media" && i+1<argc)media=argv[++i];
        else if(arg=="--help"){std::cout<<"FrameRender [--open project.frp] [--media video.mp4] [--page 0|2|3|4|5|6]\n";return 0;}
    }
    if(!SDL_Init(SDL_INIT_VIDEO|SDL_INIT_AUDIO)){std::cerr<<SDL_GetError()<<'\n';return 1;}
    fr::platform::initialize();
    SDL_SetHintWithPriority(SDL_HINT_RENDER_DRIVER,"metal",SDL_HINT_DEFAULT);
    SDL_Rect usable{0,0,1440,900};SDL_GetDisplayUsableBounds(SDL_GetPrimaryDisplay(),&usable);
    auto* window=SDL_CreateWindow("FrameRender",std::min(1440,static_cast<int>(usable.w*.94)),std::min(900,static_cast<int>(usable.h*.94)),SDL_WINDOW_RESIZABLE|SDL_WINDOW_HIGH_PIXEL_DENSITY|SDL_WINDOW_HIDDEN);
    if(!window){std::cerr<<SDL_GetError()<<'\n';SDL_Quit();return 1;}
    SDL_SetWindowMinimumSize(window,1100,700);
    SDL_SetWindowPosition(window,SDL_WINDOWPOS_CENTERED,SDL_WINDOWPOS_CENTERED);
    auto* renderer=SDL_CreateRenderer(window,nullptr);
    if(!renderer){std::cerr<<SDL_GetError()<<'\n';SDL_DestroyWindow(window);SDL_Quit();return 1;}
    SDL_SetRenderVSync(renderer,1);
    IMGUI_CHECKVERSION();ImGui::CreateContext();ImNodes::CreateContext();
    auto& io=ImGui::GetIO();io.ConfigFlags|=ImGuiConfigFlags_NavEnableKeyboard|ImGuiConfigFlags_DockingEnable;
    if(std::filesystem::exists("/System/Library/Fonts/SFNS.ttf"))io.Fonts->AddFontFromFileTTF("/System/Library/Fonts/SFNS.ttf",15);
    ImGui_ImplSDL3_InitForSDLRenderer(window,renderer);ImGui_ImplSDLRenderer3_Init(renderer);
    SDL_ShowWindow(window);
    std::cout<<"FrameRender 0.1 | "<<SDL_GetRendererName(renderer)<<" | ImGui "<<IMGUI_VERSION<<'\n';
    bool done=false;int frames=0,code=0;
    try{
        fr::Editor editor(window,renderer);editor.setPage(initialPage);
        if(!project.empty())editor.loadProject(project);if(!media.empty())editor.dropFile(media);
        while(!done){
            const Uint64 frameStart=SDL_GetTicksNS();
            SDL_Event event;
            while(SDL_PollEvent(&event)){
                ImGui_ImplSDL3_ProcessEvent(&event);
                if(event.type==SDL_EVENT_QUIT || (event.type==SDL_EVENT_WINDOW_CLOSE_REQUESTED && event.window.windowID==SDL_GetWindowID(window)))done=editor.requestQuit();
                if(event.type==SDL_EVENT_DROP_FILE && event.drop.data)editor.dropFile(event.drop.data);
            }
            if(done)break;
            if(SDL_GetWindowFlags(window)&SDL_WINDOW_MINIMIZED){SDL_Delay(30);continue;}
            ImGui_ImplSDLRenderer3_NewFrame();ImGui_ImplSDL3_NewFrame();ImGui::NewFrame();
            editor.frame();ImGui::Render();
            SDL_SetRenderScale(renderer,io.DisplayFramebufferScale.x,io.DisplayFramebufferScale.y);
            SDL_SetRenderDrawColor(renderer,22,25,29,255);SDL_RenderClear(renderer);
            ImGui_ImplSDLRenderer3_RenderDrawData(ImGui::GetDrawData(),renderer);
            ++frames;
            if(frameLimit>0 && frames>=frameLimit){
                if(!screenshot.empty()){
                    if(SDL_Surface* surface=SDL_RenderReadPixels(renderer,nullptr)){if(!SDL_SaveBMP(surface,screenshot.c_str())){std::cerr<<SDL_GetError()<<'\n';code=1;}SDL_DestroySurface(surface);}else{std::cerr<<SDL_GetError()<<'\n';code=1;}
                }
                done=true;
            }
            SDL_RenderPresent(renderer);
            const Uint64 elapsed=SDL_GetTicksNS()-frameStart;
            if(elapsed<16666667)SDL_DelayNS(16666667-elapsed);
        }
    }catch(const std::exception& error){std::cerr<<"FrameRender: "<<error.what()<<'\n';code=1;}
    ImGui_ImplSDLRenderer3_Shutdown();ImGui_ImplSDL3_Shutdown();ImNodes::DestroyContext();ImGui::DestroyContext();
    SDL_DestroyRenderer(renderer);SDL_DestroyWindow(window);SDL_Quit();return code;
}
