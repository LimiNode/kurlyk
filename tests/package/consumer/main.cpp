#include <kurlyk.hpp>
#include <string>

int main() {
    kurlyk::ProxyConfig proxy("localhost:8080", "");
    kurlyk::ProxyCheckOptions options;
    kurlyk::ProxyCheckResult result;
    kurlyk::ProxyChecker checker;
    const std::string challenge =
        kurlyk::utils::make_s256_code_challenge("test");
    (void)options;
    (void)result;
    (void)checker;
    return proxy.is_valid() && !challenge.empty() ? 0 : 1;
}
