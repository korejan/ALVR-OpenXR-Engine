//#define XR_USE_PLATFORM_ANDROID
#if defined(XR_USE_PLATFORM_ANDROID) && !defined(XR_DISABLE_DECODER_THREAD)
#include "pch.h"
#include "common.h"
#include "decoderplugin.h"

#include <span>
#include <vector>
#include <chrono>
#include <atomic>
#include <mutex>
#include <condition_variable>
#include <thread>

#include <readerwritercircularbuffer.h>

#include <media/NdkMediaCodec.h>
#include <media/NdkMediaMuxer.h> 
#include <media/NdkMediaError.h>
#include <media/NdkMediaFormat.h>
#include <media/NdkImage.h>
#include <media/NdkImageReader.h>
#include <media/NdkMediaCrypto.h>
#include <media/NdkMediaDrm.h>
#include <media/NdkMediaExtractor.h>

#include "alxr_ctypes.h"
#include "nal_utils.h"
#include "graphicsplugin.h"
#include "openxr_program.h"
#include "latency_manager.h"
#include "timing.h"

#ifndef ENABLE_IF_DEBUG
#ifndef NDEBUG
    #define ENABLE_IF_DEBUG(x) x
#else
	#define ENABLE_IF_DEBUG(x)
#endif
#endif

namespace
{;
struct FrameIndexMap final
{
    using TimeStamp = std::uint64_t;
    using FrameIndex = std::uint64_t;
    constexpr static const FrameIndex NullIndex = FrameIndex(-1);
private:
    using Value = std::atomic<FrameIndex>;
    using FrameMap = std::vector<Value>;
    FrameMap m_frameMap;

    constexpr inline std::size_t index(const TimeStamp ts) const
    {
        return ts % m_frameMap.size();
    }

public:
    inline FrameIndexMap(const std::size_t N)
    : m_frameMap(N) {}
    inline FrameIndexMap(const FrameIndexMap&) = delete;
    inline FrameIndexMap(FrameIndexMap&&) = delete;
    inline FrameIndexMap& operator=(const FrameIndexMap&) = delete;
    inline FrameIndexMap& operator=(FrameIndexMap&&) = delete;

    constexpr inline void set(const TimeStamp ts, const FrameIndex newIdx)
    {
        m_frameMap[index(ts)].store(newIdx);
    }

    constexpr inline FrameIndex get(const TimeStamp ts) const
    {
        return m_frameMap[index(ts)].load();
    }

    constexpr inline FrameIndex get_clear(const TimeStamp ts)
    {
        return m_frameMap[index(ts)].exchange(NullIndex);
    }
};

struct XrImageListener final
{
    using IOpenXrProgramPtr = std::shared_ptr<IOpenXrProgram>;

    struct AImageReaderDeleter final {
        void operator()(AImageReader* reader) const {
			if (reader == nullptr)
				return;
			AImageReader_delete(reader);
		}
	};
    using AImageReaderPtr = std::unique_ptr<AImageReader, AImageReaderDeleter>;

    FrameIndexMap     frameIndexMap{ 4096 };
    IOpenXrProgramPtr programPtr;
    AImageReaderPtr   imageReader;
    AImageReader_ImageListener imageListener;

    // This mutex is only neccessary where in the case of a residue OnImageAvailable callback is still "processing" or waiting
    // during/after an XrImageListener has been destroyed in another thread, this should not be used in any other case.
    std::mutex        listenerDestroyMutex;

    constexpr static const std::uint64_t ImageReaderFlags =
        AHARDWAREBUFFER_USAGE_CPU_WRITE_NEVER |
        AHARDWAREBUFFER_USAGE_CPU_READ_NEVER |
        AHARDWAREBUFFER_USAGE_GPU_SAMPLED_IMAGE;

    constexpr static const std::int32_t MaxImageCount = 5;

