use anyhow::{anyhow, bail};
use chrono::{TimeZone, Utc};
use cxx::{CxxString, CxxVector, UniquePtr};
use paste::paste;
use reqwest::header::InvalidHeaderValue;
use routex_api::collect_payment::{DebtorAccountIdentifier, DebtorAccountReference};
use routex_client::prelude::*;
use routex_client_common::with_any_service;
use tokio::runtime::Runtime;

#[cxx::bridge(namespace = "yaxi::internal")]
mod ffi {
    extern "Rust" {
        type RoutexClient;

        fn new_routex_client(url: &UniquePtr<CxxString>) -> Result<Box<RoutexClient>>;

        fn system_version(self: &RoutexClient, ticket_id: &CxxString) -> Result<String>;

        fn trace_id(self: &RoutexClient) -> Vec<u8>;

        fn trace(self: &RoutexClient, ticket: &CxxString, trace_id: &CxxVector<u8>)
        -> StringResult;

        fn set_redirect_uri(self: &mut RoutexClient, redirect_uri: &CxxString) -> Result<()>;

        fn set_recurring_consents(self: &mut RoutexClient, enabled: bool);

        fn register_redirect_uri(
            self: &RoutexClient,
            ticket: &CxxString,
            handle: &CxxString,
            redirect_uri: &CxxString,
        ) -> StringResult;

        fn search(
            self: &RoutexClient,
            ticket: &CxxString,
            filters: &CxxVector<SearchFilter>,
            iban_detection: bool,
            limit: &UniquePtr<Usize>,
            details: &CxxVector<Details>,
        ) -> Result<SearchResult>;

        fn info(
            self: &RoutexClient,
            ticket: &CxxString,
            connection_id: &CxxString,
        ) -> Result<InfoResult>;

        #[allow(clippy::too_many_arguments)]
        fn accounts(
            self: &RoutexClient,
            connection_id: &CxxString,
            user_id: &UniquePtr<CxxString>,
            password: &UniquePtr<CxxString>,
            connection_data: &UniquePtr<CxxVector<u8>>,
            ticket: &CxxString,
            fields: &CxxVector<AccountField>,
            filter: &UniquePtr<AccountFilter>,
            session: &UniquePtr<CxxVector<u8>>,
            recurring_consents: TriBool,
        ) -> Result<ServiceResult>;

        fn respond_accounts(
            self: &RoutexClient,
            ticket: &CxxString,
            context: &CxxVector<u8>,
            response: String,
        ) -> ServiceResult;

        fn confirm_accounts(
            self: &RoutexClient,
            ticket: &CxxString,
            context: &CxxVector<u8>,
        ) -> ServiceResult;

        #[allow(clippy::too_many_arguments)]
        fn balances(
            self: &RoutexClient,
            connection_id: &CxxString,
            user_id: &UniquePtr<CxxString>,
            password: &UniquePtr<CxxString>,
            connection_data: &UniquePtr<CxxVector<u8>>,
            ticket: &CxxString,
            accounts: &CxxVector<AccountReference>,
            session: &UniquePtr<CxxVector<u8>>,
            recurring_consents: TriBool,
        ) -> Result<ServiceResult>;

        fn respond_balances(
            self: &RoutexClient,
            ticket: &CxxString,
            context: &CxxVector<u8>,
            response: String,
        ) -> ServiceResult;

        fn confirm_balances(
            self: &RoutexClient,
            ticket: &CxxString,
            context: &CxxVector<u8>,
        ) -> ServiceResult;

        #[allow(clippy::too_many_arguments)]
        fn transactions(
            self: &RoutexClient,
            connection_id: &CxxString,
            user_id: &UniquePtr<CxxString>,
            password: &UniquePtr<CxxString>,
            connection_data: &UniquePtr<CxxVector<u8>>,
            ticket: &CxxString,
            session: &UniquePtr<CxxVector<u8>>,
            recurring_consents: TriBool,
        ) -> Result<ServiceResult>;

        fn respond_transactions(
            self: &RoutexClient,
            ticket: &CxxString,
            context: &CxxVector<u8>,
            response: String,
        ) -> ServiceResult;

        fn confirm_transactions(
            self: &RoutexClient,
            ticket: &CxxString,
            context: &CxxVector<u8>,
        ) -> ServiceResult;

        #[allow(clippy::too_many_arguments)]
        fn collect_payment(
            self: &RoutexClient,
            connection_id: &CxxString,
            user_id: &UniquePtr<CxxString>,
            password: &UniquePtr<CxxString>,
            connection_data: &UniquePtr<CxxVector<u8>>,
            ticket: &CxxString,
            account: &UniquePtr<AccountReference>,
            session: &UniquePtr<CxxVector<u8>>,
            recurring_consents: TriBool,
        ) -> Result<ServiceResult>;

        fn respond_collect_payment(
            self: &RoutexClient,
            ticket: &CxxString,
            context: &CxxVector<u8>,
            response: String,
        ) -> ServiceResult;

        fn confirm_collect_payment(
            self: &RoutexClient,
            ticket: &CxxString,
            context: &CxxVector<u8>,
        ) -> ServiceResult;

        #[allow(clippy::too_many_arguments)]
        fn transfer(
            self: &RoutexClient,
            connection_id: &CxxString,
            user_id: &UniquePtr<CxxString>,
            password: &UniquePtr<CxxString>,
            connection_data: &UniquePtr<CxxVector<u8>>,
            ticket: &CxxString,
            product: PaymentProduct,
            details: &CxxVector<TransferDetails>,
            debtor_account: &UniquePtr<AccountReference>,
            debtor_name: &UniquePtr<CxxString>,
            requested_execution_date: &UniquePtr<I64>,
            session: &UniquePtr<CxxVector<u8>>,
            recurring_consents: TriBool,
        ) -> Result<ServiceResult>;

        fn respond_transfer(
            self: &RoutexClient,
            ticket: &CxxString,
            context: &CxxVector<u8>,
            response: String,
        ) -> ServiceResult;

        fn confirm_transfer(
            self: &RoutexClient,
            ticket: &CxxString,
            context: &CxxVector<u8>,
        ) -> ServiceResult;
    }

