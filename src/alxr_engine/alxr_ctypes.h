#pragma once
#ifndef ALXR_ENGINE_CTYPES_H
#define ALXR_ENGINE_CTYPES_H

#ifdef __cplusplus
extern "C" {;
#endif

#include <stdint.h>

#ifndef ALXR_CLIENT
#define ALXR_CLIENT
#endif
#include "bindings.h"

#undef None

enum ALXRGraphicsApi : uint32_t
{
    Auto=0,
    Vulkan2,
    Vulkan,
    D3D12,
    D3D11,
    ApiCount
};

enum class ALXRDecoderType : uint32_t
{
    D311VA,
    NVDEC,
    CUVID,
    VAAPI,
    CPU
};

enum class ALXRFacialExpressionType : uint8_t {
    None = 0, // Not Support or Disabled
    FB,
    FB_V2,
    HTC,
    Pico,
    Android, // XR_ANDROID_face_tracking
    Auto,
    TypeCount
};

enum class ALXREyeTrackingType : uint8_t {
    None = 0, // Not Support or Disabled
    FBEyeTrackingSocial,
    ExtEyeGazeInteraction,
    AndroidAvatarEyes,
    Auto,
    TypeCount
};

enum class ALXRFaceTrackingDataSource : uint8_t {
    VisualSource = 0,
    AudioSource,
    UnknownSource,
    TypeCount
};

enum class ALXRTrackingSpace : uint32_t
{
    LocalRefSpace,
    StageRefSpace,
    ViewRefSpace
};

enum class ALXRCodecType : uint32_t
{
    H264_CODEC,
    HEVC_CODEC
};

// replicates https://registry.khronos.org/OpenXR/specs/1.0/html/xrspec.html#XR_FB_color_space
enum class ALXRColorSpace : int32_t  {
    Unmanaged = 0,
    Rec2020 = 1,
    Rec709 = 2,
    RiftCV1 = 3,
    RiftS = 4,
    Quest = 5,
    P3 = 6,
    AdobeRgb = 7,
    Default = Quest,
    MaxEnum = 0x7fffffff
};

enum ALXRTrackingEnabledFlags : uint64_t {
    ALXR_TRACKING_ENABLED_HANDS = (1u << 0),
    ALXR_TRACKING_ENABLED_EYES  = (1u << 1),
    ALXR_TRACKING_ENABLED_FACE  = (1u << 2),    
    ALXR_TRACKING_ENABLED_ALL = ALXR_TRACKING_ENABLED_HANDS | ALXR_TRACKING_ENABLED_EYES | ALXR_TRACKING_ENABLED_FACE
};

enum ALXRFaceTrackingDataSourceFlags : uint32_t {
    ALXR_FACE_TRACKING_DATA_SOURCE_VISUAL = (1u << 0),
    ALXR_FACE_TRACKING_DATA_SOURCE_AUDIO  = (1u << 1),
    ALXR_FACE_TRACKING_DATA_SOURCE_ANY    = ALXR_FACE_TRACKING_DATA_SOURCE_VISUAL | ALXR_FACE_TRACKING_DATA_SOURCE_AUDIO
};

enum class ALXRPassthroughMode : uint32_t {
    None = 0,
    BlendLayer,
    MaskLayer,
    TypeCount,
};

struct ALXRSystemProperties
{
    char         systemName[256];
    float        currentRefreshRate;
    const float* refreshRates;
    uint32_t refreshRatesCount;
    uint32_t recommendedEyeWidth;
    uint32_t recommendedEyeHeight;
    uint64_t enabledTrackingSystemsFlags;
};

struct ALXREyeInfo
{
    ALXREyeFov eyeFov[2];
    float ipd;
};

typedef struct ALXRHiddenAreaMesh {
    const ALXRVector2f* vertices;
    uint32_t vertexCount;
    const uint32_t* indices;
    uint32_t indexCount;
} ALXRHiddenAreaMesh;

typedef struct ALXRViewConfig {
    ALXREyeInfo eyeInfo;
    ALXRHiddenAreaMesh hidden_area_meshes[2];
} ALXRViewConfig;

struct ALXRVersion {
    uint32_t major;
    uint32_t minor;
    uint32_t patch;
};

typedef struct ALXRClientCtx
{
    void (*inputSend)(const TrackingInfo* data);
    void (*viewsConfigSend)(const ALXRViewConfig* view_config);
    uint64_t (*pathStringToHash)(const char* path);
    void (*timeSyncSend)(const TimeSync* data);
    void (*videoErrorReportSend)();
    void (*batterySend)(uint64_t device_path, float gauge_value, bool is_plugged);
    void (*setWaitingNextIDR)(const bool);
    void (*requestIDR)();

    ALXRVersion     firmwareVersion;
    ALXRVersion     xrApiVersion;
    ALXRGraphicsApi graphicsApi;
    ALXRDecoderType decoderType;
    ALXRColorSpace  displayColorSpace;
    ALXRPassthroughMode passthroughMode;
    
    //
    // internalDataPath - Optional root path of data files, if null all paths will be relative, 
    // for android builds this could be the same as ANativeActivity::internalDataPath
    //
    const char* internalDataPath;

    uint32_t faceTrackingDataSources;

    ALXRFacialExpressionType facialTracking;
    ALXREyeTrackingType eyeTracking;

    uint16_t trackingServerPortNo;

    bool verbose;
    bool disableLinearizeSrgb;
    bool noSuggestedBindings;
    bool noServerFramerateLock;
    bool noFrameSkip;
    bool disableLocalDimming;
    bool headlessSession;
    bool noFTServer;
    bool noPassthrough;
    bool noHandTracking;
    // Enables a headless OpenXR session if supported by the runtime (same as `headlessSession`).
    // In the absence of native support, will attempt to simulate a headless session.
    // Caution: May not be compatible with all runtimes and could lead to unexpected behavior.
    bool simulateHeadless;
    bool noVisibilityMasks;
    bool noMultiviewRendering;

#ifdef XR_USE_PLATFORM_ANDROID
    void* applicationVM;
    void* applicationActivity;
#endif
} ALXRClientCtx;

struct ALXRGuardianData {
    float areaWidth;
    float areaHeight;
    bool shouldSync;
};

struct ALXRRenderConfig
{
    uint32_t eyeWidth;
    uint32_t eyeHeight;
    float refreshRate;
    float foveationCenterSizeX;
    float foveationCenterSizeY;
    float foveationCenterShiftX;
    float foveationCenterShiftY;
    float foveationEdgeRatioX;
    float foveationEdgeRatioY;
    bool enableFoveation;
};

struct ALXRDecoderConfig
{
    ALXRCodecType codecType;
    uint32_t      cpuThreadCount; // only used for software decoding.
    bool          enableFEC;
    bool          realtimePriority;
};

struct ALXRStreamConfig {
    ALXRTrackingSpace trackingSpaceType;
    ALXRRenderConfig  renderConfig;
    ALXRDecoderConfig decoderConfig;
    bool              clientPrediction;
};

enum ALXRLogOptions : uint32_t {
    ALXR_LOG_OPTION_NONE = 0,
    ALXR_LOG_OPTION_TIMESTAMP = (1u << 0),
    ALXR_LOG_OPTION_LEVEL_TAG = (1u << 1)
};
enum class ALXRLogLevel : uint32_t { Verbose, Info, Warning, Error };
typedef void (*ALXRLogOutputFn)(ALXRLogLevel level, const char* output, uint32_t len);

#ifdef __cplusplus
}
#endif
#endif
