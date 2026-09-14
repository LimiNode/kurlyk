#include <kurlyk/types/ProxyConfig.hpp>

int main() {
    kurlyk::ProxyConfig config;
    config.set_proxy("127.0.0.1", 8080, kurlyk::ProxyType::PROXY_HTTP);
    config.set_proxy_auth("user", "password");
    return config.is_valid() &&
           config.get_ip() == "127.0.0.1" &&
           config.get_port() == 8080 &&
           config.get_username() == "user" &&
           config.get_password() == "password" ? 0 : 1;
}