    #[derive(Debug)]
    struct StringResult {
        value: String,
        error: UniquePtr<Error>,
    }

    #[derive(Debug)]
    struct SearchResult {
        value: Vec<ConnectionInfo>,
        error: UniquePtr<Error>,
    }

    #[derive(Debug)]
    struct InfoResult {
        value: UniquePtr<ConnectionInfo>,
        error: UniquePtr<Error>,
    }

    #[derive(Debug)]
    struct ServiceResult {
        result: UniquePtr<OBServiceResult>,
        dialog: UniquePtr<Dialog>,
        redirect: UniquePtr<Redirect>,
        redirect_handle: UniquePtr<RedirectHandle>,
        error: UniquePtr<Error>,
    }

    #[derive(Debug)]
    struct OBServiceResult {
        jwt: String,
        session: Vec<u8>,
        connection_data: Vec<u8>,
    }

    #[derive(Debug)]
    struct Dialog {
        context: UniquePtr<DialogContext>,
        message: String,
        image: UniquePtr<Image>,
        confirmation: UniquePtr<Confirmation>,
        selection: UniquePtr<Selection>,
        field: UniquePtr<Field>,
    }

    #[derive(Debug)]
    enum DialogContext {
        Sca,
        Accounts,
        Redirect,
        PaymentStatus,
        VopConfirmation,
        VopCheck,
    }

    #[derive(Debug)]
    struct Image {
        mime_type: String,
        data: Vec<u8>,
        hhd_uc_data: Vec<u8>,
    }

    #[derive(Debug)]
    struct Confirmation {
        context: Vec<u8>,
        polling_delay_secs: UniquePtr<U32>,
    }

    #[derive(Debug)]
    struct Selection {
        options: Vec<DialogOption>,
        context: Vec<u8>,
    }

    #[derive(Debug)]
    struct DialogOption {
        key: String,
        label: String,
        explanation: String,
    }

    #[derive(Debug)]
    struct Field {
        type_: InputType,
        secrecy_level: SecrecyLevel,
        min_length: UniquePtr<U32>,
        max_length: UniquePtr<U32>,
        context: Vec<u8>,
    }

    #[derive(Debug)]
    enum InputType {
        Date,
        Email,
        Number,
        Phone,
        Text,
    }

    #[derive(Debug)]
    enum SecrecyLevel {
        Plain,
        Otp,
        Password,
    }

    #[derive(Debug)]
    struct Redirect {
        url: String,
        context: Vec<u8>,
    }

    #[derive(Debug)]
    struct RedirectHandle {
        handle: String,
        context: Vec<u8>,
    }

    #[derive(Debug)]
    enum ErrorKind {
        RequestError,
        UnexpectedError,
        Canceled,
        InvalidCredentials,
        ServiceBlocked,
        Unauthorized,
        ConsentExpired,
        AccessExceeded,
        PeriodOutOfBounds,
        UnsupportedProduct,
        PaymentFailed,
        UnexpectedValue,
        TicketError,
        ProviderError,
        ResponseError,
        NotFound,
    }

    #[derive(Debug)]
    struct Error {
        kind: ErrorKind,
        string: String,
        code: UniquePtr<U8>,
    }

    #[derive(Debug)]
    enum TriBool {
        None,
        True,
        False,
    }

    #[derive(Debug)]
    struct Usize {
        val: usize,
    }

    #[derive(Debug)]
    struct U8 {
        val: u8,
    }

    #[derive(Debug)]
    struct U32 {
        val: u32,
    }

    #[derive(Debug)]
    struct I64 {
        val: i64,
    }

