#include "LrclibLookup.hpp"
#ifdef _WIN32
#include <windows.h>
#include <winhttp.h>

// WinHTTP implementation of the single request the built-in LRCLIB client needs.
LrclibHttpResponse LrclibHttpGet(const std::string& pathAndQuery) {
    LrclibHttpResponse result;
    const std::wstring path(pathAndQuery.begin(), pathAndQuery.end());  // ASCII by contract
    HINTERNET session = WinHttpOpen(
        L"VirtualDJEmbeddedLyrics (https://github.com/VeksCZ/VirtualDJEmbeddedLyrics)",
        WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!session) { result.error = L"WinHTTP unavailable"; return result; }
    WinHttpSetTimeouts(session, 10000, 10000, 15000, 15000);
    HINTERNET connection = WinHttpConnect(session, L"lrclib.net", INTERNET_DEFAULT_HTTPS_PORT, 0);
    HINTERNET request = connection
        ? WinHttpOpenRequest(connection, L"GET", path.c_str(), nullptr, WINHTTP_NO_REFERER,
                             WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE)
        : nullptr;
    if (request &&
        WinHttpSendRequest(request, L"Accept: application/json\r\n", static_cast<DWORD>(-1L),
                           WINHTTP_NO_REQUEST_DATA, 0, 0, 0) &&
        WinHttpReceiveResponse(request, nullptr)) {
        DWORD status = 0, size = sizeof(status);
        WinHttpQueryHeaders(request, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                            WINHTTP_HEADER_NAME_BY_INDEX, &status, &size, WINHTTP_NO_HEADER_INDEX);
        result.status = static_cast<long>(status);
        for (DWORD available = 0; WinHttpQueryDataAvailable(request, &available) && available;) {
            std::string chunk(available, '\0');
            DWORD read = 0;
            if (!WinHttpReadData(request, chunk.data(), available, &read) || !read) break;
            result.body.append(chunk.data(), read);
            if (result.body.size() > 8 * 1024 * 1024) break;
        }
    } else {
        result.error = L"network error " + std::to_wstring(GetLastError());
    }
    if (request) WinHttpCloseHandle(request);
    if (connection) WinHttpCloseHandle(connection);
    WinHttpCloseHandle(session);
    return result;
}
#endif
