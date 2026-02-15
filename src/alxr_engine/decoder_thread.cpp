#include "decoder_thread.h"
#include "logger.h"
#include "decoderplugin.h"
#include "latency_manager.h"

bool XrDecoderThread::QueuePacket(const VideoFrame& header, const XrDecoderThread::VideoPacket& packet)
{
	const auto decoderPlugin = m_decoderPlugin;
	if (decoderPlugin == nullptr)
		return false;
	LatencyManager::Instance().OnPreVideoPacketRecieved(header);

	bool fecFailure = false, isComplete = true;
	if (const auto fecQueue = m_fecQueue) {
		fecQueue->addVideoPacket(header, packet, fecFailure);
		if (isComplete = fecQueue->reconstruct()) {
			const size_t frameBufferSize = fecQueue->getFrameByteSize();
			const auto frameBufferPtr = fecQueue->getFrameBuffer();
			decoderPlugin->QueuePacket({ frameBufferPtr, frameBufferSize }, header.trackingFrameIndex);
			fecQueue->clearFecFailure();
		}
	} else { // then FEC is disabled
		decoderPlugin->QueuePacket(packet, header.trackingFrameIndex);
	}

	LatencyManager::Instance().OnPostVideoPacketRecieved(header, { isComplete, fecFailure });
	return true;
}

bool XrDecoderThread::QueuePacket(const VideoFrame& header, const std::size_t packetSize)
{
	assert(packetSize >= sizeof(VideoFrame));
	const size_t frameBufferSize = packetSize - sizeof(VideoFrame);
	const auto frameBufferPtr = reinterpret_cast<const std::uint8_t*>(&header) + sizeof(VideoFrame);
	return QueuePacket(header, { frameBufferPtr, frameBufferSize });
}

void XrDecoderThread::Stop()
{
	Log::Write(Log::Level::Info, "shutting down decoder thread");
	m_isRuningToken = false;
	if (m_decoderThread.joinable()) {
		Log::Write(Log::Level::Info, "Waiting for decoder thread to shutdown...");
		m_decoderThread.join();
	}
	m_fecQueue.reset();

	Log::Write(Log::Level::Info, "m_decoderPlugin destroying");
	m_decoderPlugin.reset();
	Log::Write(Log::Level::Info, "m_decoderPlugin destroyed");
	
	Log::Write(Log::Level::Info, "Decoder thread finished shutdown");
}

void XrDecoderThread::Start(const XrDecoderThread::StartCtx& ctx)
{
	if (m_isRuningToken)
		return;

	Log::Write(Log::Level::Info, "Starting decoder thread.");
	m_fecQueue = ctx.decoderConfig.enableFEC ?
		std::make_shared<FECQueue>() : nullptr;

#ifdef XR_USE_PLATFORM_WIN32
	auto decoderType = ALXRDecoderType::D311VA;
#else
	auto decoderType = ALXRDecoderType::VAAPI;
#endif
	if (const auto clientCtx = ctx.clientCtx) {
		decoderType = clientCtx->decoderType;
	}

	OptionMap optionMap{};
#ifdef XR_USE_PLATFORM_ANDROID
	//// Exynos
	optionMap.setInt32("vendor.rtc-ext-dec-low-latency.enable", 1);
	//// qualcom,e.g. Quest 1/2 hw decoder.
	//optionMap.setInt32("vendor.qti-ext-dec-picture-order.enable", 1);
	optionMap.setInt32("vendor.qti-ext-dec-low-latency.enable", 1);
	//// AMD
	optionMap.setInt32("output-decode-order", 1);
	optionMap.setInt32("decode-low-latency", 1);
#endif
	const IDecoderPlugin::RunCtx runCtx{
		.optionMap   = std::move(optionMap),
		.config      = ctx.decoderConfig,
		.clientCtx   = ctx.clientCtx,
		.programPtr  = ctx.programPtr,
		.decoderType = decoderType
	};
	m_decoderPlugin = CreateDecoderPlugin(runCtx);
	LatencyManager::Instance().ResetAll();

	if (const auto clientCtx = ctx.clientCtx) {
		Log::Write(Log::Level::Verbose, "Sending IDR request");
		clientCtx->setWaitingNextIDR(true);
		clientCtx->requestIDR();
	}

#ifndef XR_DISABLE_DECODER_THREAD
	m_isRuningToken = true;
	m_decoderThread = std::thread
	{
		[this]()
		{
			m_decoderPlugin->Run(m_isRuningToken);
			Log::Write(Log::Level::Info, "Decoder thread exiting.");
		}
	};
	Log::Write(Log::Level::Info, "Decoder Thread started.");
#endif
}