    #[derive(Debug)]
    struct SearchFilter {
        countries: UniquePtr<CxxVector<CxxString>>,
        name: UniquePtr<CxxString>,
        bic: UniquePtr<CxxString>,
        bank_code: UniquePtr<CxxString>,
        term: UniquePtr<CxxString>,
    }

    #[derive(Debug)]
    enum Details {
        Bics,
    }

    #[derive(Debug)]
    struct ConnectionInfo {
        id: String,
        countries: Vec<String>,
        display_name: String,
        credentials_full: bool,
        credentials_user: bool,
        credentials_none: bool,
        user_id: String,
        password: String,
        advice: String,
        logo_id: String,
        bics_set: bool,
        bics: Vec<String>,
    }

    #[derive(Debug)]
    enum AccountField {
        Iban,
        Number,
        Bic,
        BankCode,
        Currency,
        Name,
        DisplayName,
        OwnerName,
        ProductName,
        Status,
        Type,
    }

    #[derive(Debug)]
    struct AccountFilter {
        eq_field: UniquePtr<AccountField>,
        neq_field: UniquePtr<AccountField>,
        string: UniquePtr<CxxString>,
        status: UniquePtr<AccountStatus>,
        type_: UniquePtr<AccountType>,
        lhs: UniquePtr<AccountFilter>,
        and_rhs: UniquePtr<AccountFilter>,
        or_rhs: UniquePtr<AccountFilter>,
        supports: UniquePtr<SupportedService>,
    }

    #[derive(Debug)]
    enum AccountStatus {
        Available,
        Terminated,
        Blocked,
    }

    #[derive(Debug)]
    enum AccountType {
        Current,
        Card,
        Savings,
        CallMoney,
        TimeDeposit,
        Loan,
        Securities,
        Insurance,
        Commerce,
        Rewards,
    }

    #[derive(Debug)]
    enum SupportedService {
        CollectPayment,
    }

    #[derive(Debug)]
    struct AccountReference {
        iban: String,
        currency: UniquePtr<CxxString>,
    }

    #[derive(Debug)]
    enum PaymentProduct {
        SepaCreditTransfer,
        SepaInstantCreditTransfer,
        DefaultSepaCreditTransfer,
        CrossBorderCreditTransfer,
        DomesticCreditTransfer,
        DomesticInstantCreditTransfer,
    }

    #[derive(Debug)]
    struct TransferDetails {
        end_to_end_identification: UniquePtr<CxxString>,
        amount: UniquePtr<CxxString>,
        currency: UniquePtr<CxxString>,
        creditor_iban: UniquePtr<CxxString>,
        creditor_agent_bic: UniquePtr<CxxString>,
        creditor_name: UniquePtr<CxxString>,
        creditor_town: UniquePtr<CxxString>,
        creditor_country: UniquePtr<CxxString>,
        remittance: UniquePtr<CxxString>,
        charge_bearer: UniquePtr<ChargeBearer>,
    }

    #[derive(Debug)]
    enum ChargeBearer {
        BorneByDebtor,
        BorneByCreditor,
        Shared,
        FollowingServiceLevel,
    }

    #[namespace = "yaxi"]
    unsafe extern "C++" {
        include!("yaxi/routex-client.h");
        type AccountStatus;
        type AccountType;
        type ChargeBearer;
        type Details;
        type DialogContext;
        type InputType;
        type PaymentProduct;
        type SecrecyLevel;
        type SupportedService;
    }
}

impl From<routex_client::Error> for ffi::Error {
    fn from(err: routex_client::Error) -> Self {
        use ffi::ErrorKind;
        use routex_client::Error;

        let (kind, string, code) = match err {
            Error::RequestError(error) => {
                (ErrorKind::RequestError, Some(format!("{error:?}")), None)
            }
            Error::ServiceError(error) => match error {
                routex_api::Error::UnexpectedError { user_message, .. } => {
                    (ErrorKind::UnexpectedError, user_message, None)
                }
                routex_api::Error::Canceled { .. } => (ErrorKind::Canceled, None, None),
                routex_api::Error::InvalidCredentials { user_message, .. } => {
                    (ErrorKind::InvalidCredentials, user_message, None)
                }
                routex_api::Error::ServiceBlocked {
                    user_message, code, ..
                } => (
                    ErrorKind::ServiceBlocked,
                    user_message,
                    code.map(|c| c as u8),
                ),
                routex_api::Error::Unauthorized { user_message, .. } => {
                    (ErrorKind::Unauthorized, user_message, None)
                }
                routex_api::Error::ConsentExpired { user_message, .. } => {
                    (ErrorKind::ConsentExpired, user_message, None)
                }
                routex_api::Error::AccessExceeded { user_message, .. } => {
                    (ErrorKind::AccessExceeded, user_message, None)
                }
                routex_api::Error::PeriodOutOfBounds { user_message, .. } => {
                    (ErrorKind::PeriodOutOfBounds, user_message, None)
                }
                routex_api::Error::UnsupportedProduct {
                    reason,
                    user_message,
                    ..
                } => (
                    ErrorKind::UnsupportedProduct,
                    user_message,
                    reason.map(|c| c as u8),
                ),
                routex_api::Error::PaymentFailed {
                    code, user_message, ..
                } => (
                    ErrorKind::PaymentFailed,
                    user_message,
                    code.map(|c| c as u8),
                ),
                routex_api::Error::UnexpectedValue { error, .. } => {
                    (ErrorKind::UnexpectedValue, Some(error), None)
                }
                routex_api::Error::TicketError { error, code, .. } => {
                    (ErrorKind::TicketError, Some(error), Some(code as u8))
                }
                routex_api::Error::ProviderError {
                    code, user_message, ..
                } => (
                    ErrorKind::ProviderError,
                    user_message,
                    code.map(|c| c as u8),
                ),
                routex_api::Error::InterruptError { .. } => {
                    (ErrorKind::ResponseError, Some(String::new()), None)
                }
            },
            Error::ResponseError(response) => (
                ErrorKind::ResponseError,
                Some(format!("{response:?}")),
                None,
            ),
            Error::NotFound => (ErrorKind::NotFound, None, None),
        };

        ffi::Error {
            kind,
            string: string.unwrap_or_default(),
            code: code.map_or_else(UniquePtr::null, |val| UniquePtr::new(ffi::U8 { val })),
        }
    }
}

