#pragma once
#ifndef ALXR_INTERACTION_MANAGER_H
#define ALXR_INTERACTION_MANAGER_H
#include <cstdint>
#include <cassert>
#include <cstring>
#include <utility>
#include <algorithm>
#include <atomic>
#include <array>
#include <unordered_map>
#include <string_view>
#include <string>
#include <chrono>
#include <functional>
#include <optional>

#include "pch.h"
#include "common.h"
#include "alxr_ctypes.h"
#include "xr_utils.h"
#include "interaction_profiles.h"
#include "timing.h"
#include "ALVR-common/packet_types.h"
#include "eye_gaze_interaction.h"

namespace Side {
    constexpr const std::size_t LEFT = 0;
    constexpr const std::size_t RIGHT = 1;
    constexpr const std::size_t COUNT = 2;
}  // namespace Side

namespace ALXR {;

struct ALXRPaths {
    constexpr static const std::uint64_t INVALID_PATH = std::uint64_t(-1);
    std::uint64_t head;
    std::uint64_t left_hand;
    std::uint64_t right_hand;
    std::uint64_t left_haptics;
    std::uint64_t right_haptics;

    constexpr inline bool operator==(const ALXRPaths& rhs) const {
        return  head == rhs.head &&
                left_hand == rhs.left_hand &&
                right_hand == rhs.right_hand &&
                left_haptics == rhs.left_haptics &&
                right_haptics == rhs.right_haptics;
    }
    constexpr inline bool operator!=(const ALXRPaths& rhs) const {
        return !(*this == rhs);
    }
};
inline constexpr const ALXRPaths ALXR_NULL_PATHS {
    .head          = ALXRPaths::INVALID_PATH,
    .left_hand     = ALXRPaths::INVALID_PATH,
    .right_hand    = ALXRPaths::INVALID_PATH,
    .left_haptics  = ALXRPaths::INVALID_PATH,
    .right_haptics = ALXRPaths::INVALID_PATH
};

struct HapticsFeedback {
    std::uint64_t alxrPath;
    float amplitude;
    float duration;
    float frequency;
};

enum class PassthroughMode : std::size_t {
    None = 0,
    BlendLayer,
    MaskLayer
};
using TogglePTModeFn = std::function<void(const PassthroughMode)>;

struct InteractionManager {

    template < typename IsProfileSupportedFn >
    InteractionManager
    (
        XrInstance instance,
        XrSession session,
        const ALXRPaths& alxrPaths,
        const TogglePTModeFn& togglePTMode,
        IsProfileSupportedFn&& isProfileSupported
    );
    InteractionManager(const InteractionManager&) = delete;
    InteractionManager& operator=(const InteractionManager&) = delete;

    ~InteractionManager();    
    void Clear();

    bool IsHandActive(const std::size_t hand) const;
    XrPath GetXrPath(const char* const str) const;
    XrPath GetXrPath(const InteractionProfile& profile) const;
    XrPath GetXrInputPath(const InteractionProfile& profile, const std::size_t hand, const char* const str) const;
    XrPath GetXrOutputPath(const InteractionProfile& profile, const std::size_t hand, const char* const str) const;
    XrPath GetCurrentProfilePath(const std::size_t hand) const;

    using SpaceLocOpt = std::optional<ALXR::SpaceLoc>;
    inline SpaceLocOpt GetSpaceLocation
    (
        const std::size_t hand,
        const XrSpace& baseSpace,
        const XrTime& time
    ) const;

    inline std::optional<XrSpaceLocation> GetEyeGazeSpaceLocation
    (
        const XrSpace& baseSpace,
        const XrTime& time
    ) const;

    void LogActions() const;

    using ControllerInfo     = ::TrackingInfo::Controller;
    using ControllerInfoList = std::array<ControllerInfo, Side::COUNT>;
    void PollActions(ControllerInfoList& controllerInfo);

    void SetActiveProfile(const std::array<XrPath,2>& profilePaths);
    void SetActiveFromCurrentProfile();
    void ApplyHapticFeedback(const HapticsFeedback& hapticFeedback);

private:
    template < typename IsProfileSupportedFn >
    void InitializeActions(IsProfileSupportedFn&& isProfileSupported);
    template < typename IsProfileSupportedFn >
    void InitSuggestedBindings(IsProfileSupportedFn&& isProfileSupported) const;

    using SuggestedBindingList = std::vector<XrActionSuggestedBinding>;
    SuggestedBindingList MakeSuggestedBindings(const InteractionProfile& profile) const;

    bool IsClicked(const std::size_t hand, const ALVR_INPUT button, bool& changedSinceLastSync) const;

    bool PollQuitAction(const InteractionProfile& activeProfile);
    bool PollPassthrougMode(const InteractionProfile& activeProfile) const;

    void RequestExitSession();

    void LogActionSourceName(XrAction action, const char* const actionName) const;

    ALXRPaths  m_alxrPaths{ ALXR_NULL_PATHS };
    XrInstance m_instance { XR_NULL_HANDLE };
    XrSession  m_session  { XR_NULL_HANDLE };
    TogglePTModeFn m_togglePTMode{};

    using EyeGazeInteractionPtr = std::unique_ptr<ALXR::EyeGazeInteraction>;
    EyeGazeInteractionPtr m_eyeGazeInteraction { nullptr };

