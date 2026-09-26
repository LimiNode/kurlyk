#pragma once
#ifndef KURLYK_HEADER_KURLYK_CONFIG_HPP_INCLUDED
#define KURLYK_HEADER_KURLYK_CONFIG_HPP_INCLUDED

/// \file config.hpp
/// \brief Defines public compile-time configuration for the native Asio backend.

/// \def KURLYK_USE_BOOST_ASIO
/// \brief Selects Boost.Asio instead of standalone Asio for WebSocket support.
///
/// Leave undefined (or set to 0) to use standalone Asio. Define to 1 before
/// including a Kurlyk header when the application is configured for Boost.Asio.
#ifndef KURLYK_USE_BOOST_ASIO
#   define KURLYK_USE_BOOST_ASIO 0
#endif

#if KURLYK_USE_BOOST_ASIO
#   if defined(ASIO_STANDALONE)
#       error "KURLYK_USE_BOOST_ASIO conflicts with ASIO_STANDALONE"
#   endif
#else
#   ifndef ASIO_STANDALONE
#       define ASIO_STANDALONE
#   endif
#endif

#endif // KURLYK_HEADER_KURLYK_CONFIG_HPP_INCLUDED
