#include <yaxi/routex-client.h>

#include <exception>
#include <jni.h>
#include <string>
#include <variant>

namespace {

template <class... Ts> struct overloaded : Ts... {
    using Ts::operator()...;
};
template <class... Ts> overloaded(Ts...) -> overloaded<Ts...>;

std::string describe(const yaxi::Error &error) {
    return std::visit(overloaded{
                          [](const yaxi::RequestError &e) { return "RequestError: " + e.error; },
                          [](const auto &e) { return std::string(e.what()); },
                      },
                      error);
}

} // namespace

// Runs one search through the published client and reports how it ended. The
// ticket is deliberately invalid: reaching a verdict about it at all means TLS
// completed, which is what the caller is asking about.
extern "C" JNIEXPORT jstring JNICALL
Java_tech_yaxi_routex_cxx_consumertest_Probe_search(JNIEnv *env, jclass) {
    std::string outcome;
    try {
        const yaxi::RoutexClient client;
        auto result = client.search("not-a-ticket", {yaxi::TermSearchFilter{"yaxi"}}, false, 1);
        if (const auto *error = std::get_if<yaxi::Error>(&result)) {
            outcome = describe(*error);
        } else {
            outcome = "ok";
        }
    } catch (const std::exception &e) {
        outcome = std::string("exception: ") + e.what();
    } catch (...) {
        outcome = "exception: unknown";
    }
    return env->NewStringUTF(outcome.c_str());
}
