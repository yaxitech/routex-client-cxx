#include "helpers.hpp"
#include "ticket.hpp"

#include <catch2/catch_test_macros.hpp>
#include <yaxi/routex-client.h>

#include <nlohmann/json.hpp>

#include <chrono>
#include <cstddef>
#include <optional>
#include <string>
#include <variant>
#include <vector>

using namespace yaxi;
using namespace yaxi::test;
using json = nlohmann::json;

namespace {
constexpr auto DEMO_CONNECTION_ID = "connection-96386142-60e5-4ca9-abcf-944efce5bc1e";

ServiceResponse &requireServiceResponse(Result<ServiceResponse> &result) {
    REQUIRE_THAT(result, HoldsAlternative<ServiceResponse>());
    return std::get<ServiceResponse>(result);
}

ServiceResult &requireServiceResult(Result<ServiceResponse> &result) {
    auto &resp = requireServiceResponse(result);
    REQUIRE_THAT(resp, HoldsAlternative<ServiceResult>());
    return std::get<ServiceResult>(resp);
}

Dialog &requireDialog(Result<ServiceResponse> &result) {
    auto &resp = requireServiceResponse(result);
    REQUIRE_THAT(resp, HoldsAlternative<Dialog>());
    return std::get<Dialog>(resp);
}

} // namespace

TEST_CASE_METHOD(OnlineFixture, "Service CollectPayment", "[online][collect_payment]") {
    auto accountsTicket = generator.accounts(uuidV4());

    Credentials creds{};
    creds.connectionId = DEMO_CONNECTION_ID;
    creds.userId = "input";

    auto filter = (AccountField::Iban != std::optional<std::string>{}) &&
                  ((AccountField::Type == std::optional{AccountType::Savings}) ||
                   (AccountField::Type == std::optional{AccountType::Current}) ||
                   (AccountField::Type == std::optional<AccountType>{})) &&
                  AccountFilter::supports(SupportedService::CollectPayment);

    std::vector<AccountField> fields{
        AccountField::Iban,        AccountField::Bic,       AccountField::Name,
        AccountField::DisplayName, AccountField::OwnerName, AccountField::Currency,
    };

    auto accountsResult = client.accounts(creds, accountsTicket, fields, std::move(filter));
    auto &dialog = requireDialog(accountsResult);
    REQUIRE(dialog.context == std::optional{DialogContext::Sca});
    REQUIRE_THAT(dialog.input, HoldsAlternative<Field>());
    auto &fieldInput = std::get<Field>(dialog.input);

    auto respondResult = client.respondAccounts(accountsTicket, fieldInput.context, "133742");
    auto &accountsServiceResult = requireServiceResult(respondResult);

    auto payload = jwtDecodeUnverified(accountsServiceResult.jwt);
    auto firstAccount = payload.at("data").at("data").at(0);
    REQUIRE(firstAccount == json{
                                {"bic", "BYLADEM1001"},
                                {"iban", "DE02120300000000202051"},
                                {"currency", "EUR"},
                                {"ownerName", "Dr. Peter Steiger"},
                            });

    REQUIRE(payload.at("data").at("ticketId") == TicketGenerator::getId(accountsTicket));

    std::vector<std::string> ownerNames;
    for (auto &acc : payload.at("data").at("data")) {
        ownerNames.push_back(acc.at("ownerName"));
    }
    REQUIRE(ownerNames == std::vector<std::string>{"Dr. Peter Steiger"});

    auto selectedAccount = firstAccount.at("iban").get<std::string>();

    json paymentData = {
        {"amount", {{"amount", "100"}, {"currency", "EUR"}}},
        {"creditorAccount", {{"iban", "DE79430609671288143100"}}},
        {"creditorName", "YAXI GmbH"},
        {"remittance", "Sign-up fee routex 123456789"},
    };
    auto paymentTicket = generator.collectPayment(uuidV4(), paymentData);

    Credentials payCreds{};
    payCreds.connectionId = DEMO_CONNECTION_ID;
    payCreds.userId = "confirmation";
    payCreds.connectionData = accountsServiceResult.connectionData;
    AccountReference account{};
    account.iban = selectedAccount;

    auto payResult =
        client.collectPayment(payCreds, paymentTicket, account, accountsServiceResult.session);
    auto &payDialog = requireDialog(payResult);
    REQUIRE_THAT(payDialog.input, HoldsAlternative<Confirmation>());
    auto &confirmInput = std::get<Confirmation>(payDialog.input);
    REQUIRE(confirmInput.pollingDelaySecs == std::optional<uint32_t>{1});

    auto confirmResult = client.confirmCollectPayment(paymentTicket, confirmInput.context);
    auto &paySr = requireServiceResult(confirmResult);
    auto paymentPayload = jwtDecodeUnverified(paySr.jwt);
    REQUIRE(paymentPayload.at("data").at("data") == json{{"status", "Accepted"}});
}

