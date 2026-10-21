#include <yaxi/routex-client.h>

// Never called. It exists so the compiler sees the published headers and the
// linker has to resolve the published symbols out of the prefab package.
extern "C" void yaxiConsumerLinkCheck() {
    const yaxi::RoutexClient client(
        [](const yaxi::HttpRequest &) { return yaxi::HttpResponse{500, {}, {}}; });
    const auto version = client.systemVersion("00000000-0000-0000-0000-000000000000");
    const auto traceId = client.traceId();
    static_cast<void>(version);
    static_cast<void>(traceId);
}