    inline static AImageReaderPtr MakeImageReader()
    {
        AImageReader* newImgReader = nullptr;
        if (AImageReader_newWithUsage(1, 1, AIMAGE_FORMAT_PRIVATE, ImageReaderFlags, MaxImageCount, &newImgReader) != AMEDIA_OK) {
            return {};
        }
        return AImageReaderPtr { newImgReader };
    }

    XrImageListener(const IOpenXrProgramPtr& pptr)
    : programPtr(pptr),
      imageReader(MakeImageReader()),
      imageListener {
          .context = this,
          .onImageAvailable = &XrImageListener::OnImageAvailable
      }
    {
        if (programPtr == nullptr || imageReader == nullptr) {
            imageReader.reset();
            programPtr.reset();
            return;
        }
        if (AImageReader_setImageListener(imageReader.get(), &imageListener) != AMEDIA_OK) {
            Log::Write(Log::Level::Error, "XrImageListener: Failed to set image listener");
            imageReader.reset();
            programPtr.reset();
        }
    }

    inline XrImageListener(const XrImageListener&) noexcept = delete;
    inline XrImageListener(XrImageListener&&)  noexcept = delete;
    inline XrImageListener& operator=(const XrImageListener&) noexcept = delete;
    inline XrImageListener& operator=(XrImageListener&&)  noexcept = delete;

    ~XrImageListener()
    {
        {
            std::scoped_lock sl(listenerDestroyMutex);
            if (imageReader)
                AImageReader_setImageListener(imageReader.get(), nullptr);
            imageListener.onImageAvailable = nullptr;
        }
        Log::Write(Log::Level::Info, "XrImageListener destroyed");
    }

    inline bool IsValid() const { return imageReader != nullptr || programPtr != nullptr; }

    inline ANativeWindow* GetWindow() const
    {
        if (imageReader == nullptr)
            return nullptr;
        ANativeWindow* surface_handle = nullptr;
        if (AImageReader_getWindow(imageReader.get(), &surface_handle) != AMEDIA_OK) {
            return nullptr;
        }
        return surface_handle;
    }

    struct AImageDeleter final {
        void operator()(AImage* img) const {
			if (img == nullptr)
				return;
			AImage_delete(img);
		}
	};
    using AImagePtr = std::unique_ptr<AImage, AImageDeleter>;

    inline void OnImageAvailable(AImageReader* reader)
    {
        std::scoped_lock sl(listenerDestroyMutex);
        
        auto img = [&]() -> AImagePtr {
            AImage* tmp = nullptr;
            if (AImageReader_acquireLatestImage(reader, &tmp) != AMEDIA_OK)
                return {};
            return AImagePtr { tmp };
        }();
        if (img == nullptr) {
            Log::Write(Log::Level::Error, "XrImageListener: Failed to acquire latest AImage");
            return;
        }

        std::int64_t presentationTimeNs = 0;
        AImage_getTimestamp(img.get(), &presentationTimeNs);
        const auto ptsUs = static_cast<std::uint64_t>(presentationTimeNs * 0.001);
        const auto frameIndex = frameIndexMap.get_clear(ptsUs);
        if (frameIndex == FrameIndexMap::NullIndex) {
            Log::Write(Log::Level::Warning, Fmt("XrImageListener: Unknown frame index for pts: %lld us, frame ignored", ptsUs));
            return;
        }

        if (const auto graphicsPluginPtr = programPtr->GetGraphicsPlugin()) {
            std::int32_t w = 0, h = 0;
            AImage_getWidth(img.get(), &w);
            AImage_getHeight(img.get(), &h);
            const IGraphicsPlugin::YUVBuffer buf{
                .luma {
                    .data = img.release(),
                    .pitch = (std::size_t)w,
                    .height = (std::size_t)h
                },
                .frameIndex = frameIndex
            };
            graphicsPluginPtr->UpdateVideoTextureMediaCodec(buf);
        }
    }

