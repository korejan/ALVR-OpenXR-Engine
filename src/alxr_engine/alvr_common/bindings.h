#pragma once

#ifdef __cplusplus
extern "C" {;
#endif

#include <stdint.h>

typedef struct EyeFov {
    float left = 49.;
    float right = 45.;
    float top = 50.;
    float bottom = 48.;
} ALXREyeFov;

typedef struct TrackingQuat {
    float x;
    float y;
    float z;
    float w;
} ALXRQuaternionf;

typedef struct TrackingVector3 {
    float x;
    float y;
    float z;
} ALXRVector3f;

typedef struct TrackingVector2 {
    float x;
    float y;
} ALXRVector2f;

typedef struct ALXRPosef {
    ALXRQuaternionf orientation;
    ALXRVector3f    position;
} ALXRPosef;

typedef struct TrackingInfo {
    static constexpr const uint32_t MAX_CONTROLLERS = 2;
    static constexpr const uint32_t BONE_COUNT = 19;
    struct Controller {
        // Tracking info of hand. A3
        ALXRQuaternionf boneRotations[BONE_COUNT];
        ALXRVector3f    bonePositionsBase[BONE_COUNT];
        ALXRPosef       boneRootPose;

        // Tracking info of controller. (float * 19 = 76 bytes)
        ALXRPosef    pose;
        ALXRVector3f angularVelocity;
        ALXRVector3f linearVelocity;

        ALXRVector2f joystickPosition;
        ALXRVector2f trackpadPosition;

        uint64_t buttons;

        float triggerValue;
        float gripValue;

        uint32_t handFingerConfidences;

        bool enabled;
        bool isHand;
    } controller[MAX_CONTROLLERS];

    ALXRPosef headPose;
    uint64_t  targetTimestampNs;
    uint8_t   mounted;
} ALXRTrackingInfo;

// Client >----(mode 0)----> Server
// Client <----(mode 1)----< Server
// Client >----(mode 2)----> Server
// Client <----(mode 3)----< Server
typedef struct TimeSync {
    uint32_t type; // ALVR_PACKET_TYPE_TIME_SYNC
    uint32_t mode; // 0,1,2,3
    uint64_t sequence;
    uint64_t serverTime;
    uint64_t clientTime;

    // Following value are filled by client only when mode=0.
    uint64_t packetsLostTotal;
    uint64_t packetsLostInSecond;

    uint64_t averageDecodeLatency;

    uint32_t averageTotalLatency;

    uint32_t averageSendLatency;

    uint32_t averageTransportLatency;
    
    uint32_t idleTime;

    uint64_t fecFailureInSecond;
    uint64_t fecFailureTotal;
    uint32_t fecFailure;

    float fps;

    // Following value are filled by server only when mode=3.
    uint64_t trackingRecvFrameIndex;

    // Following value are filled by server only when mode=1.
    uint32_t serverTotalLatency;
} ALXRTimeSync;

typedef struct VideoFrame {
    uint32_t type; // ALVR_PACKET_TYPE_VIDEO_FRAME
    uint32_t packetCounter;
    uint64_t trackingFrameIndex;
    // FEC decoder needs some value for identify video frame number to detect new frame.
    // trackingFrameIndex becomes sometimes same value as previous video frame (in case of low
    // tracking rate).
    uint64_t videoFrameIndex;
    uint64_t sentTime;
    uint32_t frameByteSize;
    uint32_t fecIndex;
    uint16_t fecPercentage;
    // char frameBuffer[];
} ALXRVideoFrame;

#ifdef __cplusplus
}
#endif
