#import <Foundation/Foundation.h>
#include "../exception_report.h"
#include "https.h"
#include "download_file.h"
#include "../mods/catalogue_schema.h"
#include <memory>

@interface WWHDDownload : NSObject <NSURLSessionDataDelegate> {
@public
    std::unique_ptr<host::DownloadFile> sink;
    std::string failure;
    dispatch_semaphore_t completed;
    NSUInteger redirects;
}
@end
@implementation WWHDDownload
- (void)URLSession:(NSURLSession*)session task:(NSURLSessionTask*)task
        willPerformHTTPRedirection:(NSHTTPURLResponse*)response newRequest:(NSURLRequest*)request
        completionHandler:(void (^)(NSURLRequest*))handler {
    if(++redirects>5 || !mods::catalogue::https_url(request.URL.absoluteString.UTF8String ?: "")) {
        failure="HTTPS download redirect refused";handler(nil);[task cancel];
    }else handler(request);
}
- (void)URLSession:(NSURLSession*)session dataTask:(NSURLSessionDataTask*)task
        didReceiveResponse:(NSURLResponse*)response
        completionHandler:(void (^)(NSURLSessionResponseDisposition))handler {
    if(![response isKindOfClass:[NSHTTPURLResponse class]] || ((NSHTTPURLResponse*)response).statusCode!=200) {
        failure="HTTPS server did not return a successful response";
        handler(NSURLSessionResponseCancel);return;
    }
    handler(NSURLSessionResponseAllow);
}
- (void)URLSession:(NSURLSession*)session dataTask:(NSURLSessionDataTask*)task didReceiveData:(NSData*)data {
    if(!failure.empty())return;
    try{sink->append(data.bytes,data.length);}
    catch(const std::exception& error){failure=error.what();[task cancel];}
}
- (void)URLSession:(NSURLSession*)session task:(NSURLSessionTask*)task didCompleteWithError:(NSError*)error {
    if(failure.empty()) {
        if(error)failure="HTTPS download failed; check the connection and try again";
        else try{sink->finish();}catch(const std::exception& problem){failure=problem.what();}
    }
    dispatch_semaphore_signal(completed);
}
@end
namespace host {
void download_https(const std::string& url,const std::filesystem::path& destination,uint64_t limit) {
    if(!mods::catalogue::https_url(url))exception_report::raise("Download URL must use HTTPS");
    @autoreleasepool {
        auto delegate=[WWHDDownload new];
        delegate->sink=std::make_unique<DownloadFile>(destination,limit);
        delegate->completed=dispatch_semaphore_create(0);delegate->redirects=0;
        auto configuration=[NSURLSessionConfiguration ephemeralSessionConfiguration];
        configuration.HTTPCookieStorage=nil;configuration.HTTPShouldSetCookies=NO;configuration.URLCache=nil;
        configuration.requestCachePolicy=NSURLRequestReloadIgnoringLocalCacheData;
        configuration.timeoutIntervalForRequest=30;configuration.timeoutIntervalForResource=120;
        auto queue=[NSOperationQueue new];queue.maxConcurrentOperationCount=1;
        auto session=[NSURLSession sessionWithConfiguration:configuration delegate:delegate delegateQueue:queue];
        auto address=[NSURL URLWithString:[NSString stringWithUTF8String:url.c_str()]];
        if(!address){[session invalidateAndCancel];exception_report::raise("Invalid HTTPS download URL");}
        [[session dataTaskWithURL:address] resume];
        dispatch_semaphore_wait(delegate->completed,DISPATCH_TIME_FOREVER);
        auto failure=delegate->failure;
        [session finishTasksAndInvalidate];
        // Destroy the writer now so a failed transfer leaves no file when this call returns.
        delegate->sink.reset();
        if(!failure.empty())exception_report::raise(failure);
    }
}
} // namespace host