    using InteractionProfilePtr     = std::atomic<const InteractionProfile*>;
    using InteractionProfilePtrList = std::array<InteractionProfilePtr, 2>;
    InteractionProfilePtrList m_activeProfiles{ nullptr, nullptr };

    using HandPathList   = std::array<XrPath, Side::COUNT>;
    using HandSpaceList  = std::array<XrSpace, Side::COUNT>;
    using HandActiveList = std::array<XrBool32, Side::COUNT>;
    
    HandPathList   m_handSubactionPath { XR_NULL_PATH, XR_NULL_PATH };
    HandSpaceList  m_handSpace         { XR_NULL_HANDLE, XR_NULL_HANDLE };
    HandActiveList m_handActive        { XR_FALSE, XR_FALSE };

    using ClockType = XrSteadyClock;
    static_assert(ClockType::is_steady);
    using time_point = ClockType::time_point;
    time_point m_quitStartTime{};

    struct ALVRAction
    {
        const char* const name;
        const char* const localizedName;
        XrAction xrAction{ XR_NULL_HANDLE };
    };
    using ALVRActionMap = std::unordered_map<ALVR_INPUT, ALVRAction>;

    ALVRActionMap m_boolActionMap =
    {
        { ALVR_INPUT_SYSTEM_CLICK, { "system_click", "System Click" }},
        { ALVR_INPUT_APPLICATION_MENU_CLICK, { "appliction_click", "Appliction Click" }},
        { ALVR_INPUT_GRIP_CLICK, { "grip_click", "Grip Click" }},
        { ALVR_INPUT_GRIP_TOUCH, { "grip_touch", "Grip Touch" }},
        { ALVR_INPUT_A_CLICK, { "a_click", "A Click" }},
        { ALVR_INPUT_A_TOUCH, { "a_touch", "A Touch" }},
        { ALVR_INPUT_B_CLICK, { "b_click", "B Click" }},
        { ALVR_INPUT_B_TOUCH, { "b_touch", "B Touch" }},
        { ALVR_INPUT_X_CLICK, { "x_click", "X Click" }},
        { ALVR_INPUT_X_TOUCH, { "x_touch", "X Touch" }},
        { ALVR_INPUT_Y_CLICK, { "y_click", "Y Click" }},
        { ALVR_INPUT_Y_TOUCH, { "y_touch", "Y Touch" }},
        { ALVR_INPUT_JOYSTICK_CLICK, { "joystick_click", "Joystick Click" }},
        { ALVR_INPUT_JOYSTICK_TOUCH, { "joystick_touch", "Joystick Touch" }},
        { ALVR_INPUT_BACK_CLICK, { "back_click", "Back Click" }},
        { ALVR_INPUT_TRIGGER_CLICK, { "trigger_click", "Trigger Click" }},
        { ALVR_INPUT_TRIGGER_TOUCH, { "trigger_touch", "Trigger Touch" }},
        { ALVR_INPUT_TRACKPAD_CLICK, { "trackpad_click", "Trackpad Click" }},
        { ALVR_INPUT_TRACKPAD_TOUCH, { "trackpad_touch", "Trackpad Touch" }},
        { ALVR_INPUT_THUMB_REST_TOUCH, { "thumbrest_touch", "Thumbrest Touch" }},
    };
    ALVRActionMap m_scalarActionMap =
    {
        { ALVR_INPUT_GRIP_VALUE,    { "grip_value", "Grip Value" }},
        { ALVR_INPUT_JOYSTICK_X,    { "joystick_x", "Joystick X" }},
        { ALVR_INPUT_JOYSTICK_Y,    { "joystick_y", "Joystick Y" }},
        { ALVR_INPUT_TRIGGER_VALUE, { "trigger_value", "Trigger Value" }},
        { ALVR_INPUT_TRACKPAD_X,    { "trackpad_x", "Trackpad X" }},
        { ALVR_INPUT_TRACKPAD_Y,    { "trackpad_y", "Trackpad Y" }},
    };
    ALVRActionMap m_vector2fActionMap =
    {
        { ALVR_INPUT_JOYSTICK_X, { "joystick_pos", "Joystick Pos" }},
    };
    ALVRActionMap m_scalarToBoolActionMap =
    {
        { ALVR_INPUT_GRIP_CLICK,    { "grip_value_to_click", "Grip Value To Click" }, },
        { ALVR_INPUT_TRIGGER_CLICK, { "trigger_value_to_click", "Trigger Value To Click" } }
    };
    ALVRActionMap m_boolToScalarActionMap =
    {
        { ALVR_INPUT_GRIP_VALUE, { "grip_click_to_value", "Grip Click To Value" }}
    };
    XrAction m_poseAction    { XR_NULL_HANDLE };
    XrAction m_vibrateAction { XR_NULL_HANDLE };
    XrAction m_quitAction    { XR_NULL_HANDLE };
    XrActionSet m_actionSet  { XR_NULL_HANDLE };
};
////////////////////////////////////////////////////////////////////////////////////////////////////

template < typename IsProfileSupportedFn >
inline InteractionManager::InteractionManager
(
    XrInstance instance,
    XrSession session,
    const ALXRPaths& alxrPaths,
    const TogglePTModeFn& togglePTMode,
    IsProfileSupportedFn&& isProfileSupported
)
: m_alxrPaths{ alxrPaths },
  m_instance{ instance },
  m_session{ session },
  m_togglePTMode { togglePTMode }
{
    CHECK(m_alxrPaths != ALXR_NULL_PATHS);
    CHECK(m_instance != XR_NULL_HANDLE);
    CHECK(m_session != XR_NULL_HANDLE);
    InitializeActions(std::forward<IsProfileSupportedFn>(isProfileSupported));
}

inline InteractionManager::~InteractionManager() {
    Log::Write(Log::Level::Verbose, "Destroying InteractionManager");
    Clear();
}

inline void InteractionManager::Clear() {

    for (auto& activeProfile : m_activeProfiles) {
        activeProfile.store(nullptr);
    }
    Log::Write(Log::Level::Verbose, "Destroying Hand Action Spaces");
    for (auto hand : { Side::LEFT, Side::RIGHT }) {
        if (m_handSpace[hand] != XR_NULL_HANDLE) {
            xrDestroySpace(m_handSpace[hand]);
        }
        m_handSpace[hand] = XR_NULL_HANDLE;
    }

    m_eyeGazeInteraction.reset();

    if (m_actionSet != XR_NULL_HANDLE) {
        Log::Write(Log::Level::Verbose, "Destroying ActionSet");
        xrDestroyActionSet(m_actionSet);
        m_actionSet = XR_NULL_HANDLE;
    }
    m_boolToScalarActionMap.clear();
    m_scalarToBoolActionMap.clear();
    m_vector2fActionMap.clear();
    m_scalarActionMap.clear();
    m_boolActionMap.clear();
    m_quitAction = XR_NULL_HANDLE;
    m_vibrateAction = XR_NULL_HANDLE;
    m_poseAction = XR_NULL_HANDLE;
    
    m_handActive = { XR_FALSE, XR_FALSE };
    m_quitStartTime = {};

    m_session = XR_NULL_HANDLE;
    m_instance = XR_NULL_HANDLE;
}

inline XrPath InteractionManager::GetXrPath(const char* const str) const {
    XrPath path{ XR_NULL_PATH };
    CHECK_XRCMD(xrStringToPath(m_instance, str, &path));
    assert(path != XR_NULL_PATH);
    return path;
}

inline XrPath InteractionManager::GetXrPath(const InteractionProfile& profile) const {
    return GetXrPath(profile.path);
}

inline XrPath InteractionManager::GetXrInputPath(const InteractionProfile& profile, const std::size_t hand, const char* const str) const {
    using namespace std::string_literals;
    const auto fullPath = profile.userHandPaths[hand] + "/input/"s + str;
    return GetXrPath(fullPath.c_str());
}

inline XrPath InteractionManager::GetXrOutputPath(const InteractionProfile& profile, const std::size_t hand, const char* const str) const {
    using namespace std::string_literals;
    const auto fullPath = profile.userHandPaths[hand] + "/output/"s + str;
    return GetXrPath(fullPath.c_str());
}

inline XrPath InteractionManager::GetCurrentProfilePath(const std::size_t hand) const
{
    if (m_session == XR_NULL_HANDLE)
        return XR_NULL_PATH;
    const auto handPath = m_handSubactionPath[hand];
    XrInteractionProfileState ps{
        .type = XR_TYPE_INTERACTION_PROFILE_STATE,
        .next = nullptr,
        .interactionProfile = XR_NULL_PATH
    };
    if (XR_SUCCEEDED(xrGetCurrentInteractionProfile(m_session, handPath, &ps)) &&
        ps.interactionProfile != XR_NULL_PATH) {
        return ps.interactionProfile;
    }
    return XR_NULL_PATH;
}

inline bool InteractionManager::IsHandActive(const std::size_t hand) const {
    return m_handActive[hand] == XR_TRUE;
}

inline InteractionManager::SpaceLocOpt InteractionManager::GetSpaceLocation
(
    const std::size_t hand,
    const XrSpace& baseSpace,
    const XrTime& time
) const
{
    assert(hand < Side::COUNT);
    assert(m_handSpace[hand] != XR_NULL_HANDLE);
    return ALXR::GetSpaceLocation(m_handSpace[hand], baseSpace, time);
}

inline std::optional<XrSpaceLocation> InteractionManager::GetEyeGazeSpaceLocation
(
    const XrSpace& baseSpace,
    const XrTime& time
) const {
    if (m_eyeGazeInteraction == nullptr)
        return {};
    return m_eyeGazeInteraction->GetSpaceLocation(baseSpace, time);
}

inline InteractionManager::SuggestedBindingList
InteractionManager::MakeSuggestedBindings(const InteractionProfile& profile) const
{
    SuggestedBindingList bindings{ XrActionSuggestedBinding
        {m_poseAction, GetXrInputPath(profile, Side::LEFT,  profile.posePath)},
        {m_poseAction, GetXrInputPath(profile, Side::RIGHT, profile.posePath)},
    };

    if (profile.hapticPath) {
        for (const auto hand : { Side::LEFT, Side::RIGHT }) {
            bindings.push_back(XrActionSuggestedBinding{
                .action = m_vibrateAction,
                .binding = GetXrOutputPath(profile, hand, profile.hapticPath)
            });
        }
    }

    if (profile.quitPath) {
        bindings.push_back(XrActionSuggestedBinding{
            .action = m_quitAction,
            .binding = GetXrInputPath(profile, Side::LEFT, profile.quitPath)
        });
    }

    const auto helper = [&]
    (
        const std::size_t hand,
        const InputMap& inputMap,
        const auto& actionMap
    )
    {
        for (const auto& buttonMap : inputMap) {
            if (buttonMap == MapEnd)
                break;
            const auto action_itr = actionMap.find(buttonMap.button);
            if (action_itr == actionMap.end()) {
                Log::Write(Log::Level::Warning, Fmt("No action for button %d", buttonMap.button));
                continue;
            }
            const auto binding = GetXrInputPath(profile, hand, buttonMap.path);
            const auto& alvrAction = action_itr->second;
            bindings.push_back(XrActionSuggestedBinding{
                .action = alvrAction.xrAction,
                .binding = binding
            });
        }
    };
    for (const auto hand : { Side::LEFT, Side::RIGHT }) {
        helper(hand, profile.boolMap[hand],         m_boolActionMap);
        helper(hand, profile.scalarMap[hand],       m_scalarActionMap);
        helper(hand, profile.vector2fMap[hand],     m_vector2fActionMap);
        helper(hand, profile.boolToScalarMap[hand], m_boolToScalarActionMap);
        helper(hand, profile.scalarToBoolMap[hand], m_scalarToBoolActionMap);
    }
    return bindings;
}

template < typename IsProfileSupportedFn >
inline void InteractionManager::InitSuggestedBindings(IsProfileSupportedFn&& IsProfileSupported) const
{
    CHECK(m_instance != XR_NULL_HANDLE);
    for (const auto& profile : ALXR::InteractionProfileMap)
    {
        if (!IsProfileSupported(profile)) {
            Log::Write(Log::Level::Warning, Fmt("Interaction profile \"%s\" is not enabled or supported, no suggested bindings will be made for this profile.", profile.path));
            continue;
        }
        Log::Write(Log::Level::Info, Fmt("Creating suggested bindings for profile: \"%s\"", profile.path));
        const auto bindings = MakeSuggestedBindings(profile);
        const XrInteractionProfileSuggestedBinding suggestedBindings{
            .type = XR_TYPE_INTERACTION_PROFILE_SUGGESTED_BINDING,
            .next = nullptr,
            .interactionProfile = GetXrPath(profile),
            .countSuggestedBindings = (uint32_t)bindings.size(),
            .suggestedBindings = bindings.data()
        };
        if (XR_FAILED(xrSuggestInteractionProfileBindings(m_instance, &suggestedBindings))) {
            Log::Write(Log::Level::Warning, Fmt("Failed to make suggested bindings for profile \"%s\"", profile.path));
        }
    }
}

template < typename IsProfileSupportedFn >
inline void InteractionManager::InitializeActions(IsProfileSupportedFn&& isProfileSupported)
{
    CHECK(m_session  != XR_NULL_HANDLE);
    CHECK(m_instance != XR_NULL_HANDLE);

    m_handSubactionPath = {
        GetXrPath("/user/hand/left"),
        GetXrPath("/user/hand/right")
    };
    CHECK(std::none_of(m_handSubactionPath.begin(), m_handSubactionPath.end(), [](const XrPath p) { return p == XR_NULL_PATH; }));

    XrActionSetCreateInfo actionSetInfo {
        .type = XR_TYPE_ACTION_SET_CREATE_INFO,
        .next = nullptr,
        .priority = 0
    };
    std::strcpy(actionSetInfo.actionSetName, "alxr");
    std::strcpy(actionSetInfo.localizedActionSetName, "ALXR");
    CHECK_XRCMD(xrCreateActionSet(m_instance, &actionSetInfo, &m_actionSet));
    CHECK(m_actionSet != XR_NULL_HANDLE);
    {
        XrActionCreateInfo actionInfo{
            .type = XR_TYPE_ACTION_CREATE_INFO,
            .next = nullptr,
            .actionType = XR_ACTION_TYPE_POSE_INPUT,
            .countSubactionPaths = uint32_t(m_handSubactionPath.size()),
            .subactionPaths = m_handSubactionPath.data()
        };
        std::strcpy(actionInfo.actionName, "hand_pose");
        std::strcpy(actionInfo.localizedActionName, "Hand Pose");
        CHECK_XRCMD(xrCreateAction(m_actionSet, &actionInfo, &m_poseAction));
        CHECK(m_poseAction != XR_NULL_HANDLE);

        actionInfo = {
           .type = XR_TYPE_ACTION_CREATE_INFO,
           .next = nullptr,
           .actionType = XR_ACTION_TYPE_VIBRATION_OUTPUT,
           .countSubactionPaths = uint32_t(m_handSubactionPath.size()),
           .subactionPaths = m_handSubactionPath.data()
        };
        std::strcpy(actionInfo.actionName, "vibrate_hand");
        std::strcpy(actionInfo.localizedActionName, "Vibrate Hand");
        CHECK_XRCMD(xrCreateAction(m_actionSet, &actionInfo, &m_vibrateAction));
        CHECK(m_vibrateAction != XR_NULL_HANDLE);

        actionInfo = {
           .type = XR_TYPE_ACTION_CREATE_INFO,
           .next = nullptr,
           .actionType = XR_ACTION_TYPE_BOOLEAN_INPUT,
           .countSubactionPaths = 0,
           .subactionPaths = nullptr
        };
        std::strcpy(actionInfo.actionName, "quit_session");
        std::strcpy(actionInfo.localizedActionName, "Quit Session");
        CHECK_XRCMD(xrCreateAction(m_actionSet, &actionInfo, &m_quitAction));
        CHECK(m_quitAction != XR_NULL_HANDLE);
    }

    const auto CreateActions = [&](const XrActionType actType, auto& actionMap)
    {
        XrActionCreateInfo actionInfo {
            .type = XR_TYPE_ACTION_CREATE_INFO,
            .next = nullptr,
            .actionType = actType,
            .countSubactionPaths = uint32_t(m_handSubactionPath.size()),
            .subactionPaths = m_handSubactionPath.data()
        };
        for (auto& [k, alvrAction] : actionMap)
        {
            std::strcpy(actionInfo.actionName, alvrAction.name);
            std::strcpy(actionInfo.localizedActionName, alvrAction.localizedName);
            CHECK_XRCMD(xrCreateAction(m_actionSet, &actionInfo, &alvrAction.xrAction));
            CHECK(alvrAction.xrAction != XR_NULL_HANDLE);
        }
    };
    CreateActions(XR_ACTION_TYPE_BOOLEAN_INPUT,  m_boolActionMap);
    CreateActions(XR_ACTION_TYPE_FLOAT_INPUT,    m_scalarActionMap);
    CreateActions(XR_ACTION_TYPE_VECTOR2F_INPUT, m_vector2fActionMap);
    CreateActions(XR_ACTION_TYPE_BOOLEAN_INPUT,  m_boolToScalarActionMap);
    CreateActions(XR_ACTION_TYPE_BOOLEAN_INPUT,  m_scalarToBoolActionMap);

    XrActionSpaceCreateInfo actionSpaceInfo {
        .type = XR_TYPE_ACTION_SPACE_CREATE_INFO,
        .next = nullptr,
        .action = m_poseAction,
        .poseInActionSpace {
            .orientation {
                .x = 0.0f,
                .y = 0.0f,
                .z = 0.0f,
                .w = 1.f
            },
            .position {
                .x = 0.0f,
                .y = 0.0f,
                .z = 0.0f
            },
        },
    };
    for (const auto hand : { Side::LEFT, Side::RIGHT }) {
        actionSpaceInfo.subactionPath = m_handSubactionPath[hand];
        CHECK_XRCMD(xrCreateActionSpace(m_session, &actionSpaceInfo, &m_handSpace[hand]));
        CHECK(m_handSpace[hand] != XR_NULL_HANDLE);
    }

    InitSuggestedBindings(std::forward<IsProfileSupportedFn>(isProfileSupported));

    if (isProfileSupported(ALXR::EyeGazeProfile)) {
        m_eyeGazeInteraction = std::make_unique<ALXR::EyeGazeInteraction>(m_instance, m_session, m_actionSet);
    }

    const XrSessionActionSetsAttachInfo attachInfo {
        .type = XR_TYPE_SESSION_ACTION_SETS_ATTACH_INFO,
        .next = nullptr,
        .countActionSets = 1,
        .actionSets = &m_actionSet,
    };
    CHECK_XRCMD(xrAttachSessionActionSets(m_session, &attachInfo));
}

inline void InteractionManager::PollActions(InteractionManager::ControllerInfoList& controllerInfoList)
{
    if (m_session == XR_NULL_HANDLE)
        return;

    m_handActive = { XR_FALSE, XR_FALSE };

    // Sync actions
    const XrActiveActionSet activeActionSet{
        .actionSet = m_actionSet,
        .subactionPath = XR_NULL_PATH
    };
    const XrActionsSyncInfo syncInfo{
        .type = XR_TYPE_ACTIONS_SYNC_INFO,
        .next = nullptr,
        .countActiveActionSets = 1,
        .activeActionSets = &activeActionSet
    };
    if (XR_FAILED(xrSyncActions(m_session, &syncInfo))) {
        return;
    }

    if (m_eyeGazeInteraction)
        m_eyeGazeInteraction->PollActions();

    const std::array<const ALXR::InteractionProfile* const, 2> activeProfilePtrs = {
        m_activeProfiles[Side::LEFT].load(),
        m_activeProfiles[Side::RIGHT].load(),
    };

    for (const auto hand : { Side::LEFT, Side::RIGHT })
    {
        XrActionStateGetInfo getInfo{
            .type = XR_TYPE_ACTION_STATE_GET_INFO,
            .next = nullptr,
            .action = m_poseAction,
            .subactionPath = m_handSubactionPath[hand]
        };
        XrActionStatePose poseState{ .type = XR_TYPE_ACTION_STATE_POSE, .next = nullptr, .isActive = XR_FALSE };
        xrGetActionStatePose(m_session, &getInfo, &poseState);
        m_handActive[hand] = poseState.isActive;

        auto& controllerInfo = controllerInfoList[hand];
        if (poseState.isActive == XR_TRUE)
            controllerInfo.enabled = true;

        const auto activeProfilePtr = activeProfilePtrs[hand];
        if (activeProfilePtr == nullptr)
            continue;
        const auto forEachButton = [this, &getInfo](/*const*/ auto& actionMap, const ALXR::InputMap& inputMap, auto&& fn) -> void
        {
            for (const auto& buttonMap : inputMap) {
                if (buttonMap == ALXR::MapEnd)
                    break;
                const auto actionItr = actionMap.find(buttonMap.button);
                if (actionItr == actionMap.end())
                    continue;
                /*const*/ auto& alvrAction = actionItr->second;
                if (alvrAction.xrAction == XR_NULL_HANDLE)
                    continue;
                getInfo.action = alvrAction.xrAction;
                fn(buttonMap.button);
            }
        };
        const auto& activeProfile = *activeProfilePtr;
        forEachButton(m_boolActionMap, activeProfile.boolMap[hand], [&](const ALVR_INPUT button)
        {
            XrActionStateBoolean boolValue{ .type = XR_TYPE_ACTION_STATE_BOOLEAN, .next = nullptr, .isActive = XR_FALSE };
            if (XR_FAILED(xrGetActionStateBoolean(m_session, &getInfo, &boolValue)))
                return;
            if (boolValue.isActive == XR_TRUE && boolValue.currentState == XR_TRUE) {
                controllerInfo.buttons |= ALVR_BUTTON_FLAG(button);
            }
        });

        constexpr static const auto GetFloatRef = [](ControllerInfo& c, const ALVR_INPUT input) -> float&
        {
            switch (input) {
            case ALVR_INPUT_JOYSTICK_X:
                return c.joystickPosition.x;
            case ALVR_INPUT_TRACKPAD_X:
                return c.trackpadPosition.x;
            case ALVR_INPUT_JOYSTICK_Y:
                return c.joystickPosition.y;
            case ALVR_INPUT_TRACKPAD_Y:
                return c.trackpadPosition.y;
            case ALVR_INPUT_TRIGGER_VALUE:
                return c.triggerValue;
            case ALVR_INPUT_GRIP_VALUE:
            default:
                return c.gripValue;
            }
        };
        forEachButton(m_scalarActionMap, activeProfile.scalarMap[hand], [&](const ALVR_INPUT button)
        {
            XrActionStateFloat floatValue{ .type = XR_TYPE_ACTION_STATE_FLOAT, .next = nullptr, .isActive = XR_FALSE };
            if (XR_FAILED(xrGetActionStateFloat(m_session, &getInfo, &floatValue)) ||
                floatValue.isActive == XR_FALSE)
                return;
            auto& val = GetFloatRef(controllerInfo, button);
            val = floatValue.currentState;
            controllerInfo.enabled = true;
        });

        constexpr static const auto GetVector2fRef = [](ControllerInfo& c, const ALVR_INPUT input) -> decltype(ControllerInfo::trackpadPosition)&
        {
            switch (input) {
            case ALVR_INPUT_TRACKPAD_X:
            case ALVR_INPUT_TRACKPAD_Y:
                return c.trackpadPosition;
            case ALVR_INPUT_JOYSTICK_X:
            case ALVR_INPUT_JOYSTICK_Y:
            default: return c.joystickPosition;
            }
        };
        forEachButton(m_vector2fActionMap, activeProfile.vector2fMap[hand], [&](const ALVR_INPUT button)
        {
            XrActionStateVector2f vec2Value{ .type = XR_TYPE_ACTION_STATE_VECTOR2F, .next = nullptr, .isActive = XR_FALSE };
            if (XR_FAILED(xrGetActionStateVector2f(m_session, &getInfo, &vec2Value)) ||
                vec2Value.isActive == XR_FALSE)
                return;
            auto& val = GetVector2fRef(controllerInfo, button);
            val.x = vec2Value.currentState.x;
            val.y = vec2Value.currentState.y;
            controllerInfo.enabled = true;
        });

        forEachButton(m_boolToScalarActionMap, activeProfile.boolToScalarMap[hand], [&](const ALVR_INPUT button)
        {
            XrActionStateBoolean boolValue{ .type = XR_TYPE_ACTION_STATE_BOOLEAN, .next = nullptr, .isActive = XR_FALSE };
            if (XR_FAILED(xrGetActionStateBoolean(m_session, &getInfo, &boolValue)))
                return;
            if (boolValue.isActive == XR_TRUE && boolValue.currentState == XR_TRUE) {
                auto& val = GetFloatRef(controllerInfo, button);
                val = 1.0f;
                controllerInfo.enabled = true;
            }
        });

        forEachButton(m_scalarToBoolActionMap, activeProfile.scalarToBoolMap[hand], [&](const ALVR_INPUT button)
        {
            XrActionStateBoolean boolValue{ .type = XR_TYPE_ACTION_STATE_BOOLEAN, .next = nullptr, .isActive = XR_FALSE };
            if (XR_FAILED(xrGetActionStateBoolean(m_session, &getInfo, &boolValue)))
                return;
            if (boolValue.isActive == XR_TRUE && boolValue.currentState == XR_TRUE) {
                controllerInfo.buttons |= ALVR_BUTTON_FLAG(button);
            }
        });

        if (controllerInfo.buttons != 0)
            controllerInfo.enabled = true;
    }
    
    //
    // TODO: This code currently assume the same interaction profile for both hands,
    //       needs to change to support systems that can support mixed profiles per hand
    //       (.e.g `XR_EXT_hand_interaction` for hand and a controller-based profile on the other).
    //
    for (const auto activeProfilePtr : activeProfilePtrs) {
        if (activeProfilePtr) {
            const auto& activeProfile = *activeProfilePtr;
            PollPassthrougMode(activeProfile);
            PollQuitAction(activeProfile);
            return;
        }
    }
}

inline bool InteractionManager::PollQuitAction(const InteractionProfile& activeProfile) {
#ifdef ALXR_ENGINE_DISABLE_QUIT_ACTION
    (void)activeProfile;
    return false;
#else
    if (activeProfile.quitPath == nullptr || m_quitAction == XR_NULL_HANDLE)
        return false;

    assert(m_session != XR_NULL_HANDLE);
    // There were no subaction paths specified for the quit action, because we don't care which hand did it.
    const XrActionStateGetInfo getInfo{
        .type = XR_TYPE_ACTION_STATE_GET_INFO,
        .next = nullptr,
        .action = m_quitAction,
        .subactionPath = XR_NULL_PATH
    };
    XrActionStateBoolean quitValue{ .type = XR_TYPE_ACTION_STATE_BOOLEAN, .next = nullptr, .isActive = XR_FALSE };
    CHECK_XRCMD(xrGetActionStateBoolean(m_session, &getInfo, &quitValue));
    if (quitValue.isActive == XR_TRUE && quitValue.currentState == XR_TRUE) {
        using namespace std::literals::chrono_literals;
        if (quitValue.changedSinceLastSync == XR_TRUE) {
            m_quitStartTime = ClockType::now();
        }
        else {
            constexpr const auto QuitHoldTime = 4s;
            const auto currTime = ClockType::now();
            const auto holdTime = std::chrono::duration_cast<std::chrono::seconds>(currTime - m_quitStartTime);
            if (holdTime >= QuitHoldTime)
            {
                Log::Write(Log::Level::Info, "Exit session requested.");
                m_quitStartTime = currTime;
                RequestExitSession();
                return true;
            }
        }
    }
    return false;
#endif
}

template < typename Fn >
constexpr inline void forEachButton(const ALXR::ButtonFlags buttons, Fn&& fn) {
    for (int button = 0; button < ALVR_INPUT_COUNT; ++button) {
        const auto buttonMaskX = ALVR_BUTTON_FLAG(button);
        if ((buttons & buttonMaskX) == 0)
            continue;
        if (!fn(static_cast<const ALVR_INPUT>(button)))
            return;
    }
}

inline bool InteractionManager::IsClicked(const std::size_t hand, const ALVR_INPUT button, bool& changedSinceLastSync) const
{
    assert(m_session != XR_NULL_HANDLE);
    const auto actionItr = m_boolActionMap.find(button);
    if (actionItr == m_boolActionMap.end())
        return false;
    const auto& alvrAction = actionItr->second;
    if (alvrAction.xrAction == XR_NULL_HANDLE)
        return false;
    const XrActionStateGetInfo getInfo{
        .type = XR_TYPE_ACTION_STATE_GET_INFO,
        .next = nullptr,
        .action = alvrAction.xrAction,
        .subactionPath = m_handSubactionPath[hand]
    };
    XrActionStateBoolean value{ .type = XR_TYPE_ACTION_STATE_BOOLEAN, .next = nullptr, .isActive = XR_FALSE };
    CHECK_XRCMD(xrGetActionStateBoolean(m_session, &getInfo, &value));
    if (value.isActive == XR_FALSE)
        return false;
    changedSinceLastSync = value.changedSinceLastSync;
    return value.currentState == XR_TRUE;
};

inline bool InteractionManager::PollPassthrougMode(const InteractionProfile& activeProfile) const
{
    const auto& passthroughModes = activeProfile.passthroughModes;
    if (!passthroughModes.has_value() || !m_togglePTMode)
        return false;
    assert(m_session != XR_NULL_HANDLE);

    const auto IsMaskClicked = [this](const std::size_t hand, const ALXR::ButtonFlags buttonMask, bool& changedSinceLast)
    {
        bool isAllClicked = true;
        forEachButton(buttonMask, [&](const ALVR_INPUT button)
        {
            bool isChanged = false;
            if (!IsClicked(hand, button, /*out*/ isChanged))
                return isAllClicked = false;
            changedSinceLast = changedSinceLast || isChanged;
            return true;
        });
        return isAllClicked;
    };
    const auto IsButtonComboClicked = [IsMaskClicked](const HandButtonMaskList& handMasks, bool& changedSinceLastSync)
    {
        return IsMaskClicked(Side::LEFT,  handMasks[Side::LEFT], /*out*/ changedSinceLastSync) &&
               IsMaskClicked(Side::RIGHT, handMasks[Side::RIGHT],/*out*/ changedSinceLastSync);
    };
    bool changedSinceLastSync = false;
    const auto& blendMode = passthroughModes->blendMode;
    if (IsButtonComboClicked(blendMode, changedSinceLastSync) && changedSinceLastSync) {
        m_togglePTMode(PassthroughMode::BlendLayer);
        return true;
    }

    changedSinceLastSync = false;
    const auto& maskMode = passthroughModes->maskMode;
    if (IsButtonComboClicked(maskMode, changedSinceLastSync) && changedSinceLastSync) {
        m_togglePTMode(PassthroughMode::MaskLayer);
        return true;
    }

    return false;
}

inline void InteractionManager::ApplyHapticFeedback(const HapticsFeedback& hapticFeedback)
{
    if (m_session == XR_NULL_HANDLE)
        return;

    const size_t hand = hapticFeedback.alxrPath == m_alxrPaths.right_haptics ? 1 : 0;
    const auto activeProfilePtr = m_activeProfiles[hand].load();
    if (activeProfilePtr == nullptr || !activeProfilePtr->hapticPath)
        return;

    const XrHapticVibration vibration{
        .type = XR_TYPE_HAPTIC_VIBRATION,
        .next = nullptr,
        .duration = static_cast<XrDuration>(static_cast<double>(hapticFeedback.duration) * 1e+9),
        .frequency = hapticFeedback.frequency,
        .amplitude = hapticFeedback.amplitude
    };
    const XrHapticActionInfo hapticActionInfo{
        .type = XR_TYPE_HAPTIC_ACTION_INFO,
        .next = nullptr,
        .action = m_vibrateAction,
        .subactionPath = m_handSubactionPath[hand]
    };
    /*CHECK_XRCMD*/(xrApplyHapticFeedback(m_session, &hapticActionInfo, reinterpret_cast<const XrHapticBaseHeader*>(&vibration)));
}

inline void InteractionManager::RequestExitSession()
{
    if (m_session == XR_NULL_HANDLE)
        return;
    CHECK_XRCMD(xrRequestExitSession(m_session));
}

inline void InteractionManager::SetActiveProfile(const std::array<XrPath,2>& newProfilePaths)
{
    for (std::size_t idx = 0; idx < newProfilePaths.size(); ++idx) {
        const auto newProfilePath = newProfilePaths[idx];
        const auto newProfileItr = std::find_if
        (
            ALXR::InteractionProfileMap.begin(),
            ALXR::InteractionProfileMap.end(),
            [&](const ALXR::InteractionProfile& ip) { return newProfilePath == GetXrPath(ip.path); }
        );
        const auto* const newProfile = newProfileItr == ALXR::InteractionProfileMap.end() ?
            nullptr : &*newProfileItr;
        assert(newProfile != &ALXR::EyeGazeProfile);
        m_activeProfiles[idx].store(newProfile);

        Log::Write(Log::Level::Info, Fmt("Interaction Profile Changed for hand-index: %u", (std::uint32_t)idx));
        if (newProfile)
            Log::Write(Log::Level::Info, Fmt("\tNew selected profile: \"%s\"", newProfile->path));
        else
            Log::Write(Log::Level::Info, "\tNo new profile active/hand is not active.");
    }
}

inline void InteractionManager::SetActiveFromCurrentProfile() {
    const std::array<XrPath, 2> profilePaths = {
        GetCurrentProfilePath(Side::LEFT),
        GetCurrentProfilePath(Side::RIGHT),
    };
    SetActiveProfile(profilePaths);
}

inline void InteractionManager::LogActionSourceName(XrAction action, const char* const actionName) const
{
    if (action == XR_NULL_HANDLE || actionName == nullptr)
        return;

    const XrBoundSourcesForActionEnumerateInfo getInfo {
        .type = XR_TYPE_BOUND_SOURCES_FOR_ACTION_ENUMERATE_INFO,
        .next = nullptr,
        .action = action
    };
    uint32_t pathCount = 0;
    CHECK_XRCMD(xrEnumerateBoundSourcesForAction(m_session, &getInfo, 0, &pathCount, nullptr));
    std::vector<XrPath> paths(pathCount);
    CHECK_XRCMD(xrEnumerateBoundSourcesForAction(m_session, &getInfo, uint32_t(paths.size()), &pathCount, paths.data()));

    constexpr const XrInputSourceLocalizedNameFlags NameFlagsAll =
        XR_INPUT_SOURCE_LOCALIZED_NAME_USER_PATH_BIT |
        XR_INPUT_SOURCE_LOCALIZED_NAME_INTERACTION_PROFILE_BIT |
        XR_INPUT_SOURCE_LOCALIZED_NAME_COMPONENT_BIT;
    std::string sourceName;
    for (uint32_t i = 0; i < pathCount; ++i)
    {
        const XrInputSourceLocalizedNameGetInfo nameInfo{
            .type = XR_TYPE_INPUT_SOURCE_LOCALIZED_NAME_GET_INFO,
            .next = nullptr,
            .sourcePath = paths[i],
            .whichComponents = NameFlagsAll
        };
        uint32_t size = 0;
        CHECK_XRCMD(xrGetInputSourceLocalizedName(m_session, &nameInfo, 0, &size, nullptr));
        if (size < 1)
            continue;
        std::vector<char> grabSource(size);
        CHECK_XRCMD(xrGetInputSourceLocalizedName(m_session, &nameInfo, uint32_t(grabSource.size()), &size, grabSource.data()));
        if (!sourceName.empty())
            sourceName += " and ";
        sourceName += "'";
        sourceName += std::string(grabSource.data(), size - 1);
        sourceName += "'";
    }

    Log::Write(Log::Level::Info,
        Fmt("%s action is bound to %s", actionName, ((!sourceName.empty()) ? sourceName.c_str() : "nothing")));
}

inline void InteractionManager::LogActions() const {
    if (m_session == XR_NULL_HANDLE)
        return;
    LogActionSourceName(m_quitAction, "Quit");
    LogActionSourceName(m_poseAction, "Pose");
    LogActionSourceName(m_vibrateAction, "Vibrate");
    for (const auto& [k, v] : m_boolActionMap)
        LogActionSourceName(v.xrAction, v.localizedName);
    for (const auto& [k, v] : m_boolToScalarActionMap)
        LogActionSourceName(v.xrAction, v.localizedName);
    for (const auto& [k, v] : m_scalarActionMap)
        LogActionSourceName(v.xrAction, v.localizedName);
    for (const auto& [k, v] : m_scalarToBoolActionMap)
        LogActionSourceName(v.xrAction, v.localizedName);
    for (const auto& [k, v] : m_vector2fActionMap)
        LogActionSourceName(v.xrAction, v.localizedName);
}

}
#endif
