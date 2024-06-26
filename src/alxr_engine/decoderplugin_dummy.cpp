#include "decoderplugin.h"
namespace {
    struct DummyDecoderPlugin final : public IDecoderPlugin {

        virtual bool QueuePacket
        (
            const PacketType& /*newPacketData*/,
            const std::uint64_t /*trackingFrameIndex*/
        ) override {
            return true;
        }

        virtual bool Run(shared_bool& /*isRunningToken*/) override {
            return true;
        }
    };
}

std::shared_ptr<IDecoderPlugin> CreateDecoderPlugin_Dummy(const IDecoderPlugin::RunCtx&) {
    return std::make_shared<DummyDecoderPlugin>();
}
