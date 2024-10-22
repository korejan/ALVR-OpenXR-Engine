#if !defined(XR_USE_PLATFORM_ANDROID)  && !defined(XR_DISABLE_DECODER_THREAD)

#include "pch.h"
#include "common.h"
#include "decoderplugin.h"

#include <cstring>
#include <functional>
#include <memory>
#include <atomic>
#include <chrono>
#include <mutex>

#include <readerwritercircularbuffer.h>

extern "C" {
#include <libavutil/log.h>
#include <libavutil/avutil.h>
#include <libavutil/opt.h>
#include <libavutil/pixfmt.h>
#include <libavutil/pixdesc.h>
//#include <libavdevice/avdevice.h> // TODO: CHECK!
#include <libavcodec/avcodec.h>
// #include <libavformat/avformat.h> TODO: CHECK!
#include <libswscale/swscale.h>
#include <libavutil/hwcontext.h>
#ifdef XR_USE_PLATFORM_WIN32
#include <libavutil/hwcontext_d3d11va.h>
#endif
#include <libavutil/imgutils.h>
#ifdef XR_USE_PLATFORM_ANDROID
#include <libavcodec/jni.h>
#include <libavcodec/mediacodec.h>
#endif
}

#include "alxr_ctypes.h"
#include "nal_utils.h"
#include "graphicsplugin.h"
#include "openxr_program.h"
#include "latency_manager.h"
#include "timing.h"

namespace {;
template < typename AVType, void(&avdeleter)(AVType*) >
struct AVDeleter1 {
    inline void operator()(AVType* avc) const
    {
        if (avc)
            avdeleter(avc);
    }
};

template < typename AVType, void(&avdeleter)(AVType**) >
struct AVDeleter2 {
    inline void operator()(AVType* avc) const
    {
        if (avc)
            avdeleter(&avc);
    }
};

template < typename AVType, void(&avdeleter)(AVType*)>
using make_av_ptr_type1 = std::unique_ptr<
    AVType,
    AVDeleter1<AVType, avdeleter>
>;

template < typename AVType, void(&avdeleter)(AVType**)>
using make_av_ptr_type2 = std::unique_ptr<
    AVType,
    AVDeleter2<AVType, avdeleter>
>;

struct AVPacketDeleter{
    inline void operator()(AVPacket* avc) const {
        if (avc) {
            av_packet_free(&avc);
        }
    }
};
using AVPacketPtr = std::unique_ptr<AVPacket, AVPacketDeleter>;
struct NALPacket
{
    AVPacketPtr data;
    std::uint64_t frameIndex;

    /*constexpr*/ inline NALPacket(AVPacket* p = nullptr, const std::uint64_t fi = std::uint64_t(-1)) noexcept
        : data(p), frameIndex(fi) {}
    /*constexpr*/ inline NALPacket(NALPacket&&) noexcept = default;
    /*constexpr*/ inline NALPacket& operator=(NALPacket&&) noexcept = default;

