#pragma once
#ifndef KURLYK_HEADER_KURLYK_UTILS_HPP_INCLUDED
#define KURLYK_HEADER_KURLYK_UTILS_HPP_INCLUDED

/// \file utils.hpp
/// \brief Contains utility functions and classes for handling HTTP requests and responses.

#include <iostream>
#include <string>
#include <unordered_map>
#include <vector>
#include <sstream>
#include <algorithm>
#include <regex>
#include <cctype>
#include <system_error>

#ifdef KURLYK_USE_CURL
#include <curl/curl.h>
#include "utils/CurlErrorCategory.hpp"
#endif

#if KURLYK_WEBSOCKET_SUPPORT
#include "utils/WebSocketErrorCategory.hpp"
#endif

#include "utils/ClientErrorCategory.hpp"
#include "utils/HttpErrorCategory.hpp"

#include "utils/EventQueue.hpp"
#include "utils/CaseInsensitiveMultimap.hpp"

#ifdef _WIN32
// For Windows systems
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <direct.h>
#include <windows.h>
#include <locale>
#include <codecvt>
#else
// For POSIX systems
#include <unistd.h>
#include <limits.h>
#include <dirent.h>
#include <sys/stat.h>
#include <errno.h>
#endif

#include "utils/percent_encoding.hpp"
#include "utils/http_parser.hpp"
#include "utils/http_utils.hpp"
#include "utils/url_utils.hpp"
#include "utils/email_utils.hpp"
#include "utils/user_agent_utils.hpp"
#include "utils/print_utils.hpp"
#include "utils/path_utils.hpp"
#include "utils/encoding_utils.hpp"
#include "utils/string_utils.hpp"

#endif // KURLYK_HEADER_KURLYK_UTILS_HPP_INCLUDED