    static inline void OnImageAvailable(void* ctx, AImageReader* reader)
    {
        assert(ctx != nullptr);
        reinterpret_cast<XrImageListener*>(ctx)->OnImageAvailable(reader);
    }
};

struct AMediaCodecDeleter final {
    void operator()(AMediaCodec* codec) const {
        if (codec == nullptr)
            return;
        ENABLE_IF_DEBUG(const auto deleteResult =) AMediaCodec_delete(codec);
        assert(deleteResult == AMEDIA_OK);
    }
};
using AMediaCodecPtr = std::unique_ptr<AMediaCodec, AMediaCodecDeleter>;

struct MediaCodecDecoderPlugin final : IDecoderPlugin
{
    using GraphicsPluginPtr = std::shared_ptr<IGraphicsPlugin>;

    IDecoderPlugin::RunCtx m_runCtx;
    
    using InputBufferQueue  = moodycamel::BlockingReaderWriterCircularBuffer<std::int32_t>;
    struct OutputBufferId {
        std::int64_t presentationTimeUs;
        std::size_t  bufferId;
    };
    using OutputBufferQueue = moodycamel::BlockingReaderWriterCircularBuffer<OutputBufferId>;
    
    OutputBufferQueue m_outputBufferQueue{ 120 };
    InputBufferQueue  m_inputBufferQueue { 120 };

    struct CodecCtx final {        
        XrImageListener imgListener;
        AMediaCodecPtr  codec{};
        
        CodecCtx(const IDecoderPlugin::RunCtx& ctx)
        : imgListener{ ctx.programPtr } {}

        CodecCtx(const CodecCtx&) noexcept = delete;
        CodecCtx& operator=(const CodecCtx&) noexcept = delete;

        ~CodecCtx() {
            if (codec != nullptr) {
                ENABLE_IF_DEBUG(const auto stopResult = ) AMediaCodec_stop(codec.get());
                assert(stopResult == AMEDIA_OK);
            }
		}
    };
    using CodecCtxPtr = std::shared_ptr<CodecCtx>;
    CodecCtxPtr m_codecCtx{};

    MediaCodecDecoderPlugin(const IDecoderPlugin::RunCtx& ctx)
    : m_runCtx{ ctx } {
        assert(m_runCtx.programPtr != nullptr);
    }

    virtual ~MediaCodecDecoderPlugin() override {
        Log::Write(Log::Level::Info, "MediaCodecDecoderPlugin destroyed");
    }
    
	virtual inline bool QueuePacket
	(
        const IDecoderPlugin::PacketType& newPacketData,
		const std::uint64_t trackingFrameIndex
	) override
    {
        using namespace std::literals::chrono_literals;

        const auto selectedCodec = m_runCtx.config.codecType;
        const auto vpssps = find_vpssps(newPacketData, selectedCodec);

        if (m_codecCtx == nullptr && vpssps.size() > 0) {
            m_codecCtx = MakeCodecContext(vpssps);
        }
        if (m_codecCtx == nullptr)
			return false;

        std::int32_t inputBufferId = -1;
        if (!m_inputBufferQueue.wait_dequeue_timed(/*out*/ inputBufferId, 100ms) || inputBufferId < 0) {
            Log::Write(Log::Level::Warning, "Waiting for input buffer took too long, skipping this frame.");
            return false;
        }

        const auto packet_data = newPacketData.subspan(vpssps.size(), newPacketData.size() - vpssps.size());
        if (is_idr(packet_data, selectedCodec)) {
            if (const auto clientCtx = m_runCtx.clientCtx) {
                clientCtx->setWaitingNextIDR(false);
            }
        }

        const bool is_config_packet = is_config(packet_data, selectedCodec);
        if (!is_config_packet) {
            LatencyCollector::Instance().decoderInput(trackingFrameIndex);
        }

        const auto bufferId = static_cast<std::size_t>(inputBufferId);
        std::size_t inBuffSize = 0;
        const auto inputBuffer = AMediaCodec_getInputBuffer(m_codecCtx->codec.get(), bufferId, &inBuffSize);
        assert(packet_data.size() <= inBuffSize);
        const std::size_t size = std::min(inBuffSize, packet_data.size());
        std::memcpy(inputBuffer, packet_data.data(), size);

        const auto pts = is_config_packet ? 0 : MakePTS();
        const std::uint32_t flags = is_config_packet ? AMEDIACODEC_BUFFER_FLAG_CODEC_CONFIG : 0;
        if (!is_config_packet) {
            m_codecCtx->imgListener.frameIndexMap.set(pts, trackingFrameIndex);
        }

        const auto result = AMediaCodec_queueInputBuffer(m_codecCtx->codec.get(), bufferId, 0, size, pts, flags);
        if (result != AMEDIA_OK) {
            Log::Write(Log::Level::Warning, Fmt("AMediaCodec_queueInputBuffer, error-code %d: ", (int)result));
            return false;
        }
        return true;
	}

