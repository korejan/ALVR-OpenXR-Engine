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
#include "rs_auto.h"

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

    const std::uint8_t* getFrameBuffer() const {
        return &m_frameBuffer[0];
    }

    uint32_t getFrameByteSize() const {
        return m_currentFrame.frameByteSize;
    }

    bool fecFailure() const {
        return m_fecFailure;
    }

    void clearFecFailure() {
        m_fecFailure = false;
    }

    FECQueue(const FECQueue&) = delete;
    FECQueue& operator=(const FECQueue&) = delete;
private:

    VideoFrame m_currentFrame;
    uint32_t m_shardPackets;
    uint32_t m_blockSize;
    uint32_t m_totalDataShards;
    uint32_t m_totalParityShards;
    uint32_t m_totalShards;
    uint32_t m_firstPacketOfNextFrame = 0;
    std::vector<std::vector<std::uint8_t>> m_marks;
    std::vector<std::uint8_t> m_frameBuffer;
    std::vector<uint32_t> m_receivedDataShards;
    std::vector<uint32_t> m_receivedParityShards;
    std::vector<bool> m_recoveredPacket;
    std::vector<std::uint8_t*> m_shards;
    bool m_recovered;
    bool m_fecFailure;

    struct ReedSolomon final {

        std::vector<std::uint8_t> m_rs_buf{};
        reed_solomon* m_rs{ nullptr };
        
        ReedSolomon(const ReedSolomon&) = delete;
        ReedSolomon& operator=(const ReedSolomon&) = delete;

        ReedSolomon() noexcept = default;
        ReedSolomon(ReedSolomon&& src) noexcept = default;
        ReedSolomon& operator=(ReedSolomon&& src) noexcept = default;

        bool isValid() const noexcept {
            return m_rs != nullptr;
        }

		bool init(const std::uint32_t data_shards, const std::uint32_t parity_shards) {
            const size_t newSize = reed_solomon_bufsize(data_shards, parity_shards);
            if (m_rs_buf.size() < newSize) {
                m_rs_buf.resize(newSize);
            }
            m_rs = reed_solomon_new_static(m_rs_buf.data(), newSize, 
                static_cast<int>(data_shards),
                static_cast<int>(parity_shards));
            return isValid();
		}

        int reconstruct(uint8_t** shards, uint8_t* marks, std::uint32_t nr_shards, std::uint32_t bs) {
            if (!isValid()) {
                return -1;
            }
            return reed_solomon_reconstruct(m_rs, shards, marks, 
                static_cast<int>(nr_shards),
                static_cast<int>(bs));
        }
	};
    ReedSolomon m_rs{};

    static std::once_flag reed_solomon_initialized;
};

#endif //ALVRCLIENT_FEC_H