    constexpr inline NALPacket(const NALPacket&) noexcept = delete;
    constexpr inline NALPacket& operator=(const NALPacket&) noexcept = delete;
};

inline auto AverrorToCodeStr(const int errnum)
{
    thread_local char buf[AV_ERROR_MAX_STRING_SIZE];
    return av_make_error_string(buf, AV_ERROR_MAX_STRING_SIZE, errnum);
}

inline void LogLibAV(const Log::Level lvl, const int errnum, const char* msg = nullptr)
{
    const char* errMsg = AverrorToCodeStr(errnum);
    if (errMsg == nullptr)
        errMsg = "Uknown reason";
    Log::Write(lvl, Fmt("%s, error-id:%d reason: %s", msg, errnum, errMsg));
}

constexpr inline AVCodecID ToAVCodecID(const ALXRCodecType ct)
{
    switch (ct)
    {
    case ALXRCodecType::H264_CODEC: return AV_CODEC_ID_H264;
    case ALXRCodecType::HEVC_CODEC: return AV_CODEC_ID_HEVC;
    default: return AV_CODEC_ID_NONE;
    }
}

constexpr inline const char* CuvidDecoderName(const ALXRCodecType ct) {
    switch (ct)
    {
    case ALXRCodecType::H264_CODEC: return "h264_cuvid";
    case ALXRCodecType::HEVC_CODEC: return "hevc_cuvid";
    default: return "";
    }
}

constexpr inline const char* ToString(const ALXRDecoderType dtype)
{
    switch (dtype)
    {
    case ALXRDecoderType::NVDEC:  return "NVDEC";
    case ALXRDecoderType::CUVID:  return "CUVID";
    case ALXRDecoderType::D311VA: return "D3D11VA";
    case ALXRDecoderType::VAAPI: return "VAAPI";
    case ALXRDecoderType::CPU: return "CPU";
    default: return "Unknown";
    }
}

constexpr inline AVHWDeviceType ToAVHWDeviceType(const ALXRDecoderType dtype)
{
    switch (dtype)
    {
    case ALXRDecoderType::NVDEC:  return AV_HWDEVICE_TYPE_CUDA;
    case ALXRDecoderType::CUVID:  return AV_HWDEVICE_TYPE_CUDA;
    case ALXRDecoderType::D311VA: return AV_HWDEVICE_TYPE_D3D11VA;
    case ALXRDecoderType::VAAPI: return AV_HWDEVICE_TYPE_VAAPI;
    default: return AV_HWDEVICE_TYPE_NONE;
    }
}

constexpr inline XrPixelFormat ToXrPixelFormat(const AVPixelFormat pixFmt)
{
    switch (pixFmt)
    {
    case AV_PIX_FMT_NV12:
    case AV_PIX_FMT_YUV420P:
        return XrPixelFormat::NV12;
    case AV_PIX_FMT_P010LE:
    case AV_PIX_FMT_YUV420P10LE:
        return XrPixelFormat::P010LE;
    default: return XrPixelFormat::Uknown;
    }
}

constexpr inline XrPixelFormat ToXrPixel3PlaneFormat(const AVPixelFormat pixFmt)
{
    switch (pixFmt)
    {
    case AV_PIX_FMT_NV12:
    case AV_PIX_FMT_YUV420P:
        return XrPixelFormat::G8_B8_R8_3PLANE_420;
    case AV_PIX_FMT_P010LE:
    case AV_PIX_FMT_YUV420P10LE:
        return XrPixelFormat::G10X6_B10X6_R10X6_3PLANE_420;
    default: return XrPixelFormat::Uknown;
    }
}

constexpr inline std::size_t PlaneCount(const AVFrame& frame)
{
    for (std::size_t planeCount = 0; planeCount < AV_NUM_DATA_POINTERS; ++planeCount) {
        if (frame.data[planeCount] == nullptr)
            return planeCount;
    }
    return 0;
}

constexpr inline XrPixelFormat GetXrPixelFormat(const AVFrame& frame, const AVCodecContext& codecCtx)
{
    if (frame.format < 0)
        return XrPixelFormat::Uknown;
    const auto frameFmt = static_cast<AVPixelFormat>(frame.format);
    switch (frameFmt) {
    case AV_PIX_FMT_CUDA:
    case AV_PIX_FMT_D3D11:
    case AV_PIX_FMT_D3D11VA_VLD:
    case AV_PIX_FMT_VAAPI:
        //case AV_PIX_FMT_VULKAN:
        //case AV_PIX_FMT_QSV:
        assert(PlaneCount(frame) < 3);
        return ToXrPixelFormat(codecCtx.sw_pix_fmt);
    default:
        switch (PlaneCount(frame)) {
        case 2:  return ToXrPixelFormat(frameFmt);
        case 3:  return ToXrPixel3PlaneFormat(frameFmt);
        default: return XrPixelFormat::Uknown;
        }

    }
}

inline bool IsApiVulkan(const IGraphicsPlugin& gp) {
    const auto gpType = gp.GetGraphicsBinding()->type;
    return gpType == XR_TYPE_GRAPHICS_BINDING_VULKAN2_KHR || gpType == XR_TYPE_GRAPHICS_BINDING_VULKAN_KHR;
}

constexpr inline auto GetVideoTextureMemFuns(const IGraphicsPlugin& gp, const ALXRDecoderType decoderType)
{
    using CreateFnType = decltype(&IGraphicsPlugin::CreateVideoTextures);
    using UpdateFnType = decltype(&IGraphicsPlugin::UpdateVideoTexture);
    using RetType = std::tuple<const CreateFnType, const UpdateFnType, const bool>;
    switch (decoderType)
    {
    case ALXRDecoderType::CUVID:
    case ALXRDecoderType::NVDEC:
        return RetType(&IGraphicsPlugin::CreateVideoTexturesCUDA, &IGraphicsPlugin::UpdateVideoTextureCUDA, true);
    // case ALXRDecoderType::VAAPI:
    //     return RetType(&IGraphicsPlugin::CreateVideoTexturesVAAPI, &IGraphicsPlugin::UpdateVideoTextureVAAPI);
    case ALXRDecoderType::D311VA:
        // D3D11VA-vulkan buffer interop not working.
        if (!IsApiVulkan(gp)) {
            return RetType(&IGraphicsPlugin::CreateVideoTexturesD3D11VA, &IGraphicsPlugin::UpdateVideoTextureD3D11VA, true);
        } // if vulkan, fall through to default.
        [[fallthrough]];
    default: return RetType(&IGraphicsPlugin::CreateVideoTextures, &IGraphicsPlugin::UpdateVideoTexture, false);
    }
}

struct FFMPEGDecoderPlugin final : public IDecoderPlugin {
    
