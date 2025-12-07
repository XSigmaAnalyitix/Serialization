/*
 * This code is inspired by PyTorch's parallel implementation.
 */

/**
 * @file thread_name.cpp
 * @brief Implementation of thread naming utilities
 *
 * This file implements cross-platform thread naming functionality using
 * native platform APIs. The implementation is self-contained and has no
 * dependencies on the logging module.
 *
 * IMPLEMENTATION STRATEGY:
 * =======================
 * - Windows: Uses SetThreadDescription/GetThreadDescription (Windows 10 1607+)
 *   with runtime linking to support older Windows versions gracefully.
 * - Unix: Uses pthread_setname_np/pthread_getname_np with 16-char limit.
 *
 * All platforms maintain a thread-local string copy of the name for reliable
 * retrieval, even when OS-level naming fails or is unsupported.
 *
 * THREAD NAME LENGTH LIMITS:
 * ==========================
 * - Unix: 16 characters (including null terminator) = 15 usable chars
 * - Windows: No practical limit (uses wide strings internally)
 *
 * CODING STANDARDS:
 * =================
 * - Follows Graphfic C++ coding standards
 * - snake_case naming convention
 * - No exceptions (error handling via return values)
 * - Cross-platform compatible (Windows and Unix)
 */

#include "parallel/thread_name.h"

#include <algorithm>
#include <cstring>

#include "common/macros.h"

// Platform-specific includes
#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
// Unix (Linux, macOS, BSD, etc.)
#include <pthread.h>
#endif

namespace graphfic::detail::parallel
{

// Thread-local storage for thread name (fallback and reliable storage)
// Max size accommodates all platforms (Windows has no limit, Unix 16)
constexpr size_t kMaxThreadNameLength = 128;

// Thread-local buffer to store the thread name
// Using a fixed-size buffer to avoid dynamic allocation in thread-local storage
static thread_local char tls_thread_name[kMaxThreadNameLength] = {0};

namespace
{

// Helper function to safely copy string to fixed buffer with truncation
void safe_string_copy(char* dest, size_t dest_size, const char* src)
{
    if (dest_size == 0 || src == nullptr)
    {
        return;
    }
    const size_t copy_len = std::min(std::strlen(src), dest_size - 1);
    std::memcpy(dest, src, copy_len);
    dest[copy_len] = '\0';
}

#if defined(_WIN32)
// Windows implementation using SetThreadDescription/GetThreadDescription
// These APIs were introduced in Windows 10 1607 (Anniversary Update)
// We use runtime linking to gracefully handle older Windows versions

using SetThreadDescriptionFunc = HRESULT(WINAPI*)(HANDLE, PCWSTR);
using GetThreadDescriptionFunc = HRESULT(WINAPI*)(HANDLE, PWSTR*);

// Lazy-initialized function pointers
static SetThreadDescriptionFunc get_set_func()
{
    static const auto func = []() -> SetThreadDescriptionFunc
    {
        const HMODULE kernel32 = GetModuleHandleW(L"kernel32.dll");
        return kernel32 ? reinterpret_cast<SetThreadDescriptionFunc>(
                              GetProcAddress(kernel32, "SetThreadDescription"))
                        : nullptr;
    }();
    return func;
}

static GetThreadDescriptionFunc get_get_func()
{
    static const auto func = []() -> GetThreadDescriptionFunc
    {
        const HMODULE kernel32 = GetModuleHandleW(L"kernel32.dll");
        return kernel32 ? reinterpret_cast<GetThreadDescriptionFunc>(
                              GetProcAddress(kernel32, "GetThreadDescription"))
                        : nullptr;
    }();
    return func;
}

// Convert UTF-8 string to wide string for Windows API
static std::wstring utf8_to_wide(const std::string& utf8)
{
    if (utf8.empty())
    {
        return {};
    }
    const int size =
        MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), static_cast<int>(utf8.size()), nullptr, 0);
    if (size <= 0)
    {
        return {};
    }
    std::wstring wide(static_cast<size_t>(size), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), static_cast<int>(utf8.size()), &wide[0], size);
    return wide;
}

// Convert wide string to UTF-8 string
static std::string wide_to_utf8(const std::wstring& wide)
{
    if (wide.empty())
    {
        return {};
    }
    const int size = WideCharToMultiByte(
        CP_UTF8, 0, wide.c_str(), static_cast<int>(wide.size()), nullptr, 0, nullptr, nullptr);
    if (size <= 0)
    {
        return {};
    }
    std::string utf8(static_cast<size_t>(size), '\0');
    WideCharToMultiByte(
        CP_UTF8, 0, wide.c_str(), static_cast<int>(wide.size()), &utf8[0], size, nullptr, nullptr);
    return utf8;
}

void platform_set_thread_name(const std::string& name)
{
    const auto set_func = get_set_func();
    if (set_func != nullptr)
    {
        const std::wstring wide_name = utf8_to_wide(name);
        (void)set_func(GetCurrentThread(), wide_name.c_str());
    }
}

std::string platform_get_thread_name()
{
    const auto get_func = get_get_func();
    if (get_func != nullptr)
    {
        PWSTR wide_name = nullptr;
        if (SUCCEEDED(get_func(GetCurrentThread(), &wide_name)) && wide_name != nullptr)
        {
            const std::wstring wide_str(wide_name);
            LocalFree(wide_name);
            return wide_to_utf8(wide_str);
        }
    }
    return {};
}

#else
// Unix implementation (Linux, macOS, BSD, etc.)
// Uses pthread_setname_np/pthread_getname_np which is available on most Unix systems
// Thread name limit is 16 characters (including null terminator) on most systems
constexpr size_t kUnixMaxNameLength = 16;

void platform_set_thread_name(const std::string& name)
{
    const std::string truncated = name.substr(0, kUnixMaxNameLength - 1);
#ifdef __APPLE__
    // macOS uses single-argument version (current thread only)
    (void)pthread_setname_np(truncated.c_str());
#else
    // Linux and most other Unix systems use two-argument version
    (void)pthread_setname_np(pthread_self(), truncated.c_str());
#endif
}

std::string platform_get_thread_name()
{
    char buffer[kUnixMaxNameLength] = {0};
    return (pthread_getname_np(pthread_self(), buffer, sizeof(buffer)) == 0) ? std::string(buffer)
                                                                             : std::string{};
}

#endif

}  // anonymous namespace

void set_thread_name(const std::string& name)
{
    safe_string_copy(tls_thread_name, kMaxThreadNameLength, name.c_str());
    platform_set_thread_name(name);
}

std::string get_thread_name()
{
    const std::string platform_name = platform_get_thread_name();
    if (!platform_name.empty())
    {
        return platform_name;
    }
    return (tls_thread_name[0] != '\0') ? std::string(tls_thread_name) : std::string{};
}

}  // namespace graphfic::detail::parallel
