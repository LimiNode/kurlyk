#pragma once
#ifndef KURLYK_HEADER_KURLYK_TYPES_SUBMIT_RESULT_HPP_INCLUDED
#define KURLYK_HEADER_KURLYK_TYPES_SUBMIT_RESULT_HPP_INCLUDED

/// \file SubmitResult.hpp
/// \brief Defines the SubmitResult structure for synchronous admission results.

#include <system_error>

namespace kurlyk {

    /// \struct SubmitResult
    /// \brief Represents the synchronous result of trying to enqueue or submit work.
    struct SubmitResult {
        bool            accepted = false;   ///< Indicates whether the work item was accepted for processing.
        std::error_code error_code;         ///< Describes the rejection reason when `accepted` is false.

        /// \brief Constructs a rejected result without an error code.
        SubmitResult() {}

        /// \brief Constructs an admission result.
        /// \param is_accepted Indicates whether the work item was accepted.
        /// \param error Describes the rejection reason.
        SubmitResult(bool is_accepted, const std::error_code& error)
            : accepted(is_accepted), error_code(error) {}

        /// \brief Converts the result to a boolean accepted flag.
        explicit operator bool() const {
            return accepted;
        }
    };

} // namespace kurlyk

#endif // KURLYK_HEADER_KURLYK_TYPES_SUBMIT_RESULT_HPP_INCLUDED