impl From<jsonwebtoken::errors::Error> for ffi::Error {
    fn from(err: jsonwebtoken::errors::Error) -> Self {
        Self {
            kind: ffi::ErrorKind::TicketError,
            string: err.to_string(),
            code: UniquePtr::new(ffi::U8 {
                val: TicketErrorCode::Invalid as u8,
            }),
        }
    }
}

impl From<ffi::TriBool> for Option<bool> {
    fn from(value: ffi::TriBool) -> Self {
        match value {
            ffi::TriBool::True => Some(true),
            ffi::TriBool::False => Some(false),
            _ => None,
        }
    }
}

impl From<Result<String, ffi::Error>> for ffi::StringResult {
    fn from(value: Result<String, ffi::Error>) -> Self {
        match value {
            Ok(value) => Self {
                value,
                error: UniquePtr::null(),
            },
            Err(err) => Self {
                value: String::new(),
                error: UniquePtr::new(err),
            },
        }
    }
}

impl Default for ffi::ServiceResult {
    fn default() -> Self {
        Self {
            result: UniquePtr::null(),
            dialog: UniquePtr::null(),
            redirect: UniquePtr::null(),
            redirect_handle: UniquePtr::null(),
            error: UniquePtr::null(),
        }
    }
}

impl<S, E> From<Result<OBResponse<S>, E>> for ffi::ServiceResult
where
    S: routex_api::Service,
    ffi::Error: From<E>,
{
    #[allow(clippy::too_many_lines)]
    fn from(value: Result<OBResponse<S>, E>) -> Self {
        match value {
            Ok(OBResponse::Result(output, session, connection_data)) => Self {
                result: UniquePtr::new(ffi::OBServiceResult {
                    jwt: output.as_str().to_string(),
                    session: session.map_or_else(Vec::new, Into::into),
                    connection_data: connection_data.map_or_else(Vec::new, Into::into),
                }),
                ..Default::default()
            },
            Ok(OBResponse::Dialog(dialog)) => {
                let (confirmation, selection, field) = match dialog.input {
                    DialogInput::Confirmation {
                        context,
                        polling_delay_secs,
                    } => (
                        UniquePtr::new(ffi::Confirmation {
                            context: context.into(),
                            polling_delay_secs: polling_delay_secs
                                .map_or_else(UniquePtr::null, |val| {
                                    UniquePtr::new(ffi::U32 { val })
                                }),
                        }),
                        UniquePtr::null(),
                        UniquePtr::null(),
                    ),
                    DialogInput::Selection { options, context } => (
                        UniquePtr::null(),
                        UniquePtr::new(ffi::Selection {
                            options: options
                                .into_iter()
                                .map(|opt| ffi::DialogOption {
                                    key: opt.key,
                                    label: opt.label,
                                    explanation: opt.explanation.unwrap_or_default(),
                                })
                                .collect(),
                            context: context.into(),
                        }),
                        UniquePtr::null(),
                    ),
                    DialogInput::Field {
                        type_,
                        secrecy_level,
                        min_length,
                        max_length,
                        context,
                    } => (
                        UniquePtr::null(),
                        UniquePtr::null(),
                        UniquePtr::new(ffi::Field {
                            type_: match type_ {
                                InputType::Date => ffi::InputType::Date,
                                InputType::Email => ffi::InputType::Email,
                                InputType::Number => ffi::InputType::Number,
                                InputType::Phone => ffi::InputType::Phone,
                                InputType::Text => ffi::InputType::Text,
                            },
                            secrecy_level: match secrecy_level {
                                SecrecyLevel::Plain => ffi::SecrecyLevel::Plain,
                                SecrecyLevel::Otp => ffi::SecrecyLevel::Otp,
                                SecrecyLevel::Password => ffi::SecrecyLevel::Password,
                            },
                            min_length: min_length.map_or_else(UniquePtr::null, |val| {
                                UniquePtr::new(ffi::U32 { val })
                            }),
                            max_length: max_length.map_or_else(UniquePtr::null, |val| {
                                UniquePtr::new(ffi::U32 { val })
                            }),
                            context: context.into(),
                        }),
                    ),
                };

                Self {
                    dialog: UniquePtr::new(ffi::Dialog {
                        context: dialog.context.map_or_else(UniquePtr::null, |context| {
                            UniquePtr::new(match context {
                                DialogContext::Sca => ffi::DialogContext::Sca,
                                DialogContext::Accounts => ffi::DialogContext::Accounts,
                                DialogContext::Redirect => ffi::DialogContext::Redirect,
                                DialogContext::PaymentStatus => ffi::DialogContext::PaymentStatus,
                                DialogContext::VopConfirmation => {
                                    ffi::DialogContext::VopConfirmation
                                }
                                DialogContext::VopCheck => ffi::DialogContext::VopCheck,
                                _ => unreachable!(),
                            })
                        }),
                        message: dialog.message.unwrap_or_default(),
                        image: dialog.image.map_or_else(UniquePtr::null, |image| {
                            UniquePtr::new(ffi::Image {
                                mime_type: image.mime_type,
                                data: image.data.into(),
                                hhd_uc_data: image.hhd_uc_data.map_or_else(Vec::new, Into::into),
                            })
                        }),
                        confirmation,
                        selection,
                        field,
                    }),
                    ..Default::default()
                }
            }
            Ok(OBResponse::Redirect(redirect)) => Self {
                redirect: UniquePtr::new(ffi::Redirect {
                    url: redirect.url.to_string(),
                    context: redirect.context.into(),
                }),
                ..Default::default()
            },
            Ok(OBResponse::RedirectHandle(redirect)) => Self {
                redirect_handle: UniquePtr::new(ffi::RedirectHandle {
                    handle: redirect.handle,
                    context: redirect.context.into(),
                }),
                ..Default::default()
            },
            Err(err) => err.into(),
        }
    }
}

