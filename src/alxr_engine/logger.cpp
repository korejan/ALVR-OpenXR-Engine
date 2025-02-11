// Copyright (c) 2017-2023, The Khronos Group Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include "pch.h"
#include "logger.h"

#include <atomic>
#include <cassert>
#include <array>
#include <string_view>
#include <sstream>
#include <chrono>

namespace Log {
namespace {;

#if defined(ANDROID)
inline constexpr const std::array<const android_LogPriority,4> LogPriorityMap = {
    ANDROID_LOG_VERBOSE,
    ANDROID_LOG_INFO,
    ANDROID_LOG_WARN,
    ANDROID_LOG_ERROR
};
#endif

namespace LvlColor {
inline constexpr const std::array<const std::string_view, 4> Begin {
    "",
    "",
    "\033[1;93m",
    "\033[1;91m"
};

inline constexpr const std::array<const std::string_view, 4> End {
    "",
    "",
    "\033[0m",
    "\033[0m"
};
}

std::atomic<unsigned int> g_logOptions{ Log::LogOptions::Timestamp | Log::LogOptions::LevelTag };
Log::Level g_minSeverity{ Log::Level::Info };
std::mutex g_logLock{};

inline void defaultOuput(Log::Level severity, const char* output, std::uint32_t len) {
    if (output == nullptr || len == 0)
        return;
    std::scoped_lock<std::mutex> lock(g_logLock);  // Ensure output is serialized    
#if defined(ANDROID)
    __android_log_print(LogPriorityMap[static_cast<std::size_t>(severity)], "alxr-client", "%s", output);
#else
    auto& logOut = (severity == Log::Level::Error) ? std::clog : std::cout;
    logOut << LvlColor::Begin[static_cast<std::size_t>(severity)]
           << output
           << LvlColor::End[static_cast<std::size_t>(severity)];
#if defined(_WIN32)
    OutputDebugStringA(output);
#endif
#endif
}

std::atomic<Log::OutputFn> g_outputFn{ defaultOuput };

}  // namespace

void SetLevel(Level minSeverity) { g_minSeverity = minSeverity; }

void Write(Level severity, const std::string& msg) {
    if (severity < g_minSeverity || msg.length() == 0) {
        return;
    }

    const auto now = std::chrono::system_clock::now();
    const time_t now_time = std::chrono::system_clock::to_time_t(now);
    tm now_tm;
#ifdef _WIN32
    localtime_s(&now_tm, &now_time);
#else
    localtime_r(&now_time, &now_tm);
#endif
    // time_t only has second precision. Use the rounding error to get sub-second precision.
    const auto secondRemainder = now - std::chrono::system_clock::from_time_t(now_time);
    const int64_t milliseconds = std::chrono::duration_cast<std::chrono::milliseconds>(secondRemainder).count();

    // { Verbose, Info, Warning, Error };
    static constexpr const std::array<const std::string_view, 4> severityName {
        "Verbose", "Info", "Warning", "Error"
    };

    const auto outputFn = g_outputFn.load();
    assert(outputFn != nullptr);
    const auto logOpts = g_logOptions.load();
    if (logOpts == 0) {
        outputFn(severity, msg.c_str(), static_cast<unsigned int>(msg.length()));
        return;
    }

    std::ostringstream out;
    out.fill('0');
    if ((logOpts & LogOptions::Timestamp) != 0) {
        out << "[" << std::setw(2) << now_tm.tm_hour << ":" << std::setw(2) << now_tm.tm_min << ":" << std::setw(2) << now_tm.tm_sec
            << "." << std::setw(3) << milliseconds << "]";
    }
    if ((logOpts & LogOptions::LevelTag) != 0) {
        out << "[" << severityName[static_cast<std::size_t>(severity)] << "] ";
    }
    out << msg << std::endl;
    const auto& result = out.str();
    if (result.length() == 0)
        return;
    outputFn(severity, result.c_str(), static_cast<unsigned int>(result.length()));
}

void SetLogCustomOutput(const LogOptions options, OutputFn outputFn) {
    g_logOptions = options;
    if (outputFn == nullptr)
        outputFn = defaultOuput;
    assert(outputFn != nullptr);
    g_outputFn.store(outputFn);
    assert(g_outputFn != nullptr);
}

}  // namespace Log