TEST_CASE_METHOD(OnlineFixture, "Service CollectPayment with debtor identification",
                 "[online][collect_payment]") {

    json data = {
        {"amount", {{"amount", "100"}, {"currency", "EUR"}}},
        {"creditorAccount", {{"iban", "DE79430609671288143100"}}},
        {"creditorName", "YAXI GmbH"},
        {"remittance", "Sign-up fee routex 123456789"},
        {"fields", {"debtorIban", "debtorName"}},
    };
    auto paymentTicket = generator.collectPayment(uuidV4(), data);

    Credentials creds{};
    creds.connectionId = DEMO_CONNECTION_ID;
    creds.userId = "result";
    AccountReference account{};
    account.iban = "DE02120300000000202051";
    auto result = client.collectPayment(creds, paymentTicket, account);

    auto &sr = requireServiceResult(result);
    auto payload = jwtDecodeUnverified(sr.jwt);
    REQUIRE(payload.at("data").at("data") == json{
                                                 {"debtorName", "Dr. Peter Steiger"},
                                                 {"debtorIban", "DE02120300000000202051"},
                                                 {"status", "Accepted"},
                                             });
}

TEST_CASE_METHOD(OnlineFixture, "Service Transfer", "[online][transfer]") {
    auto transferTicket = generator.issue(uuidV4(), "Transfer");

    Credentials creds{};
    creds.connectionId = DEMO_CONNECTION_ID;
    creds.userId = "confirmation";
    TransferDetails details{};
    details.amount = "1";
    details.currency = "EUR";
    details.creditorIban = "DE79430609671288143100";
    details.creditorName = "YAXI GmbH";

    auto result = client.transfer(creds, transferTicket, PaymentProduct::DefaultSepaCreditTransfer,
                                  {details});
    auto &dialog = requireDialog(result);
    REQUIRE(dialog.context == std::optional{DialogContext::Sca});
    REQUIRE_THAT(dialog.input, HoldsAlternative<Confirmation>());
    auto &confirmInput = std::get<Confirmation>(dialog.input);
    REQUIRE(confirmInput.pollingDelaySecs == std::optional<uint32_t>{1});

    auto confirmResult = client.confirmTransfer(transferTicket, confirmInput.context);
    auto &sr = requireServiceResult(confirmResult);

    auto payload = jwtDecodeUnverified(sr.jwt);
    REQUIRE(payload.at("data").at("ticketId") == TicketGenerator::getId(transferTicket));
    REQUIRE(payload.at("data").at("data") == json{{"status", "Accepted"}});
}

