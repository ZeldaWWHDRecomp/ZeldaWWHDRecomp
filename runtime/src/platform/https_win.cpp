#include "../exception_report.h"
#include "https.h"
#include "download_file.h"
#include "../mods/catalogue_schema.h"
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <winhttp.h>
#include <chrono>
#include <memory>
#include <vector>

namespace host {
namespace {
struct Close {void operator()(void* handle)const{if(handle)WinHttpCloseHandle(handle);}};
using Handle=std::unique_ptr<void,Close>;
void check(BOOL ok){if(!ok)exception_report::raise("HTTPS download failed; check the connection and try again");}
std::wstring wide(const std::string& text) {
    int size=MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,text.data(),int(text.size()),nullptr,0);
    if(!size)exception_report::raise("Invalid HTTPS URL");
    std::wstring out(size,L'\0');check(MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,text.data(),int(text.size()),out.data(),size));return out;
}
}
void download_https(const std::string& url,const std::filesystem::path& destination,uint64_t limit) {
    if(!mods::catalogue::https_url(url))exception_report::raise("Download URL must use HTTPS");
    auto address=wide(url);URL_COMPONENTS parts{};parts.dwStructSize=sizeof parts;
    parts.dwHostNameLength=parts.dwUrlPathLength=parts.dwExtraInfoLength=DWORD(-1);
    check(WinHttpCrackUrl(address.c_str(),DWORD(address.size()),0,&parts));
    if(parts.nScheme!=INTERNET_SCHEME_HTTPS)exception_report::raise("Download URL must use HTTPS");
    std::wstring host(parts.lpszHostName,parts.dwHostNameLength),path(parts.lpszUrlPath,parts.dwUrlPathLength);
    if(parts.dwExtraInfoLength)path.append(parts.lpszExtraInfo,parts.dwExtraInfoLength);
    if(path.empty())path=L"/";
    Handle session(WinHttpOpen(L"WWHD Mod Catalogue",WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,WINHTTP_NO_PROXY_NAME,WINHTTP_NO_PROXY_BYPASS,0));
    check(bool(session));check(WinHttpSetTimeouts(session.get(),15000,15000,30000,30000));
    Handle connection(WinHttpConnect(session.get(),host.c_str(),parts.nPort,0));check(bool(connection));
    Handle request(WinHttpOpenRequest(connection.get(),L"GET",path.c_str(),nullptr,WINHTTP_NO_REFERER,WINHTTP_DEFAULT_ACCEPT_TYPES,WINHTTP_FLAG_SECURE));check(bool(request));
    DWORD policy=WINHTTP_OPTION_REDIRECT_POLICY_DISALLOW_HTTPS_TO_HTTP,redirects=5;
    check(WinHttpSetOption(request.get(),WINHTTP_OPTION_REDIRECT_POLICY,&policy,sizeof policy));
    check(WinHttpSetOption(request.get(),WINHTTP_OPTION_MAX_HTTP_AUTOMATIC_REDIRECTS,&redirects,sizeof redirects));
    DWORD disabled=WINHTTP_DISABLE_COOKIES;
    check(WinHttpSetOption(request.get(),WINHTTP_OPTION_DISABLE_FEATURE,&disabled,sizeof disabled));
    DownloadFile file(destination,limit);auto started=std::chrono::steady_clock::now();
    check(WinHttpSendRequest(request.get(),WINHTTP_NO_ADDITIONAL_HEADERS,0,WINHTTP_NO_REQUEST_DATA,0,0,0));
    check(WinHttpReceiveResponse(request.get(),nullptr));
    DWORD status=0,size=sizeof status;
    check(WinHttpQueryHeaders(request.get(),WINHTTP_QUERY_STATUS_CODE|WINHTTP_QUERY_FLAG_NUMBER,WINHTTP_HEADER_NAME_BY_INDEX,&status,&size,WINHTTP_NO_HEADER_INDEX));
    if(status!=200)exception_report::raise("HTTPS server did not return a successful response (HTTP "+std::to_string(status)+")");
    char buffer[16384];
    for(;;) {
        if(std::chrono::steady_clock::now()-started>std::chrono::seconds(120))exception_report::raise("HTTPS download timed out");
        DWORD bytes=0;check(WinHttpReadData(request.get(),buffer,sizeof buffer,&bytes));
        if(!bytes)break;file.append(buffer,bytes);
    }
    file.finish();
}
} // namespace host
