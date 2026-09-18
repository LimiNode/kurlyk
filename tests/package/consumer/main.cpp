#include <kurlyk.hpp>

int main() {
    kurlyk::ProxyConfig proxy("localhost:8080", "");
    return proxy.is_valid() ? 0 : 1;
}
