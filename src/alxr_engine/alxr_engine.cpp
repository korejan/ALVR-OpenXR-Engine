// Copyright (c) 2017-2021, The Khronos Group Inc.
//
// SPDX-License-Identifier: Apache-2.0
#include "logger.h"
#define ENGINE_DLL_EXPORTS

#include "pch.h"
#include "common.h"
#include "options.h"
#include "platformdata.h"
#include "platformplugin.h"
#include "graphicsplugin.h"
#include "openxr_program.h"

#include <cassert>
#include <cstdint>
#include <type_traits>
#include <memory>
#include <mutex>

#include "alxr_engine.h"
#include "alxr_facial_eye_tracking_packet.h"

#include "timing.h"
#include "interaction_manager.h"
#include "latency_manager.h"
#include "decoder_thread.h"
#include "foveation.h"
#include "input_thread.h"

#if defined(XR_USE_PLATFORM_WIN32) && defined(XR_EXPORT_HIGH_PERF_GPU_SELECTION_SYMBOLS)
#pragma message("Enabling Symbols to select high-perf GPUs first")
// Export symbols to get the high performance gpu as first adapter in IDXGIFactory::EnumAdapters().
// This can be also necessary for the IMFActivate::ActivateObject method if no windows graphic settings are present.
extern "C" {
    // http://developer.download.nvidia.com/devzone/devcenter/gamegraphics/files/OptimusRenderingPolicies.pdf
    _declspec(dllexport) DWORD NvOptimusEnablement = 0x00000001;
    // https://gpuopen.com/learn/amdpowerxpressrequesthighperformance/
    _declspec(dllexport) DWORD AmdPowerXpressRequestHighPerformance = 0x00000001;
}
#endif

using IOpenXrProgramPtr = std::shared_ptr<IOpenXrProgram>;
using ClientCtxPtr = std::shared_ptr<const ALXRClientCtx>;

ClientCtxPtr      gClientCtx{ nullptr };
IOpenXrProgramPtr gProgram{ nullptr };
XrDecoderThread   gDecoderThread{};
ALXR::XrInputThread gInputThread{};
std::mutex        gRenderMutex{};

namespace ALXRStrings {
    constexpr inline const char* const HeadPath         = "/user/head";
    constexpr inline const char* const LeftHandPath     = "/user/hand/left";
    constexpr inline const char* const RightHandPath    = "/user/hand/right";
    constexpr inline const char* const LeftHandHaptics  = "/user/hand/left/output/haptic";
    constexpr inline const char* const RightHandHaptics = "/user/hand/right/output/haptic";
};

constexpr inline auto graphics_api_str(const ALXRGraphicsApi gcp)
{
    switch (gcp)
    {
    case ALXRGraphicsApi::Vulkan2:
        return "Vulkan2";
    case ALXRGraphicsApi::Vulkan:
        return "Vulkan";
    case ALXRGraphicsApi::D3D12:
        return "D3D12";
    case ALXRGraphicsApi::D3D11:
        return "D3D11";
    default:
        return "auto";
    }
}

constexpr inline bool is_valid(const ALXRClientCtx& rCtx)
{
    return  rCtx.inputSend != nullptr &&
            rCtx.viewsConfigSend != nullptr &&
            rCtx.pathStringToHash != nullptr &&
            rCtx.requestIDR != nullptr;
}

