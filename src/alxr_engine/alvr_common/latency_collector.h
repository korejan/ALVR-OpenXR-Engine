#ifndef ALVRCLIENT_LATENCY_COLLECTOR_H
#define ALVRCLIENT_LATENCY_COLLECTOR_H

#include <memory>
#include <vector>
#include <map>
#include <atomic>
#include <mutex>

class LatencyCollector {
public:
    static LatencyCollector &Instance();

    uint64_t getTrackingPredictionLatency() const;
    uint64_t getLatency(uint32_t i) const;
    uint64_t getPacketsLostTotal() const;
    uint64_t getPacketsLostInSecond() const;
    uint64_t getFecFailureTotal() const;
    uint64_t getFecFailureInSecond() const;
    float getFramesInSecond() const;

    void packetLoss(int64_t lost);
    void fecFailure();

    void setTotalLatency(uint32_t latency);

    void tracking(uint64_t frameIndex);
    void estimatedSent(uint64_t frameIndex, uint64_t offset);
    void received(uint64_t frameIndex);
    void receivedFirst(uint64_t frameIndex);
    void receivedLast(uint64_t frameIndex);
    void decoderInput(uint64_t frameIndex);
    void decoderOutput(uint64_t frameIndex);
    void rendered1(uint64_t frameIndex);
    void rendered2(uint64_t frameIndex);
    void submit(uint64_t frameIndex);

    void resetAll();
private:
    LatencyCollector();

    void submitNewFrame();

    void resetSecond();
    void checkAndResetSecond();

    static LatencyCollector m_Instance;

    struct FrameTimestamp {
        uint64_t frameIndex;

        // Timestamp in microsec.
        uint64_t tracking;
        uint64_t estimatedSent;
        uint64_t received;
        uint64_t receivedFirst;
        uint64_t receivedLast;
        uint64_t decoderInput;
        uint64_t decoderOutput;
        uint64_t rendered1;
        uint64_t rendered2;
        uint64_t submit;
    };
    constexpr static const int MAX_FRAMES = 1024;
    std::map<uint64_t, FrameTimestamp> m_Frames = std::map<uint64_t, FrameTimestamp>();
    std::mutex m_framesMutex;

    uint64_t m_StatisticsTime;
    uint64_t m_PacketsLostTotal = 0;
    uint64_t m_PacketsLostInSecond = 0;
    uint64_t m_PacketsLostPrevious = 0;
    uint64_t m_FecFailureTotal = 0;
    uint64_t m_FecFailureInSecond = 0;
    uint64_t m_FecFailurePrevious = 0;

    std::atomic<uint32_t> m_ServerTotalLatency { 0 };

    // Total/Transport/Decode/Idle latency
    uint64_t m_Latency[5]{ 0,0,0,0,0 };

    uint64_t m_LastSubmit = 0;
    float m_FramesInSecond = 0;

    FrameTimestamp & getFrame(uint64_t frameIndex);
};

#endif //ALVRCLIENT_LATENCY_COLLECTOR_H
