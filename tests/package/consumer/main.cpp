#include <kurlyk.hpp>
#include <string>

int main() {
    kurlyk::ProxyConfig proxy("localhost:8080", "");
    const std::string challenge =
        kurlyk::utils::make_s256_code_challenge("test");
    return proxy.is_valid() && !challenge.empty() ? 0 : 1;
}