TEST_CASE_METHOD(OnlineFixture, "Service Balances", "[online][balances]") {
    auto balancesTicket = generator.issue(uuidV4(), "Balances");

    Credentials creds{};
    creds.connectionId = DEMO_CONNECTION_ID;
    creds.userId = "confirmation";
    std::vector<AccountReference> accounts{
        AccountReference{.iban = "DE02120300000000202051", .currency = std::string{"EUR"}},
    };

    auto result = client.balances(creds, balancesTicket, accounts);
    auto &dialog = requireDialog(result);
    REQUIRE(dialog.context == std::optional{DialogContext::Sca});
    REQUIRE_THAT(dialog.input, HoldsAlternative<Confirmation>());
    auto &confirmInput = std::get<Confirmation>(dialog.input);
    REQUIRE(confirmInput.pollingDelaySecs == std::optional<uint32_t>{1});

    auto confirmResult = client.confirmBalances(balancesTicket, confirmInput.context);
    auto &sr = requireServiceResult(confirmResult);
    auto payload = jwtDecodeUnverified(sr.jwt);
    REQUIRE(payload.at("data").at("ticketId") == TicketGenerator::getId(balancesTicket));
    REQUIRE(payload.at("data").at("data") ==
            json{
                {"balances",
                 {{
                     {"account", {{"currency", "EUR"}, {"iban", "DE02120300000000202051"}}},
                     {"balances",
                      {
                          {{"amount", "8877.78"},
                           {"balanceType", "Booked"},
                           {"currency", "EUR"},
                           {"creditLimitIncluded", false}},
                          {{"amount", "8947.64"},
                           {"balanceType", "Available"},
                           {"currency", "EUR"},
                           {"creditLimitIncluded", false}},
                      }},
                 }}},
            });
}

TEST_CASE_METHOD(OnlineFixture, "Connection info", "[online][search]") {
    auto accountsTicket = generator.accounts(uuidV4());

    std::vector<SearchFilter> filters{
        TermSearchFilter{.term = "sparkasse"},
        TermSearchFilter{.term = "stadt"},
    };

    auto result = client.search(accountsTicket, filters, /*ibanDetection=*/true, /*limit=*/20);
    REQUIRE_THAT(result, HoldsAlternative<std::vector<ConnectionInfo>>());
    auto &infos = std::get<std::vector<ConnectionInfo>>(result);

    struct ExpectedInfo {
        std::string id;
        std::string displayName;
    };
    const std::vector<ExpectedInfo> expected{
        {"connection-3c0ec6db-f1e0-4bfb-be0b-4e5af8a7ad4e", "Sparkasse Duderstadt"},
        {"connection-8af40d65-8393-41c4-8c78-09cc652a8f26", "Stadtsparkasse Wedel"},
        {"connection-c5d12d75-0c31-4883-aaaa-f5535e9c82da", "Stadtsparkasse Dessau"},
        {"connection-6227b304-71d9-4cab-b7c1-1bcfcc16fb23", "Stadtsparkasse Rahden"},
        {"connection-099c2821-a46e-4d88-a0c1-5feda0ab63e2", "Stadtsparkasse Rheine"},
        {"connection-499673bd-9907-4c26-88fc-172af8c8032c", "Stadtsparkasse Bocholt"},
        {"connection-3c60b72f-f706-43f8-883c-1dbd1b06ac6c", "Stadtsparkasse München"},
        {"connection-6eb60518-dc4c-4d09-aa42-e38453d5c366", "Stadtsparkasse Schwedt"},
        {"connection-069d6c77-db02-4037-8ccd-7d99b1353c82", "Stadtsparkasse Augsburg"},
        {"connection-8d762a37-df3d-44c3-ba0f-c928a263b360", "Stadtsparkasse Cuxhaven"},
        {"connection-51ecdf0f-6984-4d6c-9420-ec9ba66a01c8", "Stadt-Sparkasse Solingen"},
        {"connection-8eca5600-89aa-487b-b766-a2a8c9bf6e07", "Stadtsparkasse Lengerich"},
        {"connection-41c751be-16dd-4527-9ba8-98ccf65f1520", "Stadtsparkasse Remscheid"},
        {"connection-1d4e32aa-6d1c-4ab1-81ff-7f6d1870af4e", "Stadtsparkasse Wuppertal"},
        {"connection-889582a7-49b7-4b04-8095-ec82d3e820aa", "Stadtsparkasse Düsseldorf"},
        {"connection-931f52d1-b108-4914-a75a-5b3d0715332f", "Stadtsparkasse Oberhausen"},
        {"connection-0ebbaba4-a62b-48fa-b18c-b1f297eb5cc7", "Sparkasse Arnstadt-Ilmenau"},
        {"connection-24570ecb-a2c0-4e2b-9448-1a1eef9c4009", "Stadt-Sparkasse Langenfeld"},
        {"connection-a997832f-a868-45e5-8229-d0fb66af8e46", "Stadtsparkasse Bad Pyrmont"},
        {"connection-7a30cb24-fbc6-44ad-a031-c00a1c248094", "Stadtsparkasse Grebenstein"},
    };

    REQUIRE(infos.size() == expected.size());
    for (std::size_t i = 0; i < expected.size(); ++i) {
        INFO("entry " << i);
        CHECK(infos[i].id == expected[i].id);
        CHECK(infos[i].displayName == expected[i].displayName);
        CHECK(infos[i].countries == std::vector<std::string>{"DE"});
        CHECK(infos[i].credentials.full == true);
        CHECK(infos[i].credentials.userId == false);
        CHECK(infos[i].credentials.none == true);
        CHECK(infos[i].logoId == "sparkasse");
        CHECK(infos[i].userId == std::optional<std::string>{"Anmeldename"});
        CHECK(infos[i].password == std::optional<std::string>{"Online-Banking-PIN"});
    }
}