impl<E> From<E> for ffi::ServiceResult
where
    ffi::Error: From<E>,
{
    fn from(err: E) -> Self {
        Self {
            error: UniquePtr::new(err.into()),
            ..Default::default()
        }
    }
}

impl From<ConnectionInfo> for ffi::ConnectionInfo {
    fn from(info: ConnectionInfo) -> Self {
        Self {
            id: info.id.to_string(),
            countries: info
                .countries
                .into_iter()
                .map(|code| code.alpha2().to_string())
                .collect(),
            display_name: info.display_name,
            credentials_full: info.credentials.full,
            credentials_user: info.credentials.user_id,
            credentials_none: info.credentials.none,
            user_id: info.user_id.unwrap_or_default(),
            password: info.password.unwrap_or_default(),
            advice: info.advice.unwrap_or_default(),
            logo_id: info.logo_id,
            bics_set: info.bics.is_some(),
            bics: info.bics.unwrap_or_default(),
        }
    }
}

macro_rules! account_fields {
    {
        $filter:ident
        $($field:ident $get_val:tt)+
    } => {
        impl TryFrom<ffi::AccountField> for AccountField {
            type Error = anyhow::Error;

            fn try_from(field: ffi::AccountField) -> Result<Self, Self::Error> {
                Ok(match field {
                    $(ffi::AccountField::$field => AccountField::$field,)+
                    _ => bail!("Unexpected AccountField value"),
                })
            }
        }

        paste! {
            impl TryFrom<&ffi::AccountFilter> for Filter<AccountField> {
                type Error = anyhow::Error;

                #[allow(unused_braces)]
                fn try_from($filter: &ffi::AccountFilter) -> Result<Self, Self::Error> {
                    Ok(if let Some(eq_field) = $filter.eq_field.as_ref() {
                        match *eq_field {
                            $(
                                ffi::AccountField::$field => AccountField::[<$field:snake:upper>]
                                    .eq($get_val),
                            )+
                            _ => bail!("Unexpected AccountField value"),
                        }
                    } else if let Some(neq_field) = $filter.neq_field.as_ref() {
                        match *neq_field {
                            $(
                                ffi::AccountField::$field => AccountField::[<$field:snake:upper>]
                                    .not_eq($get_val),
                            )+
                            _ => bail!("Unexpected AccountField value"),
                        }
                    } else if let Some(and_rhs) = $filter.and_rhs.as_ref() {
                        Filter::try_from($filter.lhs.as_ref().ok_or(anyhow!("Invalid filter"))?)?
                            .and(and_rhs.try_into()?)
                    } else if let Some(or_rhs) = $filter.or_rhs.as_ref() {
                        Filter::try_from($filter.lhs.as_ref().ok_or(anyhow!("Invalid filter"))?)?
                            .or(or_rhs.try_into()?)
                    } else {
                        Account::supports(
                            match *$filter.supports.as_ref().ok_or(anyhow!("Invalid filter"))? {
                                ffi::SupportedService::CollectPayment => SupportedService::CollectPayment,
                                _ => bail!("Unexpected SupportedService value"),
                            }
                        )
                    })
                }
            }
        }
    }
}

