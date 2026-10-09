// Run a local tool with an argument vector, never through a shell. Drain bounded output fully.
#pragma once
#include <algorithm>
#include <array>
#include <cerrno>
#include <string>
#include <vector>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <fcntl.h>
#include <spawn.h>
#include <sys/wait.h>
#include <unistd.h>
extern char** environ;
#endif
namespace host {
struct ProcessResult { int code = -1; std::string output, error; };
#ifdef _WIN32
inline std::wstring process_wide(const std::string& s) {
    int n=MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,s.data(),int(s.size()),nullptr,0);
    if(!n&&!s.empty())return {};
    std::wstring w(n,0);if(n)MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,s.data(),int(s.size()),w.data(),n);
    return w;
}
inline std::wstring process_quote(const std::wstring& s) {
    std::wstring out=L"\"";size_t slashes=0;
    for(wchar_t ch:s) {
        if(ch==L'\\'){++slashes;continue;}
        out.append(slashes*(ch==L'"'?2:1),L'\\');slashes=0;
        if(ch==L'"')out+=L'\\';out+=ch;
    }
    out.append(slashes*2,L'\\');return out+L'"';
}
#endif
inline ProcessResult run_process(const std::vector<std::string>& args) {
    ProcessResult result;
    if(args.empty()||args.front().empty()){result.error="empty tool command";return result;}
    for(const auto& arg:args)if(arg.find('\0')!=std::string::npos){result.error="invalid tool argument";return result;}
    constexpr size_t limit=1024*1024;
    std::array<char,4096> buffer;
#ifdef _WIN32
    HANDLE read=nullptr,write=nullptr;
    SECURITY_ATTRIBUTES sa{sizeof(sa),nullptr,TRUE};
    if(!CreatePipe(&read,&write,&sa,0)){result.error="cannot create tool output pipe";return result;}
    SetHandleInformation(read,HANDLE_FLAG_INHERIT,0);
    std::wstring command;
    for(const auto& arg:args){if(!command.empty())command+=L' ';command+=process_quote(process_wide(arg));}
    STARTUPINFOW si{};si.cb=sizeof(si);si.dwFlags=STARTF_USESTDHANDLES;
    si.hStdOutput=write;si.hStdError=write;
    si.hStdInput=CreateFileW(L"NUL",GENERIC_READ,FILE_SHARE_READ|FILE_SHARE_WRITE,&sa,OPEN_EXISTING,0,nullptr);
    PROCESS_INFORMATION pi{};
    BOOL started=CreateProcessW(nullptr,command.data(),nullptr,nullptr,TRUE,CREATE_NO_WINDOW,nullptr,nullptr,&si,&pi);
    if(si.hStdInput!=INVALID_HANDLE_VALUE)CloseHandle(si.hStdInput);
    CloseHandle(write);
    if(!started){CloseHandle(read);result.error="cannot start tool (Windows error "+std::to_string(GetLastError())+")";return result;}
    DWORD count=0;
    while(ReadFile(read,buffer.data(),DWORD(buffer.size()),&count,nullptr)&&count)
        if(result.output.size()<limit)result.output.append(buffer.data(),std::min<size_t>(count,limit-result.output.size()));
    CloseHandle(read);WaitForSingleObject(pi.hProcess,INFINITE);DWORD code=1;
    GetExitCodeProcess(pi.hProcess,&code);CloseHandle(pi.hThread);CloseHandle(pi.hProcess);
    result.code=int(code);
#else
    int pipefd[2];if(pipe(pipefd)){result.error="cannot create tool output pipe";return result;}
    fcntl(pipefd[0],F_SETFD,FD_CLOEXEC);fcntl(pipefd[1],F_SETFD,FD_CLOEXEC);
    posix_spawn_file_actions_t actions;posix_spawn_file_actions_init(&actions);
    posix_spawn_file_actions_addopen(&actions,STDIN_FILENO,"/dev/null",O_RDONLY,0);
    posix_spawn_file_actions_adddup2(&actions,pipefd[1],STDOUT_FILENO);
    posix_spawn_file_actions_adddup2(&actions,pipefd[1],STDERR_FILENO);
    posix_spawn_file_actions_addclose(&actions,pipefd[0]);
    posix_spawn_file_actions_addclose(&actions,pipefd[1]);
    std::vector<char*> argv;for(const auto& arg:args)argv.push_back(const_cast<char*>(arg.c_str()));argv.push_back(nullptr);
    pid_t pid=0;int error=posix_spawnp(&pid,argv[0],&actions,nullptr,argv.data(),environ);
    posix_spawn_file_actions_destroy(&actions);close(pipefd[1]);
    if(error){close(pipefd[0]);result.error="cannot start tool (error "+std::to_string(error)+")";return result;}
    for(;;){ssize_t count=read(pipefd[0],buffer.data(),buffer.size());
        if(count<0&&errno==EINTR)continue;if(count<=0)break;
        if(result.output.size()<limit)result.output.append(buffer.data(),std::min<size_t>(size_t(count),limit-result.output.size()));
    }
    close(pipefd[0]);int status=0;
    while(waitpid(pid,&status,0)<0){if(errno==EINTR)continue;result.error="cannot wait for tool";return result;}
    result.code=WIFEXITED(status)?WEXITSTATUS(status):128+(WIFSIGNALED(status)?WTERMSIG(status):0);
#endif
    return result;
}
} // namespace host
