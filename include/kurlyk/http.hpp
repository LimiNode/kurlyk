#pragma once
#ifndef KURLYK_HEADER_KURLYK_HTTP_HPP_INCLUDED
#define KURLYK_HEADER_KURLYK_HTTP_HPP_INCLUDED

/// \file http.hpp
/// \brief Aggregates main HTTP interfaces and utilities, including client, request manager, and helpers.

#ifndef KURLYK_AUTH_SUPPORT
#   define KURLYK_AUTH_SUPPORT 1
#endif

#ifndef KURLYK_OAUTH_SUPPORT
#   define KURLYK_OAUTH_SUPPORT KURLYK_AUTH_SUPPORT
#endif

// Core utilities
#include "core.hpp"

// HTTP components
#include "http/data.hpp"
#include "http/HttpRequestManager.hpp"
#include "http/HttpClient.hpp"
#include "http/utils.hpp"
#include "http/ProxyChecker.hpp"

#if KURLYK_AUTH_SUPPORT
#include "http/auth.hpp"
#endif

#endif // KURLYK_HEADER_KURLYK_HTTP_HPP_INCLUDED