bool alxr_init(const ALXRClientCtx* rCtx, /*[out]*/ ALXRSystemProperties* systemProperties) {
    try {
        if (rCtx == nullptr || !is_valid(*rCtx))
        {
            Log::Write(Log::Level::Error, "Rust context has not been setup!");
            return false;
        }
        
        gClientCtx = std::make_shared<ALXRClientCtx>(*rCtx);
        const auto &ctx = *gClientCtx;
        if (ctx.verbose)
            Log::SetLevel(Log::Level::Verbose);
        
        LatencyManager::Instance().Init(LatencyManager::CallbackCtx {
            .sendFn = ctx.inputSend,
            .timeSyncSendFn = ctx.timeSyncSend,
            .videoErrorReportSendFn = ctx.videoErrorReportSend
        });

        const auto options = std::make_shared<Options>();
        assert(options->AppSpace == "Stage");
        assert(options->ViewConfiguration == "Stereo");
        options->DisableLinearizeSrgb = ctx.disableLinearizeSrgb;
        options->DisableSuggestedBindings = ctx.noSuggestedBindings;
        options->NoServerFramerateLock = ctx.noServerFramerateLock;
        options->NoFrameSkip = ctx.noFrameSkip;
        options->DisableLocalDimming = ctx.disableLocalDimming;
        options->HeadlessSession = ctx.headlessSession;
        options->NoFTServer = ctx.noFTServer;
        options->NoPassthrough = ctx.noPassthrough;
        options->NoHandTracking = ctx.noHandTracking;
        options->FacialTracking = ctx.facialTracking;
        options->EyeTracking = ctx.eyeTracking;
        options->DisplayColorSpace = static_cast<XrColorSpaceFB>(ctx.displayColorSpace);
        const auto& fmVersion = ctx.firmwareVersion;
        options->firmwareVersion = { fmVersion.major, fmVersion.minor, fmVersion.patch };
        options->TrackingServerPortNo = static_cast<std::uint16_t>(ctx.trackingServerPortNo);
        options->SimulateHeadless = ctx.simulateHeadless;
        options->PassthroughMode = ctx.passthroughMode;
        options->NoVisibilityMasks = ctx.noVisibilityMasks;
        options->NoMultiviewRendering = ctx.noMultiviewRendering;
        if (ctx.internalDataPath != nullptr) {
            std::error_code ec;
            options->InternalDataPath = ctx.internalDataPath;
            if (!std::filesystem::exists(options->InternalDataPath, ec) || ec) {
                options->InternalDataPath.clear();
            }
            auto internalDataPathStr = options->InternalDataPath.string();
            Log::Write(Log::Level::Verbose, Fmt("InternalDataPath: %s", internalDataPathStr.c_str()));
        }
        if (ctx.faceTrackingDataSources != 0)
            options->FaceTrackingDataSources = ctx.faceTrackingDataSources;
        if (options->GraphicsPlugin.empty())
            options->GraphicsPlugin = graphics_api_str(ctx.graphicsApi);

        const auto platformData = std::make_shared<PlatformData>();
#ifdef XR_USE_PLATFORM_ANDROID
#pragma message ("Android Loader Enabled.")
        platformData->applicationVM = ctx.applicationVM;
        platformData->applicationActivity = ctx.applicationActivity;

        // Initialize the loader for this platform
        PFN_xrInitializeLoaderKHR initializeLoader = nullptr;
        if (XR_SUCCEEDED(xrGetInstanceProcAddr(XR_NULL_HANDLE, "xrInitializeLoaderKHR",
                                      (PFN_xrVoidFunction *) (&initializeLoader)))) {
            const XrLoaderInitInfoAndroidKHR loaderInitInfoAndroid {
                .type = XR_TYPE_LOADER_INIT_INFO_ANDROID_KHR,
                .next = nullptr,
                .applicationVM = ctx.applicationVM,
                .applicationContext = ctx.applicationActivity
            };
            initializeLoader((const XrLoaderInitInfoBaseHeaderKHR *) &loaderInitInfoAndroid);
        }
        else {
            Log::Write(Log::Level::Error, "Failed to initialize android loader!");
        }

        //av_jni_set_java_vm(ctx.applicationVM, nullptr);
#endif
        // Create platform-specific implementation.
        const auto platformPlugin = CreatePlatformPlugin(options, platformData);        
        // Initialize the OpenXR gProgram.
        gProgram = CreateOpenXrProgram(options, platformPlugin);
        gProgram->CreateInstance();
        gProgram->InitializeSystem(ALXR::ALXRPaths {
            .head           = rCtx->pathStringToHash(ALXRStrings::HeadPath),
            .left_hand      = rCtx->pathStringToHash(ALXRStrings::LeftHandPath),
            .right_hand     = rCtx->pathStringToHash(ALXRStrings::RightHandPath),
            .left_haptics   = rCtx->pathStringToHash(ALXRStrings::LeftHandHaptics),
            .right_haptics  = rCtx->pathStringToHash(ALXRStrings::RightHandHaptics)
        });
        gProgram->InitializeSession();
        gProgram->CreateSwapchains();

        ALXRSystemProperties rustSysProp{};
        gProgram->GetSystemProperties(rustSysProp);
        if (systemProperties)
            *systemProperties = rustSysProp;

        const ALXR::XrInputThread::StartCtx startCtx = {
            .programPtr = gProgram,
            .clientCtx = gClientCtx,
        };
        gInputThread.Start(startCtx);

        Log::Write(Log::Level::Info, Fmt("device name: %s", rustSysProp.systemName));
        Log::Write(Log::Level::Info, "openxrInit finished successfully");

        return true;
    } catch (const std::exception& ex) {
        Log::Write(Log::Level::Error, ex.what());
        return false;
    } catch (...) {
        Log::Write(Log::Level::Error, "Unknown Error");
        return false;
    }
}

