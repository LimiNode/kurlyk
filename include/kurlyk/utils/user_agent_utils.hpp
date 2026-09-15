#pragma once
#ifndef KURLYK_HEADER_KURLYK_UTILS_USER_AGENT_UTILS_HPP_INCLUDED
#define KURLYK_HEADER_KURLYK_UTILS_USER_AGENT_UTILS_HPP_INCLUDED

/// \file user_agent_utils.hpp
/// \brief Provides functions for converting User-Agent strings to sec-ch-ua format.

namespace kurlyk {
namespace utils {

    /// \brief Converts a User-Agent string to a sec-ch-ua header value.
    /// \param user_agent The User-Agent string.
    /// \return The generated sec-ch-ua header value.
    inline std::string convert_user_agent_to_sec_ch_ua(const std::string& user_agent) {
        // Regular expression to extract the browser name and version from User-Agent
        static const std::regex browser_regex(R"(Chrome/(\d+)\.\d+\.\d+\.\d+)");
        std::string version = "0";

        // Match the User-Agent string to extract the Chrome version
        std::smatch match;
        if (std::regex_search(user_agent, match, browser_regex) && match.size() > 1) {
            version = match[1].str(); // Extract version number
        }

        // Generate sec-ch-ua header
        std::string sec_ch_ua = "\"Not;A Brand\";v=\"99\", \"Google Chrome\";v=\"" + version + "\", \"Chromium\";v=\"" + version + "\"";
        return sec_ch_ua;
    }

} // namespace utils
} // namespace kurlyk

#endif // KURLYK_HEADER_KURLYK_UTILS_USER_AGENT_UTILS_HPP_INCLUDED