    using AVPacketQueue = moodycamel::BlockingReaderWriterCircularBuffer<NALPacket>;
    using GraphicsPluginPtr = std::shared_ptr<IGraphicsPlugin>;
    using IOpenXrProgramPtr = std::shared_ptr<IOpenXrProgram>;
    using ALXRClientCtxPtr  = std::shared_ptr<const ALXRClientCtx>;

    IDecoderPlugin::RunCtx m_ctx;

    AVPacketQueue/*Ptr*/ m_avPacketQueue;
    AVPixelFormat        m_hwPixFmt = AV_PIX_FMT_NONE;
    
    virtual ~FFMPEGDecoderPlugin() override {}

    FFMPEGDecoderPlugin(const IDecoderPlugin::RunCtx& ctx)
    : m_ctx{ ctx }
    , m_avPacketQueue{ 360 }
    {
        static std::once_flag reg_devices_once{};
        std::call_once(reg_devices_once, []()
        {
#if 0 // TODO CHECK!
            Log::Write(Log::Level::Verbose, "Registering all libav devices.");
            avdevice_register_all();
#endif
#ifndef NDEBUG
            av_log_set_flags(AV_LOG_PRINT_LEVEL);
            av_log_set_level(AV_LOG_VERBOSE);
#endif
        });
    }
    
    virtual bool QueuePacket
    (
        const IDecoderPlugin::PacketType& newPacketData,
        const std::uint64_t trackingFrameIndex
    ) override
    {
        if (const auto pkt = av_packet_alloc()) {
            const std::size_t packetSize = newPacketData.size();
            if (const auto pktBuffer = static_cast<std::uint8_t*>(av_malloc(packetSize))) {
                std::memcpy(pktBuffer, newPacketData.data(), packetSize);
                if (av_packet_from_data(pkt, pktBuffer, static_cast<int>(packetSize)) == 0) {
                    using namespace std::literals::chrono_literals;
                    constexpr static const auto QueueWaitTimeout = 500ms;
                    m_avPacketQueue.wait_enqueue_timed({ pkt, trackingFrameIndex }, QueueWaitTimeout);
                } else av_free(pktBuffer);
            }
        }
        return true;
    }

