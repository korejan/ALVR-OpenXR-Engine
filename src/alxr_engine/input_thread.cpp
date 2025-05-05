#include "pch.h"
#include "common.h"
#include "logger.h"
#include "input_thread.h"
#include "timing.h"
#include "graphicsplugin.h"
#include "openxr_program.h"

namespace ALXR {
namespace {
inline void LogViewConfig(const ALXREyeInfo& newEyeInfo) {
    constexpr const auto FmtEyeFov = [](const EyeFov& eye) {
        constexpr const float deg = 180.0f / 3.14159265358979323846f;
        return Fmt("{ .left=%f, .right=%f, .top=%f, .bottom=%f }",
            eye.left * deg, eye.right * deg, eye.top * deg, eye.bottom * deg);
    };
    const auto lEyeFovStr = FmtEyeFov(newEyeInfo.eyeFov[0]);
    const auto rEyeFovStr = FmtEyeFov(newEyeInfo.eyeFov[1]);
    Log::Write(Log::Level::Info, Fmt("New view config sent:\n"
        "\tViewConfig {\n"
        "\t  .ipd = %f,\n"
        "\t  .eyeFov {\n"
        "\t    .leftEye  = %s,\n"
        "\t    .rightEye = %s\n"
        "\t  }\n"
        "\t}",
        newEyeInfo.ipd * 1000.0f, lEyeFovStr.c_str(), rEyeFovStr.c_str()));
}
}

void XrInputThread::Update(const XrInputThread::StartCtx& ctx) {

    const auto isConnected = m_isConnected.load();

    ALXRViewConfig newViewConfig = {
        .hidden_area_meshes = {{},{}},
    };
    if (isConnected && ctx.programPtr->GetEyeInfo(newViewConfig.eyeInfo)) {
        const auto& newEyeInfo = newViewConfig.eyeInfo;
        if (std::abs(newEyeInfo.ipd - m_lastEyeInfo.ipd) > 0.01f ||
            std::abs(newEyeInfo.eyeFov[0].left - m_lastEyeInfo.eyeFov[0].left) > 0.01f ||
            std::abs(newEyeInfo.eyeFov[1].left - m_lastEyeInfo.eyeFov[1].left) > 0.01f)
        {
            std::array<IOpenXrProgram::HiddenAreaMesh, 2> hiddenAreaMeshes{};
            for (size_t viewIdx = 0; viewIdx < 2; ++viewIdx) {
                auto& ham = hiddenAreaMeshes[viewIdx];
                if (!ctx.programPtr->GetHiddenAreaMesh(viewIdx, ham) || !ham.IsValid())
                    break;
                static_assert(sizeof(ALXRVector2f) == sizeof(XrVector2f));
                newViewConfig.hidden_area_meshes[viewIdx] = {
                    .vertices = reinterpret_cast<const ALXRVector2f*>(ham.vertices.data()),
                    .vertexCount = static_cast<uint32_t>(ham.vertices.size()),
                    .indices = ham.indices.data(),
                    .indexCount = static_cast<uint32_t>(ham.indices.size())
                };
            }
            m_lastEyeInfo = newEyeInfo;
            ctx.clientCtx->viewsConfigSend(&newViewConfig);
            LogViewConfig(newEyeInfo);
        }
    }

    ctx.programPtr->PollActions();
    if (!isConnected)
        return;

    TrackingInfo newInfo;
    if (!ctx.programPtr->GetTrackingInfo(newInfo, m_clientPrediction))
        return;
    ctx.clientCtx->inputSend(&newInfo);
}

void XrInputThread::Run(const XrInputThread::StartCtx& ctx) {    
    CHECK(ctx.clientCtx);
    CHECK(ctx.programPtr);

    using namespace std::chrono;
    using namespace std::chrono_literals;
    static_assert(XrSteadyClock::is_steady);

#ifdef XR_USE_PLATFORM_ANDROID
    const struct ScopedJNIEnv final {
        JNIEnv* jniEnv = nullptr;
        JavaVM* vm;
        ScopedJNIEnv(JavaVM* vmp): vm{vmp} {
            if (vm)
                vm->AttachCurrentThread(&jniEnv, nullptr);
        }
        ~ScopedJNIEnv() {
            if (vm)
                vm->DetachCurrentThread();
        }
    } scopedJNIEnv{ reinterpret_cast<JavaVM*>(ctx.clientCtx->applicationVM) };
#endif

    ctx.programPtr->SetAndroidAppThread(AndroidThreadType::AppWorker);

    auto nextWakeTime = XrSteadyClock::now();
	while (m_isRunning.load()) {        

        Update(ctx);

        const auto targetFrameDuration = GetTargetFrameDuration();

        nextWakeTime += targetFrameDuration;
        // Adjust nextWakeTime if it's behind the current time
        const auto currentTime = XrSteadyClock::now();
        if (nextWakeTime <= currentTime) {
            // Adjust nextWakeTime to the next future interval
            const auto missedIntervals = ((currentTime - nextWakeTime) / targetFrameDuration) + 1;
            nextWakeTime = currentTime + missedIntervals * targetFrameDuration;
        }
        std::this_thread::sleep_until(nextWakeTime);
	}
}

}
