#pragma once
#ifndef KURLYK_HEADER_KURLYK_UTILS_PATH_UTILS_HPP_INCLUDED
#define KURLYK_HEADER_KURLYK_UTILS_PATH_UTILS_HPP_INCLUDED

/// \file path_utils.hpp
/// \brief Provides platform-specific utilities for obtaining paths and file locations.

#include <cstdint>
#include <cstdlib>
#include <stdexcept>
#include <string>
#include <vector>

#if defined(_WIN32)
#include <windows.h>
#include <locale>
#include <codecvt>
#elif defined(__APPLE__)
#include <limits.h>
#include <mach-o/dyld.h>
#include <unistd.h>
#else
#include <limits.h>
#include <unistd.h>
#endif

#if __cplusplus >= 201703L
#include <filesystem>
#endif

namespace kurlyk {
namespace utils {

    /// \brief Retrieves the directory of the executable file.
    /// \return A string containing the directory path of the executable.
    inline std::string get_exec_dir() {
#       if defined(_WIN32)
        std::vector<wchar_t> buffer(MAX_PATH);
        HMODULE hModule = GetModuleHandle(NULL);

        // Пробуем получить путь
        DWORD size = GetModuleFileNameW(hModule, buffer.data(), buffer.size());

        // Если путь слишком длинный, увеличиваем буфер
        while (size == buffer.size() && GetLastError() == ERROR_INSUFFICIENT_BUFFER) {
            buffer.resize(buffer.size() * 2);  // Увеличиваем буфер в два раза
            size = GetModuleFileNameW(hModule, buffer.data(), buffer.size());
        }

        if (size == 0) throw std::runtime_error("Failed to get executable path.");
        std::wstring exe_path(buffer.begin(), buffer.begin() + size);
		std::wstring::size_type pos = exe_path.find_last_of(L"\\/");
        if (pos != std::wstring::npos) {
            exe_path = exe_path.substr(0, pos);
        }

#   	if __cplusplus >= 201703L
        auto utf8_path = std::filesystem::path(exe_path).u8string();
#       if defined(__cpp_char8_t)
        return std::string(reinterpret_cast<const char*>(utf8_path.data()), utf8_path.size());
#       else
        return utf8_path;
#       endif
#   	else
		std::wstring_convert<std::codecvt_utf8_utf16<wchar_t>> converter;
		return converter.to_bytes(exe_path);
#   	endif

#       elif defined(__APPLE__)
        uint32_t path_size = 0;
        if (_NSGetExecutablePath(NULL, &path_size) != -1 || path_size == 0) {
            throw std::runtime_error("Failed to determine executable path size.");
        }

        std::vector<char> buffer(path_size + 1, '\0');
        if (_NSGetExecutablePath(buffer.data(), &path_size) != 0) {
            throw std::runtime_error("Failed to get executable path.");
        }

        std::string exe_path(buffer.data());
        std::vector<char> resolved_path(PATH_MAX, '\0');
        if (realpath(exe_path.c_str(), resolved_path.data()) != NULL) {
            exe_path.assign(resolved_path.data());
        }

        const size_t pos = exe_path.find_last_of("/");
        if (pos != std::string::npos) {
            exe_path.erase(pos);
        }
        return exe_path;

#       else
        char result[PATH_MAX];
        ssize_t count = readlink("/proc/self/exe", result, PATH_MAX);

        if (count == -1) throw std::runtime_error("Failed to get executable path.");

        std::string exe_path(result, count);

        // Обрезаем путь до директории (удаляем имя файла, оставляем только путь к папке)
        size_t pos = exe_path.find_last_of("\\/");
        if (pos != std::string::npos) {
            exe_path = exe_path.substr(0, pos);
        }

        return exe_path;
#       endif
    }

} // namespace utils
} // namespace kurlyk

#endif // KURLYK_HEADER_KURLYK_UTILS_PATH_UTILS_HPP_INCLUDED