    void OnCodecError(AMediaCodec* /*codec*/, media_status_t error, std::int32_t actionCode, const char* details) {
        Log::Write(Log::Level::Error, Fmt("MediaCodec error: error-code: %d action-code: %d details: %s", error, actionCode, details));
    }

    void OnCodecFormatChanged(AMediaCodec* /*codec*/, AMediaFormat* outputFormat) {
        std::int32_t w = 0, h = 0;
        AMediaFormat_getInt32(outputFormat, AMEDIAFORMAT_KEY_WIDTH, &w);
        AMediaFormat_getInt32(outputFormat, AMEDIAFORMAT_KEY_HEIGHT, &h);
        std::int32_t colorStd = 0, colorRange = 0;
        AMediaFormat_getInt32(outputFormat, AMEDIAFORMAT_KEY_COLOR_STANDARD, &colorStd);
        AMediaFormat_getInt32(outputFormat, AMEDIAFORMAT_KEY_COLOR_RANGE, &colorRange);

        assert(w != 0 && h != 0);
        Log::Write(Log::Level::Info, Fmt("OUTPUT_FORMAT_CHANGED, w:%d, h:%d, color-std:%d, color-range:%d", w, h, colorStd, colorRange));
    }

    void OnCodecInputAvailable(AMediaCodec* /*codec*/, std::int32_t index) {
        assert(index >= 0);
        using namespace std::literals::chrono_literals;
        m_inputBufferQueue.wait_enqueue_timed(index, 50ms);
    }

    void OnCodecOutputAvailable(AMediaCodec* /*codec*/, std::int32_t index, AMediaCodecBufferInfo* bufferInfo) {
        assert(index >= 0);
        using namespace std::literals::chrono_literals;
        const OutputBufferId newOuputBuffer = {
			.presentationTimeUs = bufferInfo->presentationTimeUs,
			.bufferId = static_cast<std::size_t>(index)
		};
		m_outputBufferQueue.wait_enqueue_timed(newOuputBuffer, 50ms);
    }

    static std::uint64_t MakePTS() {
        using namespace std::chrono;
        using ClockType = XrSteadyClock;
        static_assert(ClockType::is_steady);
        using microseconds64 = duration<std::uint64_t, microseconds::period>;
        return duration_cast<microseconds64>(ClockType::now().time_since_epoch()).count();
    }

