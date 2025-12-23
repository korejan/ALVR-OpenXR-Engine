#ifndef ALVRCLIENT_FEC_H
#define ALVRCLIENT_FEC_H

#include <cstddef>
#include <cstdint>
#include <cassert>
#include <memory>
#include <span>
#include <vector>
#include <mutex>
#include "packet_types.h"
#include "reedsolomon/rs.h"

class FECQueue {
public:
    FECQueue();

    using VideoPacket = std::span<const std::uint8_t>;
    void addVideoPacket(const VideoFrame& header, const VideoPacket& packet, bool& fecFailure);

    void addVideoPacket(const VideoFrame* packet, std::size_t packetSize, bool& fecFailure) {
        assert(packet != nullptr && packetSize > sizeof(VideoFrame));
        addVideoPacket(*packet, {
            reinterpret_cast<const std::uint8_t*>(packet) + sizeof(VideoFrame),
            packetSize - sizeof(VideoFrame)
        }, fecFailure);
    }

    bool reconstruct();
    const std::byte *getFrameBuffer() const;
    int getFrameByteSize() const;

    bool fecFailure() const;
    void clearFecFailure();

    FECQueue(const FECQueue&) = delete;
    FECQueue& operator=(const FECQueue&) = delete;
private:

    VideoFrame m_currentFrame;
    size_t m_shardPackets;
    size_t m_blockSize;
    size_t m_totalDataShards;
    size_t m_totalParityShards;
    size_t m_totalShards;
    uint32_t m_firstPacketOfNextFrame = 0;
    std::vector<std::vector<unsigned char>> m_marks;
    std::vector<std::byte> m_frameBuffer;
    std::vector<uint32_t> m_receivedDataShards;
    std::vector<uint32_t> m_receivedParityShards;
    std::vector<bool> m_recoveredPacket;
    std::vector<std::byte *> m_shards;
    bool m_recovered;
    bool m_fecFailure;

    struct ReedSolomon final : reed_solomon {

        constexpr const reed_solomon& base() const { return static_cast<const reed_solomon&>(*this); }
        constexpr reed_solomon& base() { return static_cast<reed_solomon&>(*this); }

        ReedSolomon(const ReedSolomon&) = delete;
        ReedSolomon& operator=(const ReedSolomon&) = delete;

        constexpr ReedSolomon() noexcept
        : reed_solomon{}{}

		ReedSolomon(const std::size_t data_shards, const std::size_t parity_shards) noexcept
        : reed_solomon{} {
            if (reed_solomon_new(static_cast<std::int32_t>(data_shards), static_cast<std::int32_t>(parity_shards), this) < 0) {
                base() = {};
            }
		}

        constexpr ReedSolomon(ReedSolomon&& src) noexcept
        : reed_solomon{ src.base() } {
            src.base() = {};
        }

        constexpr ReedSolomon& operator=(ReedSolomon&& src) noexcept {
            base() = src.base();
            src.base() = {};
            return *this;
        }

        ~ReedSolomon() noexcept {
            reed_solomon_release(this);
        }

        bool isValid() const noexcept {
            return m != nullptr && parity != nullptr;
		}
	};
    ReedSolomon m_rs{};

    static std::once_flag reed_solomon_initialized;
};

#endif //ALVRCLIENT_FEC_H
