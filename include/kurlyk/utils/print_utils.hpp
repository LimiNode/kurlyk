#pragma once
#ifndef KURLYK_HEADER_KURLYK_UTILS_PRINT_UTILS_HPP_INCLUDED
#define KURLYK_HEADER_KURLYK_UTILS_PRINT_UTILS_HPP_INCLUDED

/// \file print_utils.hpp
/// \brief Provides thread-safe console output utilities.

#define KURLYK_PRINT kurlyk::utils::ThreadSafePrintStream{}

namespace kurlyk {
namespace utils {

    /// \brief Thread-safe stream class for printing to console from multiple threads.
    class ThreadSafePrintStream : public std::ostringstream {
    public:
        ThreadSafePrintStream() = default;

        ~ThreadSafePrintStream() {
            get_instance().print(this->str());
        }

    private:
        class PrintStream {
        public:
            void print(const std::string &str) {
                std::lock_guard<std::mutex> guard(m_mutex_print);
                std::cout << str;
            }

        private:
            std::mutex m_mutex_print;
        };

        static PrintStream& get_instance() {
            static PrintStream instance;
            return instance;
        }
    };

} // namespace utils
} // namespace kurlyk

#endif // KURLYK_HEADER_KURLYK_UTILS_PRINT_UTILS_HPP_INCLUDED