    struct AMediaFormatDeleter final {
        void operator()(AMediaFormat* fmt) const {
            if (fmt == nullptr)
                return;
            ENABLE_IF_DEBUG(const auto deleteResult =) AMediaFormat_delete(fmt);
            assert(deleteResult == AMEDIA_OK);
        }
    };
    using AMediaFormatPtr = std::unique_ptr<AMediaFormat, AMediaFormatDeleter>;
    inline AMediaFormatPtr MakeMediaFormat
    (
        const char* const mimeType,
        const OptionMap& optionMap,
        const IDecoderPlugin::PacketType& csd0,
        const bool realtimePriority = true
    )
    {
        AMediaFormat* format = AMediaFormat_new();
        if (format == nullptr)
            return nullptr;

        AMediaFormat_setString(format, AMEDIAFORMAT_KEY_MIME, mimeType);
        AMediaFormat_setInt32(format, AMEDIAFORMAT_KEY_WIDTH, 512);
        AMediaFormat_setInt32(format, AMEDIAFORMAT_KEY_HEIGHT, 1024);

        for (const auto& [key, val] : optionMap.string_map())
            AMediaFormat_setString(format, key.c_str(), val.c_str());
        for (const auto& [key, val] : optionMap.float_map())
            AMediaFormat_setFloat(format, key.c_str(), val);
        for (const auto& [key, val] : optionMap.int64_map())
            AMediaFormat_setInt64(format, key.c_str(), val);
        for (const auto& [key, val] : optionMap.int32_map())
            AMediaFormat_setInt32(format, key.c_str(), val);

        AMediaFormat_setInt32(format, AMEDIAFORMAT_KEY_OPERATING_RATE, std::numeric_limits<std::int16_t>::max());
        AMediaFormat_setInt32(format, AMEDIAFORMAT_KEY_PRIORITY, realtimePriority ? 0 : 1);

#if defined(__ANDROID_API__) && (__ANDROID_API__ >= 30)
#pragma message ("Setting android 11(+) LOW_LATENCY key enabled.")
        AMediaFormat_setInt32(format, AMEDIAFORMAT_KEY_LOW_LATENCY, 1);
#endif
        assert(csd0.size() > 0);
        AMediaFormat_setBuffer(format, AMEDIAFORMAT_KEY_CSD_0, csd0.data(), csd0.size());

        return AMediaFormatPtr { format };
    }

    AMediaCodecPtr MakeStartedCodec(
        const IDecoderPlugin::PacketType& csd0,
        ANativeWindow* const surface_handle
    ) {
        if (surface_handle == nullptr)
            return {};

        Log::Write(Log::Level::Info, "Spawning decoder...");
        const char* const mimeType = m_runCtx.config.codecType == ALXRCodecType::HEVC_CODEC ? "video/hevc" : "video/avc";
        AMediaCodecPtr codec{ AMediaCodec_createDecoderByType(mimeType) };
        if (codec == nullptr)
        {
            Log::Write(Log::Level::Error, "AMediaCodec_createDecoderByType failed!");
            return {};
        }
        assert(codec != nullptr);

        char* codecName = nullptr;
        if (AMediaCodec_getName(codec.get(), &codecName) == AMEDIA_OK) {
            Log::Write(Log::Level::Info, Fmt("Selected decoder: %s", codecName));
            AMediaCodec_releaseName(codec.get(), codecName);
        }

        AMediaFormatPtr format = MakeMediaFormat(mimeType, m_runCtx.optionMap, csd0, m_runCtx.config.realtimePriority);
        if (format == nullptr) {
			Log::Write(Log::Level::Error, "Failed to create media format.");
			return {};
		}

        using ThisType = MediaCodecDecoderPlugin;
        AMediaCodecOnAsyncNotifyCallback asyncCallbacks = {
            .onAsyncInputAvailable = [](AMediaCodec* codec, void* userdata, std::int32_t index) {
                reinterpret_cast<ThisType*>(userdata)->OnCodecInputAvailable(codec, index);
            },
            .onAsyncOutputAvailable = [](AMediaCodec* codec, void* userdata, std::int32_t index, AMediaCodecBufferInfo* bufferInfo) {
                reinterpret_cast<ThisType*>(userdata)->OnCodecOutputAvailable(codec, index, bufferInfo);
			},
            .onAsyncFormatChanged = [](AMediaCodec* codec, void* userdata, AMediaFormat* format) {
                reinterpret_cast<ThisType*>(userdata)->OnCodecFormatChanged(codec, format);
            },
            .onAsyncError = [](AMediaCodec* codec, void* userdata, media_status_t error, std::int32_t actionCode, const char* detail) {
                reinterpret_cast<ThisType*>(userdata)->OnCodecError(codec, error, actionCode, detail);
            },
        };
        auto status = AMediaCodec_setAsyncNotifyCallback(codec.get(), asyncCallbacks, this);
        if (status != AMEDIA_OK) {
            Log::Write(Log::Level::Error, Fmt("AMediaCodec_setAsyncNotifyCallback faild, code: %ld", status));
            return {};
        }

        status = AMediaCodec_configure(codec.get(), format.get(), surface_handle, nullptr, 0);
        if (status != AMEDIA_OK) {
            Log::Write(Log::Level::Error, Fmt("Failed to configure codec, code: %ld", status));
            return {};
        }

        status = AMediaCodec_start(codec.get());
        if (status != AMEDIA_OK) {
            Log::Write(Log::Level::Error, Fmt("Failed to start codec, code: %ld", status));
            return {};
        }
        Log::Write(Log::Level::Info, "Finished constructing and starting decoder...");
        return codec;
    }

