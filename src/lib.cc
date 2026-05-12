#include <iterator>
#include <memory>
#include <stdexcept>

#include "routex-client-cxx/src/lib.rs.h"
#include "yaxi/routex-client.h"

using namespace std;
using namespace yaxi::internal;

namespace yaxi {
namespace {
template <typename T> inline unique_ptr<T> to_ptr(optional<T> opt) {
    return opt ? make_unique<T>(std::move(*opt)) : nullptr;
}

inline TriBool to_tri_bool(optional<bool> opt) {
    return opt ? opt.value() ? TriBool::True : TriBool::False : TriBool::None;
}

yaxi::internal::AccountReference convert_account_reference(yaxi::AccountReference ref) {
    return {ref.iban, to_ptr(ref.currency)};
}

template <typename U, typename T, typename F> vector<U> convert_vec(T const &in, F f) {
    vector<U> out;
    out.reserve(in.size());
    transform(in.cbegin(), in.cend(), back_inserter(out), f);
    return out;
}

inline vector<uint8_t> bytes(rust::Vec<uint8_t> const &bytes) {
    return vector<uint8_t>(make_move_iterator(bytes.begin()), make_move_iterator(bytes.end()));
}

inline optional<vector<uint8_t>> optional_bytes(rust::Vec<uint8_t> const &b) {
    return b.empty() ? nullopt : optional(bytes(b));
}

inline optional<string> optional_string(rust::String const &str) {
    return str.empty() ? nullopt : optional(string(str));
}

inline optional<uint32_t> optional_u32(unique_ptr<U32> const &val) {
    return val ? optional(val->val) : nullopt;
}

template <typename T> inline optional<T> optional_code(unique_ptr<U8> const &val) {
    return val ? optional(static_cast<T>(val->val)) : nullopt;
}

yaxi::ConnectionInfo convert_connection_info(yaxi::internal::ConnectionInfo info) {
    return {yaxi::ConnectionId(info.id),
            convert_vec<string>(info.countries, [](rust::String c) { return string(c); }),
            string(info.display_name),
            {info.credentials_full, info.credentials_user, info.credentials_none},
            optional_string(info.user_id),
            optional_string(info.password),
            optional_string(info.advice),
            string(info.logo_id),
            info.bics_set ? optional(convert_vec<string>(
                                info.bics, [](rust::String bic) { return string(bic); }))
                          : nullopt};
}

yaxi::Error error(internal::Error const &error) {
    switch (error.kind) {
    case ErrorKind::RequestError:
        return RequestError(string(error.string));
    case ErrorKind::UnexpectedError:
        return UnexpectedError(optional_string(error.string));
    case ErrorKind::Canceled:
        return Canceled();
    case ErrorKind::InvalidCredentials:
        return InvalidCredentials(optional_string(error.string));
    case ErrorKind::ServiceBlocked:
        return ServiceBlocked(optional_string(error.string),
                              optional_code<ServiceBlocked::Code>(error.code));
    case ErrorKind::Unauthorized:
        return Unauthorized(optional_string(error.string));
    case ErrorKind::ConsentExpired:
        return ConsentExpired(optional_string(error.string));
    case ErrorKind::AccessExceeded:
        return AccessExceeded(optional_string(error.string));
    case ErrorKind::PeriodOutOfBounds:
        return PeriodOutOfBounds(optional_string(error.string));
    case ErrorKind::UnsupportedProduct:
        return UnsupportedProduct(optional_string(error.string),
                                  optional_code<UnsupportedProduct::Reason>(error.code));
    case ErrorKind::PaymentFailed:
        return PaymentFailed(optional_string(error.string),
                             optional_code<PaymentFailed::Code>(error.code));
    case ErrorKind::UnexpectedValue:
        return UnexpectedValue(string(error.string));
    case ErrorKind::TicketError:
        return TicketError(string(error.string), static_cast<TicketError::Code>(error.code->val));
    case ErrorKind::ProviderError:
        return ProviderError(optional_string(error.string),
                             optional_code<ProviderError::Code>(error.code));
    case ErrorKind::ResponseError:
        return ResponseError(string(error.string));
    case ErrorKind::NotFound:
        return NotFound();
    default:
        return ResponseError(string(error.string));
    }
}

Result<string> string_result(internal::StringResult result) {
    if (result.error) {
        return Result<string>(error(*result.error));
    } else {
        return Result<string>(string(result.value));
    }
}

Result<ServiceResponse> service_result(internal::ServiceResult result) {
    if (result.error) {
        return Result<ServiceResponse>(error(*result.error));
    } else if (result.result) {
        return Result<ServiceResponse>(ServiceResult{
            string(result.result->jwt),
            optional_bytes(result.result->session),
            optional_bytes(result.result->connection_data),
        });
    } else if (result.dialog) {
        variant<Confirmation, Selection, Field> input;
        if (result.dialog->confirmation) {
            input = Confirmation{
                bytes(result.dialog->confirmation->context),
                optional_u32(result.dialog->confirmation->polling_delay_secs),
            };
        } else if (result.dialog->selection) {
            input = Selection{
                convert_vec<DialogOption>(result.dialog->selection->options,
                                          [](internal::DialogOption option) {
                                              return DialogOption{
                                                  string(option.key),
                                                  string(option.label),
                                                  optional_string(option.explanation),
                                              };
                                          }),
                bytes(result.dialog->selection->context),
            };
        } else if (result.dialog->field) {
            input = Field{
                result.dialog->field->type_,
                result.dialog->field->secrecy_level,
                optional_u32(result.dialog->field->min_length),
                optional_u32(result.dialog->field->max_length),
                bytes(result.dialog->field->context),
            };
        } else {
            throw runtime_error("Unexpected dialog variant");
        }

        return Result<ServiceResponse>(Dialog{
            result.dialog->context ? optional(*result.dialog->context) : nullopt,
            optional_string(result.dialog->message),
            result.dialog->image ? optional(Image{
                                       string(result.dialog->image->mime_type),
                                       bytes(result.dialog->image->data),
                                       optional_bytes(result.dialog->image->hhd_uc_data),
                                   })
                                 : nullopt,
            std::move(input),
        });
    } else if (result.redirect) {
        return Result<ServiceResponse>(Redirect{
            string(result.redirect->url),
            bytes(result.redirect->context),
        });
    } else if (result.redirect_handle) {
        return Result<ServiceResponse>(RedirectHandle{
            string(result.redirect_handle->handle),
            bytes(result.redirect_handle->context),
        });
    } else {
        throw runtime_error("Unexpected result variant");
    }
}

template <typename... Ts> struct overloaded : Ts... {
    using Ts::operator()...;
};
template <typename... Ts> overloaded(Ts...) -> overloaded<Ts...>;
} // namespace

const AccountField::OptionalStringField AccountField::Iban{uint8_t(internal::AccountField::Iban)};
const AccountField::OptionalStringField AccountField::Number{
    uint8_t(internal::AccountField::Number)};
const AccountField::OptionalStringField AccountField::Bic{uint8_t(internal::AccountField::Bic)};
const AccountField::OptionalStringField AccountField::BankCode{
    uint8_t(internal::AccountField::BankCode)};
const AccountField::CurrencyField AccountField::Currency{uint8_t(internal::AccountField::Currency)};
const AccountField::OptionalStringField AccountField::Name{uint8_t(internal::AccountField::Name)};
const AccountField::OptionalStringField AccountField::DisplayName{
    uint8_t(internal::AccountField::DisplayName)};
const AccountField::OptionalStringField AccountField::OwnerName{
    uint8_t(internal::AccountField::OwnerName)};
const AccountField::OptionalStringField AccountField::ProductName{
    uint8_t(internal::AccountField::ProductName)};
const AccountField::StatusField AccountField::Status{uint8_t(internal::AccountField::Status)};
const AccountField::TypeField AccountField::Type{uint8_t(internal::AccountField::Type)};

struct AccountFilter::Inner {
    Inner(internal::AccountFilter inner) : inner(std::move(inner)) {}
    internal::AccountFilter inner;
};

AccountFilter::AccountFilter(AccountFilter::Inner inner)
    : inner(make_unique<AccountFilter::Inner>(std::move(inner))) {}
AccountFilter::~AccountFilter() = default;
AccountFilter::AccountFilter(AccountFilter &&) noexcept = default;
AccountFilter &AccountFilter::operator=(AccountFilter &&) noexcept = default;

AccountFilter AccountFilter::operator&&(AccountFilter &&filter) && {
    return AccountFilter(internal::AccountFilter{
        nullptr,
        nullptr,
        nullptr,
        nullptr,
        nullptr,
        make_unique<internal::AccountFilter>(std::move(this->inner->inner)),
        make_unique<internal::AccountFilter>(std::move(filter.inner->inner)),
        nullptr,
        nullptr,
    });
}

AccountFilter AccountFilter::operator||(AccountFilter &&filter) && {
    return AccountFilter(internal::AccountFilter{
        nullptr,
        nullptr,
        nullptr,
        nullptr,
        nullptr,
        make_unique<internal::AccountFilter>(std::move(this->inner->inner)),
        nullptr,
        make_unique<internal::AccountFilter>(std::move(filter.inner->inner)),
        nullptr,
    });
}

AccountFilter AccountFilter::supports(const SupportedService &service) {
    return AccountFilter(internal::AccountFilter{
        nullptr,
        nullptr,
        nullptr,
        nullptr,
        nullptr,
        nullptr,
        nullptr,
        nullptr,
        make_unique<SupportedService>(service),
    });
}

AccountFilter AccountField::OptionalStringField::operator==(const optional<string> &val) const {
    return AccountFilter(internal::AccountFilter{
        make_unique<internal::AccountField>(static_cast<internal::AccountField>(value)),
        nullptr,
        to_ptr(val),
        nullptr,
        nullptr,
        nullptr,
        nullptr,
        nullptr,
        nullptr,
    });
}

AccountFilter AccountField::OptionalStringField::operator!=(const optional<string> &val) const {
    return AccountFilter(internal::AccountFilter{
        nullptr,
        make_unique<internal::AccountField>(static_cast<internal::AccountField>(value)),
        to_ptr(val),
        nullptr,
        nullptr,
        nullptr,
        nullptr,
        nullptr,
        nullptr,
    });
}

AccountFilter AccountField::CurrencyField::operator==(const string &val) const {
    return AccountFilter(internal::AccountFilter{
        make_unique<internal::AccountField>(static_cast<internal::AccountField>(value)),
        nullptr,
        make_unique<string>(val),
        nullptr,
        nullptr,
        nullptr,
        nullptr,
        nullptr,
        nullptr,
    });
}

AccountFilter AccountField::CurrencyField::operator!=(const string &val) const {
    return AccountFilter(internal::AccountFilter{
        nullptr,
        make_unique<internal::AccountField>(static_cast<internal::AccountField>(value)),
        make_unique<string>(val),
        nullptr,
        nullptr,
        nullptr,
        nullptr,
        nullptr,
        nullptr,
    });
}

AccountFilter AccountField::StatusField::operator==(const optional<AccountStatus> &val) const {
    return AccountFilter(internal::AccountFilter{
        make_unique<internal::AccountField>(static_cast<internal::AccountField>(value)),
        nullptr,
        nullptr,
        to_ptr(val),
        nullptr,
        nullptr,
        nullptr,
        nullptr,
        nullptr,
    });
}
AccountFilter AccountField::StatusField::operator!=(const optional<AccountStatus> &val) const {
    return AccountFilter(internal::AccountFilter{
        nullptr,
        make_unique<internal::AccountField>(static_cast<internal::AccountField>(value)),
        nullptr,
        to_ptr(val),
        nullptr,
        nullptr,
        nullptr,
        nullptr,
        nullptr,
    });
}

AccountFilter AccountField::TypeField::operator==(const optional<AccountType> &val) const {
    return AccountFilter(internal::AccountFilter{
        make_unique<internal::AccountField>(static_cast<internal::AccountField>(value)),
        nullptr,
        nullptr,
        nullptr,
        to_ptr(val),
        nullptr,
        nullptr,
        nullptr,
        nullptr,
    });
}
AccountFilter AccountField::TypeField::operator!=(const optional<AccountType> &val) const {
    return AccountFilter(internal::AccountFilter{
        nullptr,
        make_unique<internal::AccountField>(static_cast<internal::AccountField>(value)),
        nullptr,
        nullptr,
        to_ptr(val),
        nullptr,
        nullptr,
        nullptr,
        nullptr,
    });
}

struct RoutexClient::Inner {
    rust::Box<internal::RoutexClient> bridge;

