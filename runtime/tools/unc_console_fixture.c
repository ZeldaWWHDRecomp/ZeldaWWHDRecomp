// Synthetic stand-in for the release launcher; never starts a game or setup.
#include <windows.h>
#include <stdio.h>
#include <string.h>
#include <wchar.h>
int main(int argc,char** argv) {
    wchar_t cwd[32768];
    DWORD length=GetCurrentDirectoryW(32768,cwd);
    if(!length||length>=32768)return 1;
    const wchar_t* name=wcsrchr(cwd,L'\\');
    if(argc!=2||strcmp(argv[1],"--console-setup")||!name||wcscmp(name+1,L"UNC release"))return 1;
    puts("console fixture ok");
    return 0;
}