account_fields! {
    filter
    Iban { filter.string.as_ref().map(ToString::to_string) }
    Number { filter.string.as_ref().map(ToString::to_string) }
    Bic { filter.string.as_ref().map(ToString::to_string) }
    BankCode { filter.string.as_ref().map(ToString::to_string) }
    Currency { filter.string.as_ref().ok_or(anyhow!("Invalid filter value"))?.to_string() }
    Name { filter.string.as_ref().map(ToString::to_string) }
    DisplayName { filter.string.as_ref().map(ToString::to_string) }
    OwnerName { filter.string.as_ref().map(ToString::to_string) }
    ProductName { filter.string.as_ref().map(ToString::to_string) }
    Status { filter.status.as_ref().and_then(|status| Some(match *status {
        ffi::AccountStatus::Available => AccountStatus::Available,
        ffi::AccountStatus::Terminated => AccountStatus::Terminated,
        ffi::AccountStatus::Blocked => AccountStatus::Blocked,
        _ => return None,
    })) }
    Type { filter.type_.as_ref().and_then(|type_| Some(match *type_ {
        ffi::AccountType::Current => AccountType::Current,
        ffi::AccountType::Card => AccountType::Card,
        ffi::AccountType::Savings => AccountType::Savings,
        ffi::AccountType::CallMoney => AccountType::CallMoney,
        ffi::AccountType::TimeDeposit => AccountType::TimeDeposit,
        ffi::AccountType::Loan => AccountType::Loan,
        ffi::AccountType::Securities => AccountType::Securities,
        ffi::AccountType::Insurance => AccountType::Insurance,
        ffi::AccountType::Commerce => AccountType::Commerce,
        ffi::AccountType::Rewards => AccountType::Rewards,
        _ => return None,
    })) }
}

impl From<&ffi::AccountReference> for AccountReference {
    fn from(account: &ffi::AccountReference) -> Self {
        AccountReference {
            id: AccountIdentifier::Iban(account.iban.clone()),
            currency: account.currency.as_ref().map(CxxString::to_string),
        }
    }
}

struct RoutexClient {
    inner: routex_client::RoutexClient<reqwest::Client>,
    runtime: Runtime,
}

fn new_routex_client(url: &UniquePtr<CxxString>) -> anyhow::Result<Box<RoutexClient>> {
    Ok(Box::new(RoutexClient {
        inner: routex_client::RoutexClient::for_distribution(
            "C++",
            env!("CARGO_PKG_VERSION"),
            url.as_ref()
                .map_or("https://api.yaxi.tech/".parse(), |url| {
                    url.to_string().parse()
                })?,
            reqwest::Client::new(),
        ),
        runtime: Runtime::new()?,
    }))
}

impl RoutexClient {
    fn system_version(&self, ticket_id: &CxxString) -> anyhow::Result<String> {
        Ok(self
            .runtime
            .block_on(self.inner.system_version(ticket_id.to_string().parse()?))
            .unwrap_or_default())
    }

    fn trace_id(&self) -> Vec<u8> {
        self.inner.trace_id().unwrap_or_default()
    }

    fn trace(&self, ticket: &CxxString, trace_id: &CxxVector<u8>) -> ffi::StringResult {
        let ticket = ticket.to_string();

        with_any_service!(ticket, authenticated, {
            self.runtime
                .block_on(self.inner.trace(&authenticated, trace_id.as_slice()))
        })
        .into()
    }

    fn set_redirect_uri(&mut self, redirect_uri: &CxxString) -> Result<(), InvalidHeaderValue> {
        self.inner.set_redirect_uri(&redirect_uri.to_string())
    }

    fn set_recurring_consents(&mut self, enabled: bool) {
        self.inner.set_recurring_consents(enabled);
    }

    fn register_redirect_uri(
        &self,
        ticket: &CxxString,
        handle: &CxxString,
        redirect_uri: &CxxString,
    ) -> ffi::StringResult {
        let ticket = ticket.to_string();

        with_any_service!(ticket, authenticated, {
            self.runtime.block_on(self.inner.register_redirect_uri(
                &authenticated,
                handle.to_string(),
                redirect_uri.to_string(),
            ))
        })
        .map(|url| url.to_string())
        .into()
    }

