#include "media/Process.hpp"
#include <atomic>
#include <cerrno>
#include <chrono>
#include <cstring>
#include <fcntl.h>
#include <fstream>
#include <spawn.h>
#include <signal.h>
#include <stdexcept>
#include <sys/wait.h>
#include <thread>
#include <unistd.h>
extern char** environ;

namespace fr {
std::string ffmpegPath() { return FR_FFMPEG_PATH; }
std::filesystem::path temporaryPath(const std::string& extension) {
    static std::atomic<unsigned> counter{0};
    return std::filesystem::temp_directory_path()/("framerender-"+std::to_string(getpid())+"-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count())+"-"+std::to_string(counter++)+extension);
}
std::string readText(const std::filesystem::path& path,size_t limit) {
    std::ifstream file(path,std::ios::binary);std::string text(limit,'\0');file.read(text.data(),static_cast<std::streamsize>(limit));text.resize(static_cast<size_t>(file.gcount()));return text;
}
Process::~Process() { cancel();wait();if(output_>=0)close(output_); }
void Process::start(const std::vector<std::string>& args,const std::filesystem::path& errors) {
    if(args.empty()) throw std::runtime_error("Cannot launch an empty command.");
    if(pid_) throw std::runtime_error("Process already running.");
    if(output_>=0){close(output_);output_=-1;}
    int channel[2];if(pipe(channel)!=0) throw std::runtime_error("Cannot open media process pipe.");
    // Other concurrently spawned media jobs must not inherit either pipe endpoint.
    fcntl(channel[0],F_SETFD,FD_CLOEXEC);fcntl(channel[1],F_SETFD,FD_CLOEXEC);
    std::vector<char*> argv;for(const auto& arg:args)argv.push_back(const_cast<char*>(arg.c_str()));argv.push_back(nullptr);
    posix_spawn_file_actions_t actions;posix_spawn_file_actions_init(&actions);
    posix_spawn_file_actions_adddup2(&actions,channel[1],STDOUT_FILENO);
    posix_spawn_file_actions_addclose(&actions,channel[0]);posix_spawn_file_actions_addclose(&actions,channel[1]);
    posix_spawn_file_actions_addopen(&actions,STDIN_FILENO,"/dev/null",O_RDONLY,0);
    posix_spawn_file_actions_addopen(&actions,STDERR_FILENO,errors.c_str(),O_WRONLY|O_CREAT|O_TRUNC,0600);
    pid_t spawned=0;
    const int result=posix_spawn(&spawned,args.front().c_str(),&actions,nullptr,argv.data(),environ);
    posix_spawn_file_actions_destroy(&actions);close(channel[1]);
    if(result) {close(channel[0]);throw std::runtime_error("Cannot launch FFmpeg: "+std::string(strerror(result)));}
    {std::lock_guard<std::mutex> lock(mutex_);pid_=spawned;output_=channel[0];}
}
long Process::read(void* data,size_t bytes) { long n;do{n=::read(output_,data,bytes);}while(n<0 && errno==EINTR);return n; }
void Process::cancel() {
    std::lock_guard<std::mutex> lock(mutex_);
    // A stopped PCM consumer no longer drains stdout. FFmpeg's graceful flush can
    // block on that pipe forever. All jobs write disposable PCM or temporary files,
    // so cancellation deliberately terminates this owned child without flushing.
    if(pid_>0)kill(pid_,SIGKILL);
}
int Process::wait() {
    for(;;) {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if(pid_<=0)return exit_;
            int status=0;const int r=waitpid(pid_,&status,WNOHANG);
            if(r>0){pid_=0;exit_=WIFEXITED(status)?WEXITSTATUS(status):128+WTERMSIG(status);return exit_;}
            if(r<0 && errno!=EINTR){pid_=0;return exit_;}
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
}
}