    explicit Inner(rust::Box<internal::RoutexClient> b) : bridge(std::move(b)) {}
};

RoutexClient::RoutexClient(const optional<string> &url)
    : inner(make_unique<Inner>(new_routex_client(to_ptr(url)))) {}

RoutexClient::~RoutexClient() = default;

RoutexClient::RoutexClient(RoutexClient &&) noexcept = default;
RoutexClient &RoutexClient::operator=(RoutexClient &&) noexcept = default;

/**
 * System version for the currently established session
 */
optional<string> RoutexClient::systemVersion(const string &ticketId) const {
    return optional_string(inner->bridge->system_version(ticketId));
};

optional<TraceId> RoutexClient::traceId() const {
    return optional_bytes(inner->bridge->trace_id());
}

Result<string> RoutexClient::trace(const string &ticket, const TraceId &traceId) const {
    return string_result(inner->bridge->trace(ticket, traceId));
}

void RoutexClient::setRedirectUri(const string &redirectUri) {
    inner->bridge->set_redirect_uri(redirectUri);
}

void RoutexClient::setRecurringConsents(bool enabled) {
    inner->bridge->set_recurring_consents(enabled);
}

Result<string> RoutexClient::registerRedirectUri(const string &ticket, const string &handle,
                                                 const string &redirectUri) const {
    return string_result(inner->bridge->register_redirect_uri(ticket, handle, redirectUri));
}

Result<vector<ConnectionInfo>>
RoutexClient::search(const string &ticket, const vector<SearchFilter> &filters, bool ibanDetection,
                     const optional<size_t> &limit, const vector<Details> &details) const {
    auto result = inner->bridge->search(
        ticket,
        convert_vec<internal::SearchFilter>(
            filters,
            [](SearchFilter filter) {
                return visit(
                    overloaded{
                        [](const CountriesSearchFilter &f) {
                            return internal::SearchFilter{
                                make_unique<vector<string>>(f.countries),
                                nullptr,
                                nullptr,
                                nullptr,
                                nullptr,
                            };
                        },
                        [](const NameSearchFilter &f) {
                            return internal::SearchFilter{
                                nullptr, make_unique<string>(f.name), nullptr, nullptr, nullptr,
                            };
                        },
                        [](const BicSearchFilter &f) {
                            return internal::SearchFilter{
                                nullptr, nullptr, make_unique<string>(f.bic), nullptr, nullptr,
                            };
                        },
                        [](const BankCodeSearchFilter &f) {
                            return internal::SearchFilter{
                                nullptr, nullptr, nullptr, make_unique<string>(f.bankCode), nullptr,
                            };
                        },
                        [](const TermSearchFilter &f) {
                            return internal::SearchFilter{
                                nullptr, nullptr, nullptr, nullptr, make_unique<string>(f.term),
                            };
                        }},
                    filter);
            }),
        ibanDetection, limit ? make_unique<Usize>(Usize{std::move(*limit)}) : nullptr, details);

    if (result.error) {
        return Result<vector<ConnectionInfo>>(error(*result.error));
    } else {
        return Result<vector<ConnectionInfo>>(
            convert_vec<ConnectionInfo>(result.value, convert_connection_info));
    }
}

Result<ConnectionInfo> RoutexClient::info(const string &ticket, ConnectionId connectionId) const {
    auto result = inner->bridge->info(ticket, connectionId);

    if (result.error) {
        return Result<ConnectionInfo>(error(*result.error));
    } else {
        return Result<ConnectionInfo>(convert_connection_info(*result.value));
    }
}

Result<ServiceResponse> RoutexClient::accounts(const Credentials &credentials, const string &ticket,
                                               const vector<AccountField> &fields,
                                               const optional<AccountFilter> &filter,
                                               const optional<Session> &session,
                                               const optional<bool> &recurringConsents) const {
    return service_result(inner->bridge->accounts(
        credentials.connectionId, to_ptr(credentials.userId), to_ptr(credentials.password),
        to_ptr(credentials.connectionData), ticket,
        convert_vec<internal::AccountField>(
            fields, [](uint8_t field) { return static_cast<internal::AccountField>(field); }),
        filter ? make_unique<internal::AccountFilter>(std::move(filter->inner->inner)) : nullptr,
        to_ptr(session), to_tri_bool(recurringConsents)));
}

Result<ServiceResponse> RoutexClient::respondAccounts(const string &ticket,
                                                      const InputContext &context,
                                                      const string &response) const {
    return service_result(inner->bridge->respond_accounts(ticket, context, response));
}

Result<ServiceResponse> RoutexClient::confirmAccounts(const string &ticket,
                                                      const ConfirmationContext &context) const {
    return service_result(inner->bridge->confirm_accounts(ticket, context));
}

Result<ServiceResponse> RoutexClient::balances(const Credentials &credentials, const string &ticket,
                                               const vector<AccountReference> &accounts,
                                               const optional<Session> &session,
                                               const optional<bool> &recurringConsents) const {
    return service_result(inner->bridge->balances(
        credentials.connectionId, to_ptr(credentials.userId), to_ptr(credentials.password),
        to_ptr(credentials.connectionData), ticket,
        convert_vec<internal::AccountReference>(accounts, convert_account_reference),
        to_ptr(session), to_tri_bool(recurringConsents)));
}

Result<ServiceResponse> RoutexClient::respondBalances(const string &ticket,
                                                      const InputContext &context,
                                                      const string &response) const {
    return service_result(inner->bridge->respond_balances(ticket, context, response));
}

Result<ServiceResponse> RoutexClient::confirmBalances(const string &ticket,
                                                      const ConfirmationContext &context) const {
    return service_result(inner->bridge->confirm_balances(ticket, context));
}

Result<ServiceResponse> RoutexClient::transactions(const Credentials &credentials,
                                                   const string &ticket,
                                                   const optional<Session> &session,
                                                   const optional<bool> &recurringConsents) const {
    return service_result(inner->bridge->transactions(
        credentials.connectionId, to_ptr(credentials.userId), to_ptr(credentials.password),
        to_ptr(credentials.connectionData), ticket, to_ptr(session),
        to_tri_bool(recurringConsents)));
}

Result<ServiceResponse> RoutexClient::respondTransactions(const string &ticket,
                                                          const InputContext &context,
                                                          const string &response) const {
    return service_result(inner->bridge->respond_transactions(ticket, context, response));
}

Result<ServiceResponse>
RoutexClient::confirmTransactions(const string &ticket, const ConfirmationContext &context) const {
    return service_result(inner->bridge->confirm_transactions(ticket, context));
}

Result<ServiceResponse> RoutexClient::collectPayment(
    const Credentials &credentials, const string &ticket, const optional<AccountReference> &account,
    const optional<Session> &session, const optional<bool> &recurringConsents) const {
    return service_result(inner->bridge->collect_payment(
        credentials.connectionId, to_ptr(credentials.userId), to_ptr(credentials.password),
        to_ptr(credentials.connectionData), ticket,
        account ? make_unique<internal::AccountReference>(convert_account_reference(*account))
                : nullptr,
        to_ptr(session), to_tri_bool(recurringConsents)));
}

Result<ServiceResponse> RoutexClient::respondCollectPayment(const string &ticket,
                                                            const InputContext &context,
                                                            const string &response) const {
    return service_result(inner->bridge->respond_collect_payment(ticket, context, response));
}

Result<ServiceResponse>
RoutexClient::confirmCollectPayment(const string &ticket,
                                    const ConfirmationContext &context) const {
    return service_result(inner->bridge->confirm_collect_payment(ticket, context));
}

Result<ServiceResponse> RoutexClient::transfer(
    const Credentials &credentials, const string &ticket, const PaymentProduct &product,
    const vector<TransferDetails> &details, const optional<AccountReference> &debtorAccount,
    const optional<string> &debtorName,
    const optional<chrono::time_point<chrono::system_clock>> &requestedExecutionDate,
    const optional<Session> &session, const optional<bool> &recurringConsents) const {
    return service_result(inner->bridge->transfer(
        credentials.connectionId, to_ptr(credentials.userId), to_ptr(credentials.password),
        to_ptr(credentials.connectionData), ticket, product,
        convert_vec<internal::TransferDetails>(
            details,
            [](TransferDetails details) {
                return internal::TransferDetails{
                    to_ptr(details.endToEndIdentification),
                    make_unique<string>(details.amount),
                    make_unique<string>(details.currency),
                    make_unique<string>(details.creditorIban),
                    to_ptr(details.creditorAgentBic),
                    make_unique<string>(details.creditorName),
                    details.creditorAddress ? make_unique<string>(details.creditorAddress->townName)
                                            : nullptr,
                    details.creditorAddress ? make_unique<string>(details.creditorAddress->country)
                                            : nullptr,
                    to_ptr(details.remittance),
                    to_ptr(details.chargeBearer),
                };
            }),
        debtorAccount
            ? make_unique<internal::AccountReference>(convert_account_reference(*debtorAccount))
            : nullptr,
        to_ptr(debtorName),
        requestedExecutionDate
            ? make_unique<I64>(I64{chrono::duration_cast<chrono::seconds>(
                                       (*requestedExecutionDate).time_since_epoch())
                                       .count()})
            : nullptr,
        to_ptr(session), to_tri_bool(recurringConsents)));
}

Result<ServiceResponse> RoutexClient::respondTransfer(const string &ticket,
                                                      const InputContext &context,
                                                      const string &response) const {
    return service_result(inner->bridge->respond_transfer(ticket, context, response));
}

Result<ServiceResponse> RoutexClient::confirmTransfer(const string &ticket,
                                                      const ConfirmationContext &context) const {
    return service_result(inner->bridge->confirm_transfer(ticket, context));
}
} // namespace yaxi