    fn search(
        &self,
        ticket: &CxxString,
        filters: &CxxVector<ffi::SearchFilter>,
        iban_detection: bool,
        limit: &UniquePtr<ffi::Usize>,
        details: &CxxVector<ffi::Details>,
    ) -> anyhow::Result<ffi::SearchResult> {
        let ticket = ticket.to_string();

        let filters = filters
            .into_iter()
            .map(|f| {
                Ok(if let Some(countries) = f.countries.as_ref() {
                    SearchFilter::Countries(
                        countries
                            .iter()
                            .flat_map(|c| CountryCode::for_alpha2(&c.to_string()))
                            .collect(),
                    )
                } else if let Some(name) = f.name.as_ref() {
                    SearchFilter::Name(name.to_string())
                } else if let Some(bic) = f.bic.as_ref() {
                    SearchFilter::Bic(bic.to_string())
                } else if let Some(bank_code) = f.bank_code.as_ref() {
                    SearchFilter::BankCode(bank_code.to_string())
                } else {
                    SearchFilter::Term(
                        f.term
                            .as_ref()
                            .ok_or(anyhow!("Invalid filter"))?
                            .to_string(),
                    )
                })
            })
            .collect::<anyhow::Result<Vec<_>>>()?;

        Ok(
            match with_any_service!(ticket, authenticated, {
                self.runtime.block_on(
                    self.inner
                        .search(authenticated, filters)
                        .iban_detection(iban_detection)
                        .limit(limit.as_ref().map(|limit| limit.val))
                        .details(
                            details
                                .into_iter()
                                .map(|detail| {
                                    Ok(match *detail {
                                        ffi::Details::Bics => Details::Bics,
                                        _ => bail!("Unexpected Details value"),
                                    })
                                })
                                .collect::<Result<Vec<_>, _>>()?,
                        )
                        .send(),
                )
            }) as Result<Vec<_>, ffi::Error>
            {
                Ok(connections) => ffi::SearchResult {
                    value: connections.into_iter().map(Into::into).collect(),
                    error: UniquePtr::null(),
                },
                Err(err) => ffi::SearchResult {
                    value: Vec::new(),
                    error: UniquePtr::new(err),
                },
            },
        )
    }

    fn info(
        &self,
        ticket: &CxxString,
        connection_id: &CxxString,
    ) -> Result<ffi::InfoResult, uuid::Error> {
        let ticket = ticket.to_string();

        match with_any_service!(ticket, authenticated, {
            self.runtime.block_on(
                self.inner
                    .info(&authenticated, connection_id.to_string().parse()?),
            )
        }) as Result<ConnectionInfo, ffi::Error>
        {
            Ok(info) => Ok(ffi::InfoResult {
                value: UniquePtr::new(info.into()),
                error: UniquePtr::null(),
            }),
            Err(err) => Ok(ffi::InfoResult {
                value: UniquePtr::null(),
                error: UniquePtr::new(err),
            }),
        }
    }
}

macro_rules! service {
    {
        $service:ident
        $(($($arg:ident: $type:ty),*$(,)?))?
        $({$($values:tt)*})?
    } => {
        paste! {
            impl RoutexClient {
                #[allow(clippy::too_many_arguments)]
                fn $service(
                    &self,
                    connection_id: &CxxString,
                    user_id: &UniquePtr<CxxString>,
                    password: &UniquePtr<CxxString>,
                    connection_data: &UniquePtr<CxxVector<u8>>,
                    ticket: &CxxString,
                    $($($arg: $type,)*)?
                    session: &UniquePtr<CxxVector<u8>>,
                    recurring_consents: ffi::TriBool,
                ) -> anyhow::Result<ffi::ServiceResult> {
                    let mut request = self.inner.$service(
                        Credentials {
                            connection_id: connection_id.to_string().parse()?,
                            user_id: user_id.as_ref().map(CxxString::to_string),
                            password: password.as_ref().map(CxxString::to_string),
                            connection_data: connection_data.as_ref().map(|v| v.as_slice().to_vec().into()),
                        },
                        &match ticket.to_string().parse() {
                            Ok(ticket) => ticket,
                            Err(err) => return Ok(err.into()),
                        },
                        $($($values)*)?
                    );

                    if let Some(recurring_consents) = recurring_consents.into() {
                        request = request.recurring_consents(recurring_consents);
                    }

                    if let Some(session) = session.as_ref() {
                        request = request.session(session.as_slice().to_vec().into());
                    }

                    Ok(self.runtime.block_on(request.send()).into())
                }

                fn [<respond_$service>](
                    &self,
                    ticket: &CxxString,
                    context: &CxxVector<u8>,
                    response: String,
                ) -> ffi::ServiceResult {
                    self.runtime.block_on(
                            self.inner.[<respond_$service>](
                                &match ticket.to_string().parse() {
                                    Ok(ticket) => ticket,
                                    Err(err) => return err.into(),
                                },
                                context.as_slice().to_vec().into(),
                                response,
                            ),
                        ).into()
                }

                fn [<confirm_$service>](
                    &self,
                    ticket: &CxxString,
                    context: &CxxVector<u8>,
                ) -> ffi::ServiceResult {
                    self.runtime.block_on(
                            self.inner.[<confirm_$service>](
                                &match ticket.to_string().parse() {
                                    Ok(ticket) => ticket,
                                    Err(err) => return err.into(),
                                },
                                context.as_slice().to_vec().into(),
                            ),
                        ).into()
                }
            }
        }
    }
}

