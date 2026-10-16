#include "pch.h"

#include <yaxi/routex-client.h>

#include <winhttp.h>

#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <variant>

using namespace yaxi;

template <typename T> T unwrap(yaxi::Result<T> result) {
    if (const auto error = std::get_if<yaxi::Error>(&result)) {
        std::visit([](const auto &e) { throw e; }, *error);
    }

    return std::get<T>(std::move(result));
}

namespace {

[[noreturn]] void fail(const char *call) {
    throw std::runtime_error(std::string(call) + " failed: " + std::to_string(GetLastError()));
}

std::wstring widen(const std::string &s) {
    if (s.empty()) {
        return {};
    }
    const int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), nullptr, 0);
    std::wstring w(n, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), w.data(), n);
    return w;
}

std::string narrow(const std::wstring &w) {
    if (w.empty()) {
        return {};
    }
    const int n = WideCharToMultiByte(CP_UTF8, 0, w.data(), static_cast<int>(w.size()), nullptr, 0,
                                      nullptr, nullptr);
    std::string s(n, '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.data(), static_cast<int>(w.size()), s.data(), n, nullptr,
                        nullptr);
    return s;
}

struct CloseHandle {
    void operator()(HINTERNET handle) const { WinHttpCloseHandle(handle); }
};
using Handle = std::unique_ptr<void, CloseHandle>;

HttpResponse winHttpTransport(const HttpRequest &request) {
    const std::wstring url = widen(request.url);
    URL_COMPONENTS parts{};
    parts.dwStructSize = sizeof(parts);
    parts.dwHostNameLength = static_cast<DWORD>(-1);
    parts.dwUrlPathLength = static_cast<DWORD>(-1);
    parts.dwExtraInfoLength = static_cast<DWORD>(-1);
    if (!WinHttpCrackUrl(url.c_str(), 0, 0, &parts)) {
        fail("WinHttpCrackUrl");
    }
    const std::wstring host(parts.lpszHostName, parts.dwHostNameLength);
    // The query string directly follows the path in the URL.
    const std::wstring path(parts.lpszUrlPath, parts.dwUrlPathLength + parts.dwExtraInfoLength);

    const Handle session(WinHttpOpen(L"routex-client-cxx-test", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
                                     WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0));
    if (!session) {
        fail("WinHttpOpen");
    }
    const Handle connection(WinHttpConnect(session.get(), host.c_str(), parts.nPort, 0));
    if (!connection) {
        fail("WinHttpConnect");
    }
    const Handle handle(
        WinHttpOpenRequest(connection.get(), widen(request.method).c_str(), path.c_str(), nullptr,
                           WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES,
                           parts.nScheme == INTERNET_SCHEME_HTTPS ? WINHTTP_FLAG_SECURE : 0));
    if (!handle) {
        fail("WinHttpOpenRequest");
    }
    DWORD disable = WINHTTP_DISABLE_REDIRECTS;
    if (!WinHttpSetOption(handle.get(), WINHTTP_OPTION_DISABLE_FEATURE, &disable,
                          sizeof(disable))) {
        fail("WinHttpSetOption");
    }

    std::wstring headers;
    for (const auto &header : request.headers) {
        headers += widen(header.name) + L": " +
                   widen(std::string(header.value.begin(), header.value.end())) + L"\r\n";
    }
    const auto bodySize = static_cast<DWORD>(request.body.size());
    if (!WinHttpSendRequest(handle.get(),
                            headers.empty() ? WINHTTP_NO_ADDITIONAL_HEADERS : headers.c_str(),
                            static_cast<DWORD>(-1),
                            request.body.empty() ? WINHTTP_NO_REQUEST_DATA
                                                 : const_cast<uint8_t *>(request.body.data()),
                            bodySize, bodySize, 0)) {
        fail("WinHttpSendRequest");
    }
    if (!WinHttpReceiveResponse(handle.get(), nullptr)) {
        fail("WinHttpReceiveResponse");
    }

    DWORD status = 0;
    DWORD size = sizeof(status);
    if (!WinHttpQueryHeaders(handle.get(), WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                             WINHTTP_HEADER_NAME_BY_INDEX, &status, &size,
                             WINHTTP_NO_HEADER_INDEX)) {
        fail("WinHttpQueryHeaders");
    }

    // The raw headers are NUL-separated lines, the first being the status line.
    size = 0;
    WinHttpQueryHeaders(handle.get(), WINHTTP_QUERY_RAW_HEADERS, WINHTTP_HEADER_NAME_BY_INDEX,
                        WINHTTP_NO_OUTPUT_BUFFER, &size, WINHTTP_NO_HEADER_INDEX);
    if (GetLastError() != ERROR_INSUFFICIENT_BUFFER) {
        fail("WinHttpQueryHeaders");
    }
    std::wstring raw(size / sizeof(wchar_t), L'\0');
    if (!WinHttpQueryHeaders(handle.get(), WINHTTP_QUERY_RAW_HEADERS, WINHTTP_HEADER_NAME_BY_INDEX,
                             raw.data(), &size, WINHTTP_NO_HEADER_INDEX)) {
        fail("WinHttpQueryHeaders");
    }
    std::vector<HttpHeader> responseHeaders;
    for (size_t start = raw.find(L'\0') + 1; start < raw.size();) {
        const size_t end = raw.find(L'\0', start);
        const std::string line = narrow(raw.substr(start, end - start));
        start = end == std::wstring::npos ? raw.size() : end + 1;
        const size_t colon = line.find(':');
        if (colon == std::string::npos) {
            continue;
        }
        const size_t valueStart = line.find_first_not_of(' ', colon + 1);
        const std::string value =
            valueStart == std::string::npos ? std::string() : line.substr(valueStart);
        responseHeaders.push_back(
            HttpHeader{line.substr(0, colon), std::vector<uint8_t>(value.begin(), value.end())});
    }

    std::vector<uint8_t> body;
    for (;;) {
        DWORD available = 0;
        if (!WinHttpQueryDataAvailable(handle.get(), &available)) {
            fail("WinHttpQueryDataAvailable");
        }
        if (available == 0) {
            break;
        }
        const size_t offset = body.size();
        body.resize(offset + available);
        DWORD read = 0;
        if (!WinHttpReadData(handle.get(), body.data() + offset, available, &read)) {
            fail("WinHttpReadData");
        }
        body.resize(offset + read);
    }

    return HttpResponse{static_cast<uint16_t>(status), std::move(responseHeaders), std::move(body)};
}

} // namespace

extern "C" {
__declspec(dllexport) size_t search(const char *url_c, const char *ticket_c) {
    std::string url(url_c);
    std::string ticket(ticket_c);
    RoutexClient client(winHttpTransport, url);

    std::vector<yaxi::SearchFilter> filters{yaxi::TermSearchFilter{"sparkasse"},
                                            yaxi::TermSearchFilter{"stadt"}};

    std::vector<yaxi::ConnectionInfo> connectionInfos =
        unwrap(client.search(ticket, filters, true, 20));

    return connectionInfos.size();
}
}
