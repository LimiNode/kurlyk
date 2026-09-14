#pragma once
#ifndef KURLYK_HEADER_KURLYK_TYPES_HPP_INCLUDED
#define KURLYK_HEADER_KURLYK_TYPES_HPP_INCLUDED

/// \file types.hpp
/// \brief Aggregates type enumerations and utilities used throughout the Kurlyk library.

// Standard library
#include <string>
#include <unordered_map>
#include <stdexcept>
#include <vector>
#include <cstdint>

// Optional third-party integration
#if KURLYK_JSON_SUPPORT
#include <nlohmann/json.hpp>
#endif

#if KURLYK_HTTP_SUPPORT
#include <curl/curl.h>
#endif

#include "utils/string_utils.hpp"

// Enumerations and conversion utilities
#include "types/enums.hpp"
#include "types/SubmitResult.hpp"
#include "types/type_utils.hpp"
#include "types/Cookie.hpp"
#include "types/ProxyConfig.hpp"

#endif // KURLYK_HEADER_KURLYK_TYPES_HPP_INCLUDED