TEST_CASE_METHOD(OnlineFixture, "Search with BIC details", "[online][search]") {
    auto accountsTicket = generator.accounts(uuidV4());
    std::vector<SearchFilter> filters{NameSearchFilter{.name = "C24 Bank"}};
    auto result = client.search(accountsTicket, filters, /*ibanDetection=*/false,
                                /*limit=*/std::nullopt, {Details::Bics});
    REQUIRE_THAT(result, HoldsAlternative<std::vector<ConnectionInfo>>());
    auto &infos = std::get<std::vector<ConnectionInfo>>(result);
    REQUIRE_FALSE(infos.empty());
    for (auto &info : infos) {
        REQUIRE(info.bics.has_value());
        CHECK(*info.bics == std::vector<std::string>{"DEFFDEFFXXX"});
    }
}

TEST_CASE_METHOD(OnlineFixture, "Service Transactions", "[online][transactions]") {
    json data = {
        {"account", {{"iban", "DE02120300000000202051"}, {"currency", "EUR"}}},
        {"range", {{"from", "2019-01-13"}}},
    };
    auto ticket = generator.transactions(uuidV4(), data);

    Credentials creds{};
    creds.connectionId = DEMO_CONNECTION_ID;
    creds.userId = "confirmation";
    auto result = client.transactions(creds, ticket);
    auto &dialog = requireDialog(result);
    REQUIRE(dialog.context == std::optional{DialogContext::Sca});
    REQUIRE_THAT(dialog.input, HoldsAlternative<Confirmation>());
    auto &confirmInput = std::get<Confirmation>(dialog.input);
    REQUIRE(confirmInput.pollingDelaySecs == std::optional<uint32_t>{1});

    auto confirmResult = client.confirmTransactions(ticket, confirmInput.context);
    auto &sr = requireServiceResult(confirmResult);
    auto payload = jwtDecodeUnverified(sr.jwt);

    REQUIRE(payload.at("data").at("ticketId") == TicketGenerator::getId(ticket));
    REQUIRE(payload.at("data").at("data").size() == 63);
    REQUIRE(
        payload.at("data").at("data").at(0) ==
        json{
            {"amount", {{"amount", "-0.95"}, {"currency", "EUR"}}},
            {"bankTransactionCodes",
             {
                 {{"iso", {{"domain", "PMNT"}, {"family", "ICDT"}, {"subFamily", "STDO"}}}},
                 {{"swift", "DDT"}},
                 {{"national", {{"code", "106"}, {"country", "DE"}}}},
             }},
            {"bookingDate", "2025-07-29"},
            {"creditor", {{"iban", "DE96120300009005290904"}, {"name", "DHL.K53VEV55WWVE/BONN"}}},
            {"debtor", {{"iban", "DE02120300000000202051"}, {"name", "ISSUER"}}},
            {"endToEndId", "485209459755938"},
            {"purposeCode", "DCRD"},
            {"remittanceInformation", {"VISA Debitkartenumsatz"}},
            {"status", "Booked"},
            {"valueDate", "2025-07-29"},
        });
}

