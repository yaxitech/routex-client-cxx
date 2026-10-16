#pragma once

#include "yaxi/routex-client.h"

namespace yaxi::internal {
struct HttpRequest;
struct HttpResponse;

HttpResponse execute_request(const HttpTransport &transport, HttpRequest request);
} // namespace yaxi::internal
