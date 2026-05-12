#include "pch.h"

#include <yaxi/routex-client.h>

#include <optional>
#include <string>
#include <variant>

using namespace yaxi;

template <typename T> T unwrap(yaxi::Result<T> result) {
    if (const auto error = std::get_if<yaxi::Error>(&result)) {
        std::visit([](const auto &e) { throw e; }, *error);
    }

    return std::get<T>(std::move(result));
}

extern "C" {
__declspec(dllexport) size_t search(const char *url_c, const char *ticket_c) {
    std::string url(url_c);
    std::string ticket(ticket_c);
    RoutexClient client(url);

    std::vector<yaxi::SearchFilter> filters{yaxi::TermSearchFilter{"sparkasse"},
                                            yaxi::TermSearchFilter{"stadt"}};

    std::vector<yaxi::ConnectionInfo> connectionInfos =
        unwrap(client.search(ticket, filters, true, 20));

    return connectionInfos.size();
}
}
