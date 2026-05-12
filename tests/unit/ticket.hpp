#pragma once

#include <nlohmann/json.hpp>

#include <chrono>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace yaxi::test {

class TicketGenerator {
  public:
    TicketGenerator(std::string apiKeyId, const std::string &apiKeySecretBase64);

    std::string
    issue(const std::string &ticketId, const std::string &service,
          const std::optional<nlohmann::json> &data = std::nullopt,
          std::optional<std::chrono::system_clock::time_point> exp = std::nullopt) const;

    std::string
    accounts(const std::string &ticketId,
             std::optional<std::chrono::system_clock::time_point> exp = std::nullopt) const;

    std::string
    balances(const std::string &ticketId,
             std::optional<std::chrono::system_clock::time_point> exp = std::nullopt) const;

    std::string
    transactions(const std::string &ticketId, const nlohmann::json &data,
                 std::optional<std::chrono::system_clock::time_point> exp = std::nullopt) const;

    std::string
    collectPayment(const std::string &ticketId, const nlohmann::json &data,
                   std::optional<std::chrono::system_clock::time_point> exp = std::nullopt) const;

    static std::string getId(const std::string &ticket);

  private:
    std::string _apiKeyId;
    std::vector<uint8_t> _secret;
};

} // namespace yaxi::test
