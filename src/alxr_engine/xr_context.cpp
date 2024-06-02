#include "xr_context.h"
#include "timing.h"

namespace ALXR {
namespace {
inline constexpr const std::int64_t NSecPerSec = 1000000000LL;

#ifdef XR_USE_PLATFORM_WIN32
inline std::int64_t ToTimeNs(const LARGE_INTEGER& ctr) {
    // The following has been replicated from vc++'s implemetnation of 
    // std::chrono::steady_clock::now.
    using PeriodT = std::nano;
    const std::int64_t freq = _Query_perf_frequency(); // doesn't change after system boot
    static_assert(PeriodT::num == 1, "This assumes period::num == 1.");
    // 10 MHz is a very common QPC frequency on modern PCs. Optimizing for
    // this specific frequency can double the performance of this function by
    // avoiding the expensive frequency conversion path.
    constexpr const std::int64_t TenMHz = 10'000'000;
    if (freq == TenMHz) {
        static_assert(PeriodT::den % TenMHz == 0, "It should never fail.");
        constexpr const std::int64_t Multiplier = PeriodT::den / TenMHz;
        return static_cast<std::int64_t>(ctr.QuadPart * Multiplier);
    } else {
        // Instead of just having "(_Ctr * period::den) / _Freq",
        // the algorithm below prevents overflow when _Ctr is sufficiently large.
        // It assumes that _Freq * period::den does not overflow, which is currently true for nano period.
        // It is not realistic for _Ctr to accumulate to large values from zero with this assumption,
        const std::int64_t whole = (ctr.QuadPart / freq) * PeriodT::den;
        const std::int64_t part = (ctr.QuadPart % freq) * PeriodT::den / freq;
        return (whole + part);
    }
}
#else
inline std::int64_t ToTimeNs(const struct timespec& ts) {
    return (static_cast<std::int64_t>(ts.tv_sec) * NSecPerSec) + ts.tv_nsec;
}
#endif
}

std::int64_t XrContext::ToNanoseconds(const XrTime xrt) const {
#ifdef XR_USE_PLATFORM_WIN32
    LARGE_INTEGER ts;
    if (gExtFns.pxrConvertTimeToWin32PerformanceCounterKHR == nullptr ||
        XR_FAILED(gExtFns.pxrConvertTimeToWin32PerformanceCounterKHR(instance, xrt, &ts))) {
#else
    struct timespec ts;
    if (gExtFns.pxrConvertTimeToTimespecTimeKHR == nullptr ||
        XR_FAILED(gExtFns.pxrConvertTimeToTimespecTimeKHR(instance, xrt, &ts))) {
#endif
        return static_cast<std::int64_t>(xrt);
    }
    return ToTimeNs(ts);
}

XrTime XrContext::ToXrTime(const std::int64_t timeNS) const {
    static_assert(sizeof(XrTime) == sizeof(std::int64_t) && std::is_signed<XrTime>::value);
    XrTime xrTimeNow;
#ifdef XR_USE_PLATFORM_WIN32
    const LARGE_INTEGER ts = {
        .QuadPart = (timeNS * _Query_perf_frequency()) / NSecPerSec,
    };
    if (gExtFns.pxrConvertWin32PerformanceCounterToTimeKHR == nullptr ||
        XR_FAILED(gExtFns.pxrConvertWin32PerformanceCounterToTimeKHR(instance, &ts, &xrTimeNow))) {
#else
    const struct timespec ts = {
        .tv_sec  = timeNS / NSecPerSec,
        .tv_nsec = timeNS % NSecPerSec,
    };
    if (gExtFns.pxrConvertTimespecTimeToTimeKHR == nullptr ||
        XR_FAILED(gExtFns.pxrConvertTimespecTimeToTimeKHR(instance, &ts, &xrTimeNow))) {
#endif
        return static_cast<XrTime>(timeNS);
    }
    return xrTimeNow;
}

std::tuple<XrTime, std::int64_t> XrContext::XrTimeNow() const {
    static_assert(sizeof(XrTime) == sizeof(std::int64_t) && std::is_signed<XrTime>::value);
    XrTime xrTimeNow;
#ifdef XR_USE_PLATFORM_WIN32
    const LARGE_INTEGER ts = {
        .QuadPart = _Query_perf_counter(),
    };
    if (gExtFns.pxrConvertWin32PerformanceCounterToTimeKHR == nullptr ||
        XR_FAILED(gExtFns.pxrConvertWin32PerformanceCounterToTimeKHR(instance, &ts, &xrTimeNow))) {
#else
    struct timespec ts;
    if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0)
        return { -1, -1 };
    if (gExtFns.pxrConvertTimespecTimeToTimeKHR == nullptr ||
        XR_FAILED(gExtFns.pxrConvertTimespecTimeToTimeKHR(instance, &ts, &xrTimeNow))) {
#endif
        const auto timeNs = ToTimeNs(ts);
        return { static_cast<XrTime>(timeNs), timeNs };
    }
    return { xrTimeNow, ToTimeNs(ts) };
}
}
