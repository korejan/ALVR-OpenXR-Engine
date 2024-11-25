// Copyright (c) 2017-2025 The Khronos Group Inc.
//
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <array>
#include <string>
#include <string_view>
#include <locale>
#include <functional>
#include <algorithm>
#include <memory>
#include <stdarg.h>
#include <stddef.h>

#include <openxr/openxr_reflection.h>

// Macro to generate stringify functions for OpenXR enumerations based data provided in openxr_reflection.h
// clang-format off
#define ENUM_CASE_STR(name, val) case name: return #name;
#define MAKE_TO_STRING_FUNC(enumType)                  \
    inline const char* to_string(enumType e) {         \
        switch (e) {                                   \
            XR_LIST_ENUM_##enumType(ENUM_CASE_STR)     \
            default: return "Unknown " #enumType;      \
        }                                              \
    }
// clang-format on

MAKE_TO_STRING_FUNC(XrReferenceSpaceType);
MAKE_TO_STRING_FUNC(XrViewConfigurationType);
MAKE_TO_STRING_FUNC(XrEnvironmentBlendMode);
MAKE_TO_STRING_FUNC(XrSessionState);
MAKE_TO_STRING_FUNC(XrResult);
MAKE_TO_STRING_FUNC(XrFormFactor);

inline bool EqualsIgnoreCase(const std::string_view& s1, const std::string_view& s2, const std::locale& loc = std::locale()) {
    const std::ctype<char>& ctype = std::use_facet<std::ctype<char>>(loc);
    const auto compareCharLower = [&](char c1, char c2) { return ctype.tolower(c1) == ctype.tolower(c2); };
    return s1.size() == s2.size() && std::equal(s1.begin(), s1.end(), s2.begin(), compareCharLower);
}

struct IgnoreCaseStringLess {
    bool operator()(const std::string& a, const std::string& b, const std::locale& loc = std::locale()) const noexcept {
        const std::ctype<char>& ctype = std::use_facet<std::ctype<char>>(loc);
        const auto ignoreCaseCharLess = [&](char c1, char c2) { return ctype.tolower(c1) < ctype.tolower(c2); };
        return std::lexicographical_compare(a.begin(), a.end(), b.begin(), b.end(), ignoreCaseCharLess);
    }
};

template <typename T>
struct ScopeGuard {
    // Needs C++17: static_assert(std::is_invocable_v<T>, "Type must be invocable function.");

    ScopeGuard(T&& guard) noexcept : m_guard(std::move(guard)) {}

    ScopeGuard(ScopeGuard&&) noexcept = default;
    ScopeGuard& operator=(ScopeGuard&&) noexcept = default;

    ScopeGuard(ScopeGuard&) = delete;
    ScopeGuard& operator=(ScopeGuard&) = delete;

    ~ScopeGuard() { m_guard(); }

   private:
    T m_guard;
};

// Usage: auto guard = MakeScopeGuard([&] { foobar; });
template <typename T>
inline ScopeGuard<T> MakeScopeGuard(T&& guard) {
    return ScopeGuard<T>(std::forward<T>(guard));
}

inline std::string Fmt(const char* fmt, ...) {
    va_list vl;
    va_start(vl, fmt);
    int size = std::vsnprintf(nullptr, 0, fmt, vl);
    va_end(vl);
    if (size < 0)
        return {};

    std::string buffer(size + 1, '\0');
    va_start(vl, fmt);
    size = std::vsnprintf(buffer.data(), size + 1, fmt, vl);
    va_end(vl);
    if (size < 0)
        return {};
    buffer.pop_back();
    return buffer;
}

// The equivalent of C++17 std::size. A helper to get the dimension for an array.
template <typename T, size_t Size>
constexpr inline size_t ArraySize(const T (&/*unused*/)[Size]) noexcept {
    return Size;
}

template <typename T, size_t Size>
constexpr inline size_t ArraySizeOf(const std::array<T,Size>&) noexcept {
    return sizeof(T) * Size;
}

template <typename T>
inline void HashCombine(std::size_t& seed, const T& value) noexcept {
    std::hash<T> hasher;
    seed ^= hasher(value) + 0x9e3779b9 + (seed << 6) + (seed >> 2);
}

template <typename... Args>
inline std::size_t HashCombineAll(const Args&... args) noexcept {
    std::size_t seed = 0;
    (HashCombine(seed, args), ...);  // Fold expression
    return seed;
}

#include "logger.h"
#include "check.h"
