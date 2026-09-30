#if defined(_WIN32) && !defined(WIN32_LEAN_AND_MEAN)
#   define WIN32_LEAN_AND_MEAN
#endif

#define KURLYK_AUTO_INIT 0
#define KURLYK_HTTP_SUPPORT 0
#define KURLYK_WEBSOCKET_SUPPORT 1

#include <kurlyk.hpp>

#if defined(KURLYK_EXPECT_STANDALONE)
#   if KURLYK_USE_BOOST_ASIO
#       error "Standalone smoke selected Boost.Asio"
#   endif
#   ifndef ASIO_STANDALONE
#       error "Standalone smoke did not select ASIO_STANDALONE"
#   endif
#else
#   if !KURLYK_USE_BOOST_ASIO
#       error "Boost.Asio smoke did not select Boost.Asio"
#   endif
#   if defined(ASIO_STANDALONE)
#       error "Boost.Asio smoke selected ASIO_STANDALONE"
#   endif
#endif

int main() {
    kurlyk::WebSocketClient client("ws://localhost:1");
    client.set_max_send_queue_size(1);
    (void)client.send_message("compile-only");
    return 0;
}