    CodecCtxPtr MakeCodecContext(const IDecoderPlugin::PacketType& csd0) {
        auto newCodecCtx = std::make_shared<CodecCtx>(m_runCtx);
        if (!newCodecCtx->imgListener.IsValid())
            return {};
        const auto surfaceHandle = newCodecCtx->imgListener.GetWindow();
        if (surfaceHandle == nullptr) {
            Log::Write(Log::Level::Error, "Failed to get window surface handle.");
			return {};
        }
        auto codecPtr = MakeStartedCodec(csd0, surfaceHandle);
        if (codecPtr == nullptr)
			return {};
        newCodecCtx->codec = std::move(codecPtr);
        assert(newCodecCtx->codec != nullptr);
        if (const auto clientCtx = m_runCtx.clientCtx) {
            if (const auto programPtr = m_runCtx.programPtr)
                programPtr->SetRenderMode(IOpenXrProgram::RenderMode::VideoStream);
        }
        Log::Write(Log::Level::Info, "Finished Creating CodecContext");
        return newCodecCtx;
    }

    CodecCtxPtr waitForCodecCtx(shared_bool& isRunningToken) const {
        using namespace std::literals::chrono_literals;
        while (isRunningToken) {
            if (auto codecCtxPtr = m_codecCtx) {
                return codecCtxPtr;
            }
            std::this_thread::sleep_for(100ms);
        }
        return {};
    }

    virtual inline bool Run(IDecoderPlugin::shared_bool& isRunningToken) override
    {
        using namespace std::literals::chrono_literals;
        if (!isRunningToken || m_runCtx.programPtr == nullptr) {
            Log::Write(Log::Level::Error, "Decoder run parameters not valid.");
            return false;
        }

        const auto codecCtx = waitForCodecCtx(isRunningToken);
		
        while (isRunningToken) {
            OutputBufferId buffInfo{};
            if (!m_outputBufferQueue.wait_dequeue_timed(/*out*/ buffInfo, 100ms)) {
                Log::Write(Log::Level::Warning, "Waiting for decoder output buffer took longer than 100ms, attempting to re-try.");
                continue;
            }
            const auto ptsUs = static_cast<std::uint64_t>(buffInfo.presentationTimeUs);
            const auto frameIndex = codecCtx->imgListener.frameIndexMap.get(ptsUs);
            if (frameIndex != FrameIndexMap::NullIndex) {
                LatencyCollector::Instance().decoderOutput(frameIndex);
            }
            AMediaCodec_releaseOutputBuffer(codecCtx->codec.get(), buffInfo.bufferId, true);
        }
        Log::Write(Log::Level::Info, "Decoder thread exiting...");
        return true;
    }
};
}

std::shared_ptr<IDecoderPlugin> CreateDecoderPlugin_MediaCodec(const IDecoderPlugin::RunCtx& ctx) {
    return std::make_shared<MediaCodecDecoderPlugin>(ctx);
}
#endif