service! {
    accounts
    (fields: &CxxVector<ffi::AccountField>, filter: &UniquePtr<ffi::AccountFilter>)
    {
        fields.into_iter().map(|f| (*f).try_into()).collect::<anyhow::Result<Vec<_>>>()?,
        filter.as_ref().map(TryInto::try_into).transpose()?,
    }
}

service! {
    balances
    (accounts: &CxxVector<ffi::AccountReference>)
    {
        accounts.into_iter().map(Into::into),
    }
}

service! {
    transactions
}

service! {
    collect_payment
    (account: &UniquePtr<ffi::AccountReference>)
    {
        account.as_ref().map(|account| DebtorAccountReference {
            id: DebtorAccountIdentifier::Iban(account.iban.clone()),
            currency: account.currency.as_ref().map(CxxString::to_string),
        }),
    }
}

service! {
    transfer
    (
        product: ffi::PaymentProduct,
        details: &CxxVector<ffi::TransferDetails>,
        debtor_account: &UniquePtr<ffi::AccountReference>,
        debtor_name: &UniquePtr<CxxString>,
        requested_execution_date: &UniquePtr<ffi::I64>,
    )
    {
        match product {
            ffi::PaymentProduct::SepaCreditTransfer => PaymentProduct::SepaCreditTransfer,
            ffi::PaymentProduct::SepaInstantCreditTransfer => PaymentProduct::SepaInstantCreditTransfer,
            ffi::PaymentProduct::DefaultSepaCreditTransfer => PaymentProduct::DefaultSepaCreditTransfer,
            ffi::PaymentProduct::CrossBorderCreditTransfer => PaymentProduct::CrossBorderCreditTransfer,
            ffi::PaymentProduct::DomesticCreditTransfer => PaymentProduct::DomesticCreditTransfer,
            ffi::PaymentProduct::DomesticInstantCreditTransfer => PaymentProduct::DomesticInstantCreditTransfer,
            _ => bail!("Unexpected PaymentProduct value"),
        },
        debtor_account.as_ref().map(Into::into),
        debtor_name.as_ref().map(CxxString::to_string),
        requested_execution_date.as_ref().map(|secs| Ok::<_, anyhow::Error>(ISODateTimeOrDate::OffsetDateTime(
            Utc
                .timestamp_opt(secs.val, 0)
                .earliest()
                .ok_or_else(|| anyhow!("Failed to read requested_execution_date"))?
                .fixed_offset()
        ))).transpose()?,
        details.into_iter().map(|ffi::TransferDetails {
            end_to_end_identification,
            amount,
            currency,
            creditor_iban,
            creditor_agent_bic,
            creditor_name,
            creditor_town,
            creditor_country,
            remittance,
            charge_bearer,
        }| {
            let mut details = TransferDetails::new(
                Amount::new(amount.to_string().parse::<Decimal>()?, currency.to_string()),
                AccountIdentifier::Iban(creditor_iban.to_string()),
                creditor_name.to_string(),
            );
            details.end_to_end_identification = end_to_end_identification.as_ref().map(CxxString::to_string);
            details.creditor_agent_bic = creditor_agent_bic.as_ref().map(CxxString::to_string);
            details.creditor_name = creditor_name.to_string();
            details.creditor_address = if let Some(town_name) = creditor_town.as_ref() && let Some(Ok(country)) = creditor_country.as_ref().map(|c| CountryCode::for_alpha2(&c.to_string())) {
                Some(CreditorAddress::new(town_name.to_string(), country))
            } else {
                None
            };
            details.remittance = remittance.as_ref().map(CxxString::to_string);
            details.charge_bearer = charge_bearer.as_ref().map(|bearer| Ok(match *bearer {
                ffi::ChargeBearer::BorneByDebtor => ChargeBearer::BorneByDebtor,
                ffi::ChargeBearer::BorneByCreditor => ChargeBearer::BorneByCreditor,
                ffi::ChargeBearer::Shared => ChargeBearer::Shared,
                ffi::ChargeBearer::FollowingServiceLevel => ChargeBearer::FollowingServiceLevel,
                _ => bail!("Unexpected ChargeBearer value"),
            })).transpose()?;
            Ok::<_, anyhow::Error>(details)
        }).collect::<Result<Vec<_>, _>>()?,
    }
}
