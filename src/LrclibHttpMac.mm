#include "LrclibLookup.hpp"
#ifdef __APPLE__
#import <Foundation/Foundation.h>

// NSURLSession implementation of the single request the built-in LRCLIB client needs.
// Blocking by design: callers run it on a background queue, never on the main thread.
LrclibHttpResponse LrclibHttpGet(const std::string& pathAndQuery) {
    __block LrclibHttpResponse result;
    @autoreleasepool {
        NSString* text = [@"https://lrclib.net" stringByAppendingString:
                          [NSString stringWithUTF8String:pathAndQuery.c_str()]];
        NSURL* url = [NSURL URLWithString:text];
        if (!url) { result.error = L"invalid LRCLIB address"; return result; }
        NSMutableURLRequest* request = [NSMutableURLRequest requestWithURL:url
                                                               cachePolicy:NSURLRequestReloadIgnoringLocalCacheData
                                                           timeoutInterval:15.0];
        [request setValue:@"application/json" forHTTPHeaderField:@"Accept"];
        [request setValue:@"VirtualDJEmbeddedLyrics (https://github.com/VeksCZ/VirtualDJEmbeddedLyrics)"
       forHTTPHeaderField:@"User-Agent"];
        dispatch_semaphore_t done = dispatch_semaphore_create(0);
        NSURLSessionDataTask* task = [[NSURLSession sharedSession]
            dataTaskWithRequest:request
              completionHandler:^(NSData* data, NSURLResponse* response, NSError* error) {
                if (error) {
                    result.error = L"network error " + std::to_wstring(static_cast<long>(error.code));
                } else {
                    if ([response isKindOfClass:[NSHTTPURLResponse class]])
                        result.status = static_cast<long>(((NSHTTPURLResponse*)response).statusCode);
                    if (data.length)
                        result.body.assign(static_cast<const char*>(data.bytes), data.length);
                }
                dispatch_semaphore_signal(done);
            }];
        [task resume];
        if (dispatch_semaphore_wait(done, dispatch_time(DISPATCH_TIME_NOW, 20 * NSEC_PER_SEC)) != 0) {
            [task cancel];
            result.error = L"the request timed out";
        }
    }
    return result;
}
#endif