void alxr_stop_decoder_thread()
{
#ifndef XR_DISABLE_DECODER_THREAD
    gDecoderThread.Stop();
#endif
}

void alxr_destroy() {
    const auto clientCtx = gClientCtx;
    if (clientCtx == nullptr) {
        assert(gProgram == nullptr);
        return;
    }
    Log::Write(Log::Level::Info, "openxrShutdown: Shuttingdown");
    gInputThread.Stop();
    if (const auto programPtr = gProgram) {
        if (const auto graphicsPtr = programPtr->GetGraphicsPlugin()) {
            std::scoped_lock lk(gRenderMutex);
            graphicsPtr->ClearVideoTextures();
        }
    }
    alxr_stop_decoder_thread();
    gProgram.reset();
    gClientCtx.reset();
}

void alxr_request_exit_session() {
    if (const auto programPtr = gProgram) {
        programPtr->RequestExitSession();
    }
}

void alxr_process_frame(bool* exitRenderLoop /*= non-null */, bool* requestRestart /*= non-null */) {
    assert(exitRenderLoop != nullptr && requestRestart != nullptr);
    assert(gProgram != nullptr);

    gProgram->PollEvents(exitRenderLoop, requestRestart);
    if (*exitRenderLoop || !gProgram->IsSessionRunning())
        return;
    
    //gProgram->PollActions();
    {
        std::scoped_lock lk(gRenderMutex);
        gProgram->RenderFrame();
    }
}

void alxr_process_frame2(ALXRProcessFrameResult* frameResult) {
    try {
        if (frameResult == nullptr)
            return;

        assert(gProgram != nullptr);
        gProgram->PollEvents(&frameResult->exitRenderLoop, &frameResult->requestRestart);
        if (frameResult->exitRenderLoop || !gProgram->IsSessionRunning())
            return;

        {
            std::scoped_lock lk(gRenderMutex);
            gProgram->RenderFrame();
        }

        gProgram->PollHandTracking(frameResult->handTracking);
        gProgram->PollFaceEyeTracking(frameResult->facialEyeTracking);

    } catch (const std::exception& ex) {
        frameResult->exitRenderLoop = true;
        frameResult->requestRestart = false;
        Log::Write(Log::Level::Error, ex.what());
    } catch (...) {
        frameResult->exitRenderLoop = true;
        frameResult->requestRestart = false;
        Log::Write(Log::Level::Error, "Unknown Error!");
    }
}

bool alxr_is_session_running()
{
    if (const auto programPtr = gProgram)
        return gProgram->IsSessionRunning();
    return false;
}

void alxr_set_stream_config(const ALXRStreamConfig config)
{
    const auto programPtr = gProgram;
    if (programPtr == nullptr)
        return;
    alxr_stop_decoder_thread();
    if (const auto graphicsPtr = programPtr->GetGraphicsPlugin()) {
        const auto& rc = config.renderConfig;
        std::scoped_lock lk(gRenderMutex);
        programPtr->SetRenderMode(IOpenXrProgram::RenderMode::Lobby);
        graphicsPtr->ClearVideoTextures();
        
        ALXR::FoveatedDecodeParams fdParams{};
        if (rc.enableFoveation)
            fdParams = ALXR::MakeFoveatedDecodeParams(rc);
        graphicsPtr->SetFoveatedDecode(rc.enableFoveation ? &fdParams : nullptr);
        programPtr->CreateSwapchains(rc.eyeWidth, rc.eyeHeight);
    }

#ifndef XR_DISABLE_DECODER_THREAD
    if (!programPtr->IsHeadlessSession()) {
        Log::Write(Log::Level::Info, "Starting decoder thread.");

        const XrDecoderThread::StartCtx startCtx{
            .decoderConfig = config.decoderConfig,
            .programPtr = programPtr,
            .clientCtx = gClientCtx
        };
        gDecoderThread.Start(startCtx);
        Log::Write(Log::Level::Info, "Decoder Thread started.");
    }
#endif
    // OpenXR does not have functions to query the battery levels of devices.
    const auto SendDummyBatteryLevels = []() {
        const auto rCtx = gClientCtx;
        if (rCtx == nullptr)
            return;
        const auto head_path        = rCtx->pathStringToHash(ALXRStrings::HeadPath);
        const auto left_hand_path   = rCtx->pathStringToHash(ALXRStrings::LeftHandPath);
        const auto right_hand_path  = rCtx->pathStringToHash(ALXRStrings::RightHandPath);
        // TODO: On android we can still get the real battery levels of the "HMD"
        //       by registering an IntentFilter battery change event.
        rCtx->batterySend(head_path, 1.0f, true);
        rCtx->batterySend(left_hand_path, 1.0f, true);
        rCtx->batterySend(right_hand_path, 1.0f, true);
    };
    SendDummyBatteryLevels();
    programPtr->SetStreamConfig(config);

    gInputThread.SetTargetFrameRate(config.renderConfig.refreshRate)
        .SetClientPrediction(config.clientPrediction)
        .SetConnected(true);
}

