#define KURLYK_AUTO_INIT 0
#define KURLYK_HTTP_SUPPORT 0
#define KURLYK_WEBSOCKET_SUPPORT 0

#include <kurlyk.hpp>

#if defined(ASIO_STANDALONE)
#   error "HTTP-only Kurlyk headers must not configure an Asio backend"
#endif

#if KURLYK_USE_BOOST_ASIO
#   error "HTTP-only smoke unexpectedly selected Boost.Asio"
#endif

int main() {
    return 0;
}
