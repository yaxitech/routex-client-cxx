#include "ticket.hpp"
#include "helpers.hpp"

#include <jwt-cpp/jwt.h>
#include <jwt-cpp/traits/nlohmann-json/traits.h>

#include <stdexcept>

namespace yaxi::test {

namespace {
using jwt_traits = ::jwt::traits::nlohmann_json;
} // namespace

TicketGenerator::TicketGenerator(std::string apiKeyId, const std::string &apiKeySecretBase64)
    : _apiKeyId(std::move(apiKeyId)), _secret(base64Decode(apiKeySecretBase64)) {
    if (_secret.empty()) {
        throw std::runtime_error("Failed to Base64-decode API key secret");
    }
}

std::string TicketGenerator::issue(const std::string &ticketId, const std::string &service,
                                   const std::optional<nlohmann::json> &data,
                                   std::optional<std::chrono::system_clock::time_point> exp) const {
    if (ticketId.size() != 36) {
        throw std::runtime_error("Invalid ticket ID: expected 36-character UUID");
    }

    nlohmann::json payloadData = {
        {"id", ticketId},
        {"service", service},
        // null when no service-specific data is provided
        {"data", data.value_or(nullptr)},
    };

    auto expiry = exp.value_or(std::chrono::system_clock::now() + std::chrono::minutes(5));

    std::string secretStr(reinterpret_cast<const char *>(_secret.data()), _secret.size());

    return ::jwt::create<jwt_traits>()
        .set_type("JWT")
        .set_key_id(_apiKeyId)
        .set_payload_claim("data", payloadData)
        .set_expires_at(expiry)
        .sign(::jwt::algorithm::hs256{secretStr});
}

std::string
TicketGenerator::accounts(const std::string &ticketId,
                          std::optional<std::chrono::system_clock::time_point> exp) const {
    return issue(ticketId, "Accounts", std::nullopt, exp);
}

std::string
TicketGenerator::balances(const std::string &ticketId,
                          std::optional<std::chrono::system_clock::time_point> exp) const {
    return issue(ticketId, "Balances", std::nullopt, exp);
}

std::string
TicketGenerator::transactions(const std::string &ticketId, const nlohmann::json &data,
                              std::optional<std::chrono::system_clock::time_point> exp) const {
    return issue(ticketId, "Transactions", data, exp);
}

std::string
TicketGenerator::collectPayment(const std::string &ticketId, const nlohmann::json &data,
                                std::optional<std::chrono::system_clock::time_point> exp) const {
    return issue(ticketId, "CollectPayment", data, exp);
}

std::string TicketGenerator::getId(const std::string &ticket) {
    auto decoded = ::jwt::decode<jwt_traits>(ticket);
    auto payload = decoded.get_payload_json();
    if (!payload.contains("data") || !payload["data"].is_object()) {
        throw std::runtime_error("ticket has no 'data' claim");
    }
    const auto &data = payload["data"];
    if (!data.contains("id") || !data["id"].is_string()) {
        throw std::runtime_error("ticket has no 'data.id' claim");
    }
    return data["id"].get<std::string>();
}

} // namespace yaxi::test