void alxr_on_server_disconnect()
{
    gInputThread.SetConnected(false);
    if (const auto programPtr = gProgram) {
        programPtr->SetRenderMode(IOpenXrProgram::RenderMode::Lobby);
    }
}

ALXRGuardianData alxr_get_guardian_data()
{
    ALXRGuardianData gd {
        .areaWidth = 0,
        .areaHeight = 0,
        .shouldSync = false,
    };
    if (const auto programPtr = gProgram) {
        programPtr->GetGuardianData(gd);
    }
    return gd;
}

void alxr_on_pause()
{
    if (const auto programPtr = gProgram)
        programPtr->Pause();    
}

void alxr_on_resume()
{
    if (const auto programPtr = gProgram)
        programPtr->Resume();
}

[[deprecated]]
void alxr_on_tracking_update(const bool /*clientsidePrediction*/) {
    CHECK_MSG(false, "Deprecated function called!");
}

void alxr_on_receive(const unsigned char* packet, unsigned int packetSize)
{
    const auto programPtr = gProgram;
    if (programPtr == nullptr)
        return;
    const std::uint32_t type = *reinterpret_cast<const uint32_t*>(packet);
    switch (type) {
        case ALVR_PACKET_TYPE_VIDEO_FRAME: {
#ifndef XR_DISABLE_DECODER_THREAD
            assert(packetSize >= sizeof(VideoFrame));
            const auto& header = *reinterpret_cast<const VideoFrame*>(packet);
            gDecoderThread.QueuePacket(header, packetSize);
#else
        (void)packetSize;
#endif
        } break;        
        case ALVR_PACKET_TYPE_TIME_SYNC: {
            assert(packetSize >= sizeof(TimeSync));
            LatencyManager::Instance().OnTimeSyncRecieved(*(TimeSync*)packet);
        } break;
    }
}

void alxr_on_haptics_feedback(unsigned long long path, float duration_s, float frequency, float amplitude)
{
    if (const auto programPtr = gProgram) {
        programPtr->ApplyHapticFeedback(ALXR::HapticsFeedback {
            .alxrPath   = path,
            .amplitude  = amplitude,
            .duration   = duration_s,
            .frequency  = frequency
        });
    }
}

void alxr_on_video_packet(const VideoFrame* headerPtr, const unsigned char* packet, unsigned int packetSize)
{
#ifdef XR_DISABLE_DECODER_THREAD
    (void)headerPtr;
    (void)packet;
    (void)packetSize;
#else
    if (const auto programPtr = gProgram) {
        assert(headerPtr != nullptr);
        const auto& header = *headerPtr;
        gDecoderThread.QueuePacket(header, XrDecoderThread::VideoPacket{
            packet,
            static_cast<std::size_t>(packetSize)
        });
    }
#endif
}

void alxr_on_time_sync(const TimeSync* packet) {
    if (const auto programPtr = gProgram) {
        assert(packet != nullptr);
        LatencyManager::Instance().OnTimeSyncRecieved(*packet);
    }
}

void alxr_set_log_custom_output(ALXRLogOptions options, ALXRLogOutputFn outputFn) {
    static_assert(
        std::is_same<
            std::underlying_type<ALXRLogLevel>::type,
            std::underlying_type<Log::Level>::type
        >::value
    );
    Log::SetLogCustomOutput(
        static_cast<Log::LogOptions>(options),
        reinterpret_cast<Log::OutputFn>(outputFn)
    );
}