    virtual bool Run(IDecoderPlugin::shared_bool& isRunningToken) override
    {
        using AVCodecContextPtr = make_av_ptr_type2<AVCodecContext, avcodec_free_context>;
        using AVFramePtr = make_av_ptr_type2<AVFrame, av_frame_free>;
        using AVBufferRefPtr = make_av_ptr_type2<AVBufferRef, av_buffer_unref>;

        if (!isRunningToken || m_ctx.programPtr == nullptr) {
            Log::Write(Log::Level::Warning, "Decoder run parameters not valid.");
            return false;
        }

        const auto graphicsPluginPtr = [&]() -> GraphicsPluginPtr
        {
            if (const auto programPtr = m_ctx.programPtr)
                return programPtr->GetGraphicsPlugin();
            return nullptr;
        }();
        if (graphicsPluginPtr == nullptr) {
            Log::Write(Log::Level::Error, "Failed to get graphics plugin ptr.");
            return false;
        }

        const auto type = ToAVHWDeviceType(m_ctx.decoderType);
        if (type == AV_HWDEVICE_TYPE_NONE) {
            Log::Write(Log::Level::Info, "No hw-accelerated device selected, falling back to sw-decoder");
        }

        const auto hwdeviceName = type == AV_HWDEVICE_TYPE_NONE ? "none" : av_hwdevice_get_type_name(type);
        const auto codecPtr = [&]()
        {
            //const auto decodeName = "hevc_mediacodec"; // "hevc_nvdec"; //"hevc_cuvid"; // hevc_cuvid";//"hevc_mediacodec";
            if (m_ctx.decoderType == ALXRDecoderType::CUVID)
                return avcodec_find_decoder_by_name(CuvidDecoderName(m_ctx.config.codecType));
            return avcodec_find_decoder(ToAVCodecID(m_ctx.config.codecType)); //avcodec_find_decoder_by_name(decodeName);
        }();
        if (codecPtr == nullptr) {
            Log::Write(Log::Level::Error, "Failed to find decoder.");
            return false;
        }

        Log::Write(Log::Level::Info, Fmt("Selected decoder: %s / hw-device: %s", ToString(m_ctx.decoderType), hwdeviceName));
        Log::Write(Log::Level::Info, Fmt("Selected codec: %s", codecPtr->name));

        m_hwPixFmt = AV_PIX_FMT_NONE;
        if (type != AV_HWDEVICE_TYPE_NONE) {
            for (int i = 0;; ++i) {
                const AVCodecHWConfig* config = avcodec_get_hw_config(codecPtr, i);
                if (!config) {
                    Log::Write(Log::Level::Error, Fmt("Decoder %s does not support device type %s.\n", codecPtr->name, av_hwdevice_get_type_name(type)));
                    return false;
                }
                Log::Write(Log::Level::Verbose,
                    Fmt("config, type %d with methods %d (AdHOC | HW_DEV: %d), pix fmt %d (mediacodec is: %d)",
                        config->device_type, (AV_CODEC_HW_CONFIG_METHOD_AD_HOC | AV_CODEC_HW_CONFIG_METHOD_HW_DEVICE_CTX), config->methods, config->pix_fmt, AVPixelFormat::AV_PIX_FMT_MEDIACODEC));
                if (config->methods & (AV_CODEC_HW_CONFIG_METHOD_HW_DEVICE_CTX | AV_CODEC_HW_CONFIG_METHOD_HW_FRAMES_CTX) && //config->methods & AV_CODEC_HW_CONFIG_METHOD_HW_DEVICE_CTX &&
                    config->device_type == type) {
                    //hwconfig = config;
                    m_hwPixFmt = config->pix_fmt;
                    Log::Write(Log::Level::Verbose, Fmt("HWConfig found, type-id:%d method-id: %d, pixfmt-id: %d", type, config->methods, m_hwPixFmt));
                    break;
                }
            }
        }

        const AVCodecContextPtr codecCtx{ avcodec_alloc_context3(codecPtr) };
        if (codecCtx == nullptr) {
            Log::Write(Log::Level::Error, "Failed to create code context.");
            return false;
        }
        CHECK(codecCtx->opaque == nullptr);
        codecCtx->opaque = this;
        const char* const tuneParamStr = m_ctx.config.codecType == ALXRCodecType::HEVC_CODEC ?
            "zerolatency" : "fastdecode,zerolatency";
        av_opt_set(codecCtx->priv_data, "preset", "ultrafast", 0);
        av_opt_set(codecCtx->priv_data, "tune", tuneParamStr, 0);

        if (type != AV_HWDEVICE_TYPE_NONE) {
            codecCtx->get_format = get_hw_format;
            codecCtx->thread_count = 1;
        }
        else {
            codecCtx->thread_count = std::max(1u, m_ctx.config.cpuThreadCount);
        }
        Log::Write(Log::Level::Info, Fmt("Decoder thread count: %d", codecCtx->thread_count));

        AVBufferRefPtr hw_device_ctx{ nullptr };
#ifdef XR_USE_PLATFORM_WIN32
        if (AV_HWDEVICE_TYPE_D3D11VA == type)
        {
            Log::Write(Log::Level::Verbose, "Init AV_HWDEVICE_TYPE_D3D11VA");
            hw_device_ctx.reset(av_hwdevice_ctx_alloc(type));
            if (hw_device_ctx == nullptr) {
                Log::Write(Log::Level::Error, "Failed to create specified HW device.\n");
                return false;
            }
            auto device_context = (AVHWDeviceContext*)hw_device_ctx->data;
            auto d3d11_device_context = (AVD3D11VADeviceContext*)device_context->hwctx;
            d3d11_device_context->device = (ID3D11Device*)graphicsPluginPtr->GetD3D11AVDevice();
            if (d3d11_device_context->device) {
                d3d11_device_context->device->AddRef();
                d3d11_device_context->device_context = (ID3D11DeviceContext*)graphicsPluginPtr->GetD3D11VADeviceContext();
                if (d3d11_device_context->device_context)
                    d3d11_device_context->device_context->AddRef();
            }
            av_hwdevice_ctx_init(hw_device_ctx.get());
            codecCtx->hw_device_ctx = av_buffer_ref(hw_device_ctx.get());
            //codecCtx->opaque = hw_device_ctx.get();
        }
        else
#endif
        if (type != AV_HWDEVICE_TYPE_NONE)
        {
            AVBufferRef* device_ctx = nullptr;
            int err = 0;
            if ((err = av_hwdevice_ctx_create(&device_ctx, type, nullptr, nullptr, 0)) < 0) {
                Log::Write(Log::Level::Error, "Failed to create specified HW device.\n");
                return false;
            }
            codecCtx->hw_device_ctx = av_buffer_ref(device_ctx);
            hw_device_ctx.reset(device_ctx);
        }

        if (avcodec_open2(codecCtx.get(), codecPtr, nullptr) < 0) {
            Log::Write(Log::Level::Error, "Failed to open decodor.");
            return false;
        }

        const AVFramePtr swFrame{ av_frame_alloc() };
        const AVFramePtr hwFrame{ av_frame_alloc() };
        if (swFrame == nullptr || hwFrame == nullptr) {
            Log::Write(Log::Level::Error, "Failed to allocate avFrames.");
            return false;
        }

        const auto [CreateVideoTextures, UpdateVideoTextures, isBufferInteropSupported] = GetVideoTextureMemFuns(*graphicsPluginPtr, m_ctx.decoderType);
        assert(CreateVideoTextures != nullptr && UpdateVideoTextures != nullptr);
                
        using namespace std::literals::chrono_literals;
        static constexpr const auto QueueWaitTimeout = 500ms;
        std::size_t planeCount = 0;
        std::once_flag once_flag{};
        while (isRunningToken)
        {
            NALPacket nalPacket{};
            if (!m_avPacketQueue.wait_dequeue_timed(nalPacket, QueueWaitTimeout))
                continue;

            assert(nalPacket.data != nullptr);
            const auto& pkt = nalPacket.data;

            using namespace std::chrono;
            using ClockType = XrSteadyClock;
            static_assert(ClockType::is_steady);
            using microseconds64 = duration<std::uint64_t, std::chrono::seconds::period>;
            pkt->pts = duration_cast<microseconds64>(ClockType::now().time_since_epoch()).count();

            LatencyCollector::Instance().decoderInput(nalPacket.frameIndex);
            const auto result = decode_packet(pkt.get(), codecCtx.get(), hwFrame.get());
            LatencyCollector::Instance().decoderOutput(nalPacket.frameIndex);
            //av_packet_unref(pkt.get());
            if (result < 0)
            {
                LogLibAV(Log::Level::Warning, result, "Failed to decode packet");
                continue;
            }

            const auto& avFrame = [&/*, isBTS = isBufferInteropSupported*/]() -> const AVFramePtr& {
                if (isBufferInteropSupported || type == AV_HWDEVICE_TYPE_NONE)
                    return hwFrame;
                CHECK(hwFrame->format == m_hwPixFmt);
                CHECK((av_hwframe_transfer_data(swFrame.get(), hwFrame.get(), 0) == 0));
                return swFrame;
            }();
            assert(avFrame != nullptr);

            std::call_once(once_flag, [&/*, cvt = CreateVideoTextures*/]()
            {
                Log::Write(Log::Level::Verbose, Fmt("Creating video textures, width=%d, height=%d, pitch-0=%d, pitch-1=%d, type=%d sw-type=%d",
                    avFrame->width, avFrame->height, avFrame->linesize[0], avFrame->linesize[1], avFrame->format, codecCtx->sw_pix_fmt));
                const auto pixFmt = GetXrPixelFormat(*avFrame, *codecCtx);
                CHECK(pixFmt != XrPixelFormat::Uknown);
                planeCount = PlaneCount(pixFmt);
                assert(planeCount > 0);
                Log::Write(Log::Level::Verbose, Fmt("Pixel Format: %lu", pixFmt));
                std::invoke(CreateVideoTextures, graphicsPluginPtr, avFrame->width, avFrame->height, pixFmt);

                if (const auto clientCtx = m_ctx.clientCtx) {
                    clientCtx->setWaitingNextIDR(false);
                    if (const auto programPtr = m_ctx.programPtr) {
                        programPtr->SetRenderMode(IOpenXrProgram::RenderMode::VideoStream);
                    }
                }
            });

            const std::size_t uvHeight = static_cast<std::size_t>(avFrame->height / 2);
            IGraphicsPlugin::YUVBuffer buffer{
                .luma {
                    .data = avFrame->data[0],
                    .pitch = static_cast<std::size_t>(avFrame->linesize[0]),
                    .height = static_cast<std::size_t>(avFrame->height)
                },
                .chroma {
                    .data = avFrame->data[1],
                    .pitch = static_cast<std::size_t>(avFrame->linesize[1]),
                    .height = uvHeight
                },
                .frameIndex = nalPacket.frameIndex
            };
            if (planeCount > 2) {
                buffer.chroma2 = {
                    .data = avFrame->data[2],
                    .pitch = static_cast<std::size_t>(avFrame->linesize[2]),
                    .height = uvHeight
                };
            }
            std::invoke(UpdateVideoTextures, graphicsPluginPtr, buffer);
        }
        return true;
    }

#if 1
    inline int decode_packet(AVPacket* pPacket, AVCodecContext* pCodecContext, AVFrame* hwFrame)
    {
        int response = avcodec_send_packet(pCodecContext, pPacket);
        if (response < 0)
            return response;

        while (response >= 0)
        {
            response = avcodec_receive_frame(pCodecContext, hwFrame);
            if (response == AVERROR(EAGAIN) || response == AVERROR_EOF) {
                break;
            }
            else if (response < 0) {
                return response;
            }
            if (response >= 0)
            {
#if 0
                //pFrame = hwFrame;
                size_t planeCount = 0;
                for (; planeCount < AV_NUM_DATA_POINTERS; ++planeCount) {
                    if (hwFrame->data[planeCount] == nullptr)
                        break;
                }
                Log::Write(Log::Level::Info, Fmt(
                    "Frame %d (type=%c, size=%d bytes, format=%d, planes=%zu, pitch1=%d,pitch2=%d width=%d, height=%d) pts %d key_frame %d [DTS %d]",
                    pCodecContext->frame_number,
                    av_get_picture_type_char(hwFrame->pict_type),
                    hwFrame->pkt_size,
                    pCodecContext->sw_pix_fmt, //pFrame->format,
                    planeCount,
                    hwFrame->linesize[0], hwFrame->linesize[1],
                    hwFrame->width,
                    hwFrame->height,
                    hwFrame->pts,
                    hwFrame->key_frame,
                    hwFrame->coded_picture_number
                ));
#endif
                return 0;
            }
        }
        return -1;
    }
#else
    int decode_packet(AVPacket* packet, AVCodecContext* avctx, AVFrame* sw_frame, AVFrame* frame)
    {
        int ret = avcodec_send_packet(avctx, packet);
        if (ret < 0) {
            //fprintf(stderr, "Error during decoding\n");
            return ret;
        }

        while (true) {
            //if (!(frame = av_frame_alloc()) || !(sw_frame = av_frame_alloc())) {
            //    fprintf(stderr, "Can not alloc frame\n");
            //    ret = AVERROR(ENOMEM);
            //    goto fail;
            //}

            ret = avcodec_receive_frame(avctx, frame);
            if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) {
                //av_frame_free(&frame);
                //av_frame_free(&sw_frame);
                return ret;
            }
            else if (ret < 0) {
                //fprintf(stderr, "Error while decoding\n");
                goto fail;
            }

            if (frame->format == m_hwPixFmt) {
                /* retrieve data from GPU to CPU */
                if ((ret = av_hwframe_transfer_data(sw_frame, frame, 0)) < 0) {
                    //fprintf(stderr, "Error transferring the data to system memory\n");
                    goto fail;
                }
                //tmp_frame = sw_frame;
            }
            else
                //tmp_frame = frame;

            //size = av_image_get_buffer_size(tmp_frame->format, tmp_frame->width,
            //    tmp_frame->height, 1);
            //buffer = av_malloc(size);
            //if (!buffer) {
            //    fprintf(stderr, "Can not alloc buffer\n");
            //    ret = AVERROR(ENOMEM);
            //    goto fail;
            //}
            //ret = av_image_copy_to_buffer(buffer, size,
            //    (const uint8_t* const*)tmp_frame->data,
            //    (const int*)tmp_frame->linesize, tmp_frame->format,
            //    tmp_frame->width, tmp_frame->height, 1);
            //if (ret < 0) {
            //    fprintf(stderr, "Can not copy image to buffer\n");
            //    goto fail;
            //}

            //if ((ret = fwrite(buffer, 1, size, output_file)) < 0) {
            //    fprintf(stderr, "Failed to dump raw data.\n");
            //    goto fail;
            //}

            fail:
            //av_frame_free(&frame);
            //av_frame_free(&sw_frame);
            //av_freep(&buffer);
            if (ret < 0)
                return ret;
        }
    }
#endif

    static AVPixelFormat get_hw_format(AVCodecContext* avctx, const AVPixelFormat* pix_fmts)
    {
        if (pix_fmts == nullptr || avctx == nullptr) {
            Log::Write(Log::Level::Error, "Failed to get HW surface format.\n");
            return AV_PIX_FMT_NONE;
        }

        const auto this_ = reinterpret_cast<const FFMPEGDecoderPlugin*>(avctx->opaque);
        if (this_ == nullptr) {
            Log::Write(Log::Level::Error, "Context for AVCodecContext::opque has not been set!");
            return AV_PIX_FMT_NONE;
        }

        for (const AVPixelFormat* p = pix_fmts; *p != -1; ++p) {
            if (*p == this_->m_hwPixFmt)
                return *p;
        }
        Log::Write(Log::Level::Error, "Failed to get HW surface format.\n");
        return AV_PIX_FMT_NONE;
    }
};
}

std::shared_ptr<IDecoderPlugin> CreateDecoderPlugin_FFMPEG(const IDecoderPlugin::RunCtx& ctx) {
    return std::make_shared<FFMPEGDecoderPlugin>(ctx);
}

#endif
