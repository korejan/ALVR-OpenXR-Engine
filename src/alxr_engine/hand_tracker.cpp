#include "hand_tracker.h"
#include "common.h"
#include "logger.h"
#include "alxr_facial_eye_tracking_packet.h"
#include "packet_types.h"

namespace ALXR {
namespace {

constexpr bool IsPoseValid(XrSpaceLocationFlags locationFlags) {
    constexpr const XrSpaceLocationFlags PoseValidFlags = XR_SPACE_LOCATION_POSITION_VALID_BIT | XR_SPACE_LOCATION_ORIENTATION_VALID_BIT;
    return (locationFlags & PoseValidFlags) == PoseValidFlags;
}

constexpr bool IsPoseValid(const XrHandJointLocationEXT& jointLocation) {
    return IsPoseValid(jointLocation.locationFlags);
}

constexpr inline XrHandJointEXT GetJointParent(const XrHandJointEXT h) {
    switch (h) {
    case XR_HAND_JOINT_PALM_EXT:                return XR_HAND_JOINT_PALM_EXT;
    case XR_HAND_JOINT_WRIST_EXT:               return XR_HAND_JOINT_PALM_EXT;
    case XR_HAND_JOINT_THUMB_METACARPAL_EXT:    return XR_HAND_JOINT_WRIST_EXT;
    case XR_HAND_JOINT_THUMB_PROXIMAL_EXT:      return XR_HAND_JOINT_THUMB_METACARPAL_EXT;
    case XR_HAND_JOINT_THUMB_DISTAL_EXT:        return XR_HAND_JOINT_THUMB_PROXIMAL_EXT;
    case XR_HAND_JOINT_THUMB_TIP_EXT:           return XR_HAND_JOINT_THUMB_DISTAL_EXT;
    case XR_HAND_JOINT_INDEX_METACARPAL_EXT:    return XR_HAND_JOINT_WRIST_EXT;
    case XR_HAND_JOINT_INDEX_PROXIMAL_EXT:      return XR_HAND_JOINT_INDEX_METACARPAL_EXT;
    case XR_HAND_JOINT_INDEX_INTERMEDIATE_EXT:  return XR_HAND_JOINT_INDEX_PROXIMAL_EXT;
    case XR_HAND_JOINT_INDEX_DISTAL_EXT:        return XR_HAND_JOINT_INDEX_INTERMEDIATE_EXT;
    case XR_HAND_JOINT_INDEX_TIP_EXT:           return XR_HAND_JOINT_INDEX_DISTAL_EXT;
    case XR_HAND_JOINT_MIDDLE_METACARPAL_EXT:   return XR_HAND_JOINT_WRIST_EXT;
    case XR_HAND_JOINT_MIDDLE_PROXIMAL_EXT:     return XR_HAND_JOINT_MIDDLE_METACARPAL_EXT;
    case XR_HAND_JOINT_MIDDLE_INTERMEDIATE_EXT: return XR_HAND_JOINT_MIDDLE_PROXIMAL_EXT;
    case XR_HAND_JOINT_MIDDLE_DISTAL_EXT:       return XR_HAND_JOINT_MIDDLE_INTERMEDIATE_EXT;
    case XR_HAND_JOINT_MIDDLE_TIP_EXT:          return XR_HAND_JOINT_MIDDLE_DISTAL_EXT;
    case XR_HAND_JOINT_RING_METACARPAL_EXT:     return XR_HAND_JOINT_WRIST_EXT;
    case XR_HAND_JOINT_RING_PROXIMAL_EXT:       return XR_HAND_JOINT_RING_METACARPAL_EXT;
    case XR_HAND_JOINT_RING_INTERMEDIATE_EXT:   return XR_HAND_JOINT_RING_PROXIMAL_EXT;
    case XR_HAND_JOINT_RING_DISTAL_EXT:         return XR_HAND_JOINT_RING_INTERMEDIATE_EXT;
    case XR_HAND_JOINT_RING_TIP_EXT:            return XR_HAND_JOINT_RING_DISTAL_EXT;
    case XR_HAND_JOINT_LITTLE_METACARPAL_EXT:   return XR_HAND_JOINT_WRIST_EXT;
    case XR_HAND_JOINT_LITTLE_PROXIMAL_EXT:     return XR_HAND_JOINT_LITTLE_METACARPAL_EXT;
    case XR_HAND_JOINT_LITTLE_INTERMEDIATE_EXT: return XR_HAND_JOINT_LITTLE_PROXIMAL_EXT;
    case XR_HAND_JOINT_LITTLE_DISTAL_EXT:       return XR_HAND_JOINT_LITTLE_INTERMEDIATE_EXT;
    case XR_HAND_JOINT_LITTLE_TIP_EXT:          return XR_HAND_JOINT_LITTLE_DISTAL_EXT;
    default: return h;
    }
}

constexpr inline XrHandJointEXT ToXRHandJointType(const ALVR_HAND h) {
    switch (h) {
    case ALVR_HAND::alvrHandBone_WristRoot: return XR_HAND_JOINT_WRIST_EXT;
    case ALVR_HAND::alvrHandBone_Thumb0:    return XR_HAND_JOINT_THUMB_METACARPAL_EXT;
    case ALVR_HAND::alvrHandBone_Thumb1:    return XR_HAND_JOINT_THUMB_PROXIMAL_EXT;
    case ALVR_HAND::alvrHandBone_Thumb2:    return XR_HAND_JOINT_THUMB_DISTAL_EXT;
    case ALVR_HAND::alvrHandBone_Thumb3:    return XR_HAND_JOINT_THUMB_TIP_EXT;
    case ALVR_HAND::alvrHandBone_Index1:    return XR_HAND_JOINT_INDEX_PROXIMAL_EXT;
    case ALVR_HAND::alvrHandBone_Index2:    return XR_HAND_JOINT_INDEX_INTERMEDIATE_EXT;
    case ALVR_HAND::alvrHandBone_Index3:    return XR_HAND_JOINT_INDEX_DISTAL_EXT;
    case ALVR_HAND::alvrHandBone_Middle1:   return XR_HAND_JOINT_MIDDLE_PROXIMAL_EXT;
    case ALVR_HAND::alvrHandBone_Middle2:   return XR_HAND_JOINT_MIDDLE_INTERMEDIATE_EXT;
    case ALVR_HAND::alvrHandBone_Middle3:   return XR_HAND_JOINT_MIDDLE_DISTAL_EXT;
    case ALVR_HAND::alvrHandBone_Ring1:     return XR_HAND_JOINT_RING_PROXIMAL_EXT;
    case ALVR_HAND::alvrHandBone_Ring2:     return XR_HAND_JOINT_RING_INTERMEDIATE_EXT;
    case ALVR_HAND::alvrHandBone_Ring3:     return XR_HAND_JOINT_RING_DISTAL_EXT;
    case ALVR_HAND::alvrHandBone_Pinky0:    return XR_HAND_JOINT_LITTLE_METACARPAL_EXT;
    case ALVR_HAND::alvrHandBone_Pinky1:    return XR_HAND_JOINT_LITTLE_PROXIMAL_EXT;
    case ALVR_HAND::alvrHandBone_Pinky2:    return XR_HAND_JOINT_LITTLE_INTERMEDIATE_EXT;
    case ALVR_HAND::alvrHandBone_Pinky3:    return XR_HAND_JOINT_LITTLE_DISTAL_EXT;
    default: return XR_HAND_JOINT_MAX_ENUM_EXT;
    }
}
}

XrHandTracker::XrHandTracker(const XrContext& ctx)
: m_ctx{ ctx }
, m_runtimeType{ ctx.GetRuntimeType() } {
    
    if (!IsSupported()) {
        Log::Write(Log::Level::Warning, Fmt("%s is not enabled/supported.", XR_EXT_HAND_TRACKING_EXTENSION_NAME));
        return ;
    }
    if (!LoadExtFunctions()) {
        return;
    }

    // Create a hand tracker for left hand that tracks default set of hand joints.
    const auto createHandTracker = [&](auto& handTracker, const XrHandEXT hand)
    {
        const XrHandTrackerCreateInfoEXT createInfo{
            .type = XR_TYPE_HAND_TRACKER_CREATE_INFO_EXT,
            .next = nullptr,
            .hand = hand,
            .handJointSet = XR_HAND_JOINT_SET_DEFAULT_EXT
        };
        if (XR_FAILED(xrCreateHandTrackerEXT(m_ctx.session, &createInfo, &handTracker.tracker))) {
            handTracker.tracker = XR_NULL_HANDLE;
            Log::Write(Log::Level::Error, Fmt("Failed to create hand tracker for %s hand.", hand == XR_HAND_LEFT_EXT ? "left" : "right"));
        }
    };
    createHandTracker(m_handTrackers[0], XR_HAND_LEFT_EXT);
    createHandTracker(m_handTrackers[1], XR_HAND_RIGHT_EXT);

    auto& leftHandBaseOrientation = m_handTrackers[0].baseOrientation;
    auto& rightHandBaseOrientation = m_handTrackers[1].baseOrientation;
    auto& yRot = rightHandBaseOrientation;
    yRot = Eigen::AngleAxisf(ALXR::ToRadians(-90.0f), Eigen::Vector3f(0, 1, 0));
    const Eigen::Quaternionf zRot(Eigen::AngleAxisf(ALXR::ToRadians(180.0f), Eigen::Vector3f(0, 0, 1)));
    leftHandBaseOrientation = yRot * zRot;
}

XrHandTracker::~XrHandTracker() {
    Log::Write(Log::Level::Verbose, "Destroying HandTrackers");
    for (auto& handTracker : m_handTrackers) {
        if (handTracker.tracker != XR_NULL_HANDLE) {
            assert(xrDestroyHandTrackerEXT);
            xrDestroyHandTrackerEXT(handTracker.tracker);
            handTracker.tracker = XR_NULL_HANDLE;
        }
    }
}

bool XrHandTracker::IsSupported() const {
    if (!m_ctx.IsValid())
        return false;
    const XrSystemId systemId = m_ctx.GetSystemId();
    if (systemId == XR_NULL_SYSTEM_ID)
        return false;
    // Inspect hand tracking system properties
    XrSystemHandTrackingPropertiesEXT handTrackingSystemProperties{
        .type = XR_TYPE_SYSTEM_HAND_TRACKING_PROPERTIES_EXT,
        .next = nullptr,
        .supportsHandTracking = XR_FALSE
    };
    XrSystemProperties systemProperties{ .type = XR_TYPE_SYSTEM_PROPERTIES, .next = &handTrackingSystemProperties };
    if (XR_FAILED(xrGetSystemProperties(m_ctx.instance, systemId, &systemProperties))) {
        return false;
    }
    return handTrackingSystemProperties.supportsHandTracking == XR_TRUE;
}

bool XrHandTracker::IsEnabled() const {
    return m_handTrackers[0].tracker != XR_NULL_HANDLE ||
           m_handTrackers[1].tracker != XR_NULL_HANDLE;
}

bool XrHandTracker::LoadExtFunctions() {
    if (!m_ctx.IsValid())
        return false;
    assert(IsSupported());

#ifndef XR_EXTENSION_PROTOTYPES
#define ALXR_INIT_PFN(ExtName)\
    if (XR_FAILED(xrGetInstanceProcAddr(m_ctx.instance, #ExtName, reinterpret_cast<PFN_xrVoidFunction*>(&ExtName)))) { \
        return false; \
    }

    ALXR_INIT_PFN(xrCreateHandTrackerEXT)
    ALXR_INIT_PFN(xrLocateHandJointsEXT)
    ALXR_INIT_PFN(xrDestroyHandTrackerEXT)

#undef ALXR_INIT_PFN

    if (xrCreateHandTrackerEXT  == nullptr ||
        xrLocateHandJointsEXT   == nullptr ||
        xrDestroyHandTrackerEXT == nullptr)
    {
        xrCreateHandTrackerEXT  = nullptr;
        xrLocateHandJointsEXT   = nullptr;
        xrDestroyHandTrackerEXT = nullptr;
        Log::Write(Log::Level::Warning, Fmt("%s is not enabled/supported.", XR_EXT_HAND_TRACKING_EXTENSION_NAME));
        return false;
    }
    Log::Write(Log::Level::Info, Fmt("%s is enabled.", XR_EXT_HAND_TRACKING_EXTENSION_NAME));

#endif
    return true;
}

bool XrHandTracker::GetJointLocations(const XrTime& time, const XrSpace space, ALXRHandTracking& handTrackingData) const {
    static_assert(sizeof(ALXRHandJointLocation) == sizeof(XrHandJointLocationEXT));
    static_assert(sizeof(ALXRHandJointVelocity) == sizeof(XrHandJointVelocityEXT));
    static_assert(MaxHandJointCount == XR_HAND_JOINT_COUNT_EXT);

    for (std::size_t hand = 0; hand < 2; ++hand) {

        auto& handData = handTrackingData.hands[hand];
        handData.isActive = false;
        if (time == 0)
            continue;
        auto& handTracker = m_handTrackers[hand];
        if (handTracker.tracker == XR_NULL_HANDLE)
            continue;

        XrHandJointVelocitiesEXT velocities{
            .type = XR_TYPE_HAND_JOINT_VELOCITIES_EXT,
            .next = nullptr,
            .jointCount = XR_HAND_JOINT_COUNT_EXT,
            .jointVelocities = reinterpret_cast<XrHandJointVelocityEXT*>(&handData.jointVelocities[0]),
        };
        XrHandJointLocationsEXT locations{
            .type = XR_TYPE_HAND_JOINT_LOCATIONS_EXT,
            .next = &velocities,
            .isActive = XR_FALSE,
            .jointCount = XR_HAND_JOINT_COUNT_EXT,
            .jointLocations = reinterpret_cast<XrHandJointLocationEXT*>(&handData.jointLocations[0]),
        };
        const XrHandJointsLocateInfoEXT locateInfo{
            .type = XR_TYPE_HAND_JOINTS_LOCATE_INFO_EXT,
            .next = nullptr,
            .baseSpace = space,
            .time = time
        };
        handData.isActive = XR_SUCCEEDED(xrLocateHandJointsEXT(handTracker.tracker, &locateInfo, &locations)) &&
            locations.isActive == XR_TRUE;
    }
    return true;
}

bool XrHandTracker::GetJointLocations(const XrTime& time, const XrSpace space, ALXRTrackingInfo::Controller (&controllerInfo)[2]) /*const*/ {
    if (!IsEnabled() || time == 0)
        return  false;

    const bool isHandOnControllerPose = //m_runtimeType == OxrRuntimeType::HTCWave ||
                                          m_runtimeType == XrRuntimeType::SteamVR ||
                                          m_runtimeType == XrRuntimeType::WMR ||
                                          m_runtimeType == XrRuntimeType::MagicLeap;
    std::array<Eigen::Affine3f, XR_HAND_JOINT_COUNT_EXT> oculusOrientedJointPoses;
    for (std::size_t hand = 0; hand < 2; ++hand) {

        auto& controller = controllerInfo[hand];
        // TODO: v17/18 server does not allow for both controller & hand tracking data, this needs changing,
        //       we don't want to override a controller device pose with potentially an emulated pose for
        //       runtimes such as WMR & SteamVR.
        if (isHandOnControllerPose && controller.enabled)
            continue;

        auto& handTracker = m_handTrackers[hand];
        if (handTracker.tracker == XR_NULL_HANDLE)
            continue;

        //XrHandJointVelocitiesEXT velocities {
        //    .type = XR_TYPE_HAND_JOINT_VELOCITIES_EXT,
        //    .next = nullptr,
        //    .jointCount = XR_HAND_JOINT_COUNT_EXT,
        //    .jointVelocities = handTracker.jointVelocities.data(),
        //};
        XrHandJointLocationsEXT locations{
            .type = XR_TYPE_HAND_JOINT_LOCATIONS_EXT,
            .next = nullptr, //&velocities,
            .isActive = XR_FALSE,
            .jointCount = XR_HAND_JOINT_COUNT_EXT,
            .jointLocations = handTracker.jointLocations.data(),
        };
        const XrHandJointsLocateInfoEXT locateInfo{
            .type = XR_TYPE_HAND_JOINTS_LOCATE_INFO_EXT,
            .next = nullptr,
            .baseSpace = space,
            .time = time
        };
        if (XR_FAILED(xrLocateHandJointsEXT(handTracker.tracker, &locateInfo, &locations)) ||
            locations.isActive == XR_FALSE)
            continue;

        const auto& jointLocations = handTracker.jointLocations;
        const auto& handBaseOrientation = handTracker.baseOrientation;
        for (size_t jointIdx = 0; jointIdx < XR_HAND_JOINT_COUNT_EXT; ++jointIdx)
        {
            const auto& jointLoc = jointLocations[jointIdx];
            Eigen::Affine3f& jointMatFixed = oculusOrientedJointPoses[jointIdx];
            jointMatFixed.setIdentity();
            if (!IsPoseValid(jointLoc)) {
                continue;
            }
            jointMatFixed = ALXR::ToAffine3f(jointLoc.pose) * handBaseOrientation;
        }

        for (size_t boneIndex = 0; boneIndex < ALVR_HAND::alvrHandBone_MaxSkinnable; ++boneIndex)
        {
            auto& boneRot = controller.boneRotations[boneIndex];
            auto& bonePos = controller.bonePositionsBase[boneIndex];
            boneRot = { 0,0,0,1 };
            bonePos = { 0,0,0 };

            const auto xrJoint = ToXRHandJointType(static_cast<const ALVR_HAND>(boneIndex));
            if (xrJoint == XR_HAND_JOINT_MAX_ENUM_EXT)
                continue;

            const auto xrJointParent = GetJointParent(xrJoint);
            const Eigen::Affine3f& jointParentWorld = oculusOrientedJointPoses[xrJointParent];
            const Eigen::Affine3f& JointWorld = oculusOrientedJointPoses[xrJoint];

            const Eigen::Affine3f jointLocal = jointParentWorld.inverse() * JointWorld;

            const Eigen::Quaternionf localizedRot(jointLocal.rotation());
            boneRot = ToALXRQuaternionf(localizedRot);
            bonePos = ToALXRVector3f(jointLocal.translation());
        }

        controller.enabled = true;
        controller.isHand = true;

        const Eigen::Affine3f& palmMatP = oculusOrientedJointPoses[XR_HAND_JOINT_PALM_EXT];
        controller.boneRootPose = ToALXRPosef(palmMatP);
        controller.linearVelocity = { 0,0,0 };
        controller.angularVelocity = { 0,0,0 };
    }
    return true;
}

}