TEST_CASE_METHOD(OnlineFixture, "Redirect: RedirectHandle", "[online][redirect]") {
    auto ticket = generator.accounts(uuidV4());
    Credentials creds{};
    creds.connectionId = DEMO_CONNECTION_ID;
    creds.userId = "redirect";

    auto result = client.accounts(creds, ticket, {});
    auto &resp = requireServiceResponse(result);
    REQUIRE_THAT(resp, HoldsAlternative<RedirectHandle>());
    auto &handle = std::get<RedirectHandle>(resp);

    auto registerResult =
        client.registerRedirectUri(ticket, handle.handle, "myapp://redirect/?context=signup");
    REQUIRE_THAT(registerResult, HoldsAlternative<std::string>());
    auto &redirectUrl = std::get<std::string>(registerResult);

    REQUIRE(httpGetRedirectLocation(redirectUrl) == "myapp://redirect/?context=signup");

    auto confirmResult = client.confirmAccounts(ticket, handle.context);
    auto &confirmResp = requireServiceResponse(confirmResult);
    REQUIRE_THAT(confirmResp, HoldsAlternative<ServiceResult>());
}

TEST_CASE_METHOD(OnlineFixture, "Redirect: setRedirectUri", "[online][redirect]") {
    client.setRedirectUri("myapp://redirect/?context=signup");

    auto ticket = generator.accounts(uuidV4());
    Credentials creds{};
    creds.connectionId = DEMO_CONNECTION_ID;
    creds.userId = "redirect";

    auto result = client.accounts(creds, ticket, {});
    auto &resp = requireServiceResponse(result);
    REQUIRE_THAT(resp, HoldsAlternative<Redirect>());
    auto &redirect = std::get<Redirect>(resp);

    REQUIRE(httpGetRedirectLocation(redirect.url) == "myapp://redirect/?context=signup");

    auto confirmResult = client.confirmAccounts(ticket, redirect.context);
    auto &confirmResp = requireServiceResponse(confirmResult);
    REQUIRE_THAT(confirmResp, HoldsAlternative<ServiceResult>());
}

TEST_CASE_METHOD(OnlineFixture, "Error: expired ticket", "[online][errors]") {
    auto expiredAt = std::chrono::system_clock::now() - std::chrono::minutes(5);
    auto ticket = generator.accounts(uuidV4(), expiredAt);

    auto result = client.info(ticket, DEMO_CONNECTION_ID);
    REQUIRE_THAT(result, HoldsAlternative<Error>());
    auto &err = std::get<Error>(result);
    REQUIRE_THAT(err, HoldsAlternative<TicketError>());
    auto &ticketError = std::get<TicketError>(err);
    REQUIRE(std::string{ticketError.what()} == "Ticket is expired");
    REQUIRE(ticketError.code == TicketError::Code::Expired);
}

TEST_CASE_METHOD(OnlineFixture, "Traces: retrieve", "[online][traces]") {
    auto traceIdBefore = client.traceId();

    auto ticket = generator.accounts(uuidV4());
    auto infoResult = client.info(ticket, DEMO_CONNECTION_ID);
    REQUIRE_THAT(infoResult, HoldsAlternative<ConnectionInfo>());
    auto &info = std::get<ConnectionInfo>(infoResult);
    REQUIRE(info.id == DEMO_CONNECTION_ID);

    auto traceIdAfter = client.traceId();
    REQUIRE(traceIdAfter.has_value());
    REQUIRE(traceIdBefore != traceIdAfter);

    auto traceResult = client.trace(ticket, *traceIdAfter);
    REQUIRE_THAT(traceResult, HoldsAlternative<std::string>());
    auto &trace = std::get<std::string>(traceResult);
    REQUIRE_FALSE(trace.empty());

    constexpr auto ageHeader = "-----BEGIN AGE ENCRYPTED FILE-----";
    constexpr auto ageFooter = "-----END AGE ENCRYPTED FILE-----\n";
    if (trace.starts_with(ageHeader)) {
        REQUIRE(trace.ends_with(ageFooter));
    } else {
        auto parsed = json::parse(trace);
        REQUIRE(parsed.contains("data"));
        REQUIRE(parsed.at("data").is_array());
        REQUIRE_FALSE(parsed.at("data").empty());
        REQUIRE(parsed.at("data").at(0).contains("traceID"));
    }
}
