#pragma once
#ifndef ALXR_INTERACTION_PROFILES_H
#define ALXR_INTERACTION_PROFILES_H

#include "pch.h"
#include <cstdint>
#include <array>
#include <optional>
#include "xrpaths.h"
#include "ALVR-common/packet_types.h"

namespace ALXR {;

struct ButtonMap {
    const ALVR_INPUT button;
    const char* const path;

    constexpr inline bool operator==(const ButtonMap& rhs) const { return button == rhs.button;}
    constexpr inline bool operator!=(const ButtonMap& rhs) const { return !(*this == rhs); }
};
using InputMap = std::array<const ButtonMap, 12>;
using LeftMap = InputMap;
using RightMap = InputMap;

constexpr inline const std::size_t HandSize = 2;
using HandInputMap = std::array<const InputMap, HandSize>;
using HandPathList = std::array<const char* const, HandSize>;

using ButtonFlags = std::uint64_t;
using HandButtonMaskList = std::array<const ButtonFlags, HandSize>;

constexpr inline const ButtonMap    MapEnd{ ALVR_INPUT_COUNT,nullptr };
constexpr inline const InputMap     EmptyMap{ MapEnd };
constexpr inline const HandInputMap EmptyHandMap{ EmptyMap, EmptyMap };
constexpr inline const HandButtonMaskList EmptyHandMask{ 0, 0 };
constexpr inline const HandPathList UserHandPaths{
    XRPaths::UserHandLeft,
    XRPaths::UserHandRight
};
constexpr inline const HandPathList UserHandHTCPaths {
    XRPaths::UserHandLeftHTC,
    XRPaths::UserHandRightHTC
};

struct PassthroughModeButtons {
    const HandButtonMaskList blendMode = EmptyHandMask;
    const HandButtonMaskList maskMode  = EmptyHandMask;
};
using OptionalPassthroughMode = std::optional<const PassthroughModeButtons>;

struct InteractionProfile {
    const HandInputMap boolMap         = EmptyHandMap;
    const HandInputMap scalarMap       = EmptyHandMap;
    const HandInputMap vector2fMap     = EmptyHandMap;
    const HandInputMap boolToScalarMap = EmptyHandMap;
    const HandInputMap scalarToBoolMap = EmptyHandMap;

    const char* const path;
    const char* const extensionName = nullptr;
    const char* const quitPath   = XRPaths::MenuClick;
    const char* const hapticPath = XRPaths::Haptic;
    const char* const posePath   = XRPaths::AimPose;
    const char* const eyeGazePosePath = nullptr;
        
    const HandPathList userHandPaths = UserHandPaths;
    const char* const  userEyesPath = nullptr;

    // set button flags must refer to entries in InteractionProfile::boolMap.
    const OptionalPassthroughMode passthroughModes {};

    constexpr inline bool IsCore() const { return extensionName == nullptr; }
    constexpr inline bool IsExt() const  { return !IsCore(); }
};

using namespace XRPaths;

constexpr inline const InteractionProfile EyeGazeProfile{
    .path = "/interaction_profiles/ext/eye_gaze_interaction",
    .extensionName = XR_EXT_EYE_GAZE_INTERACTION_EXTENSION_NAME,
    .quitPath = nullptr,
    .hapticPath = nullptr,
    .posePath = nullptr,
    .eyeGazePosePath = ALXR::GazeExtPose,
    .userEyesPath = ALXR::UserEyesExt
};

constexpr inline const std::size_t ProfileMapSize = 13;
constexpr inline const std::array<const InteractionProfile, ProfileMapSize> InteractionProfileMap{
    InteractionProfile {
        .boolMap {
            LeftMap { ButtonMap
                {ALVR_INPUT_SYSTEM_CLICK, MenuClick},
                {ALVR_INPUT_GRIP_CLICK, SqueezeClick},
                {ALVR_INPUT_X_CLICK, XClick},
                {ALVR_INPUT_X_TOUCH, XTouch},
                {ALVR_INPUT_Y_CLICK, YClick},
                {ALVR_INPUT_Y_TOUCH, YTouch},
                {ALVR_INPUT_JOYSTICK_CLICK, ThumbstickClick},
                {ALVR_INPUT_JOYSTICK_TOUCH, ThumbstickTouch},
                {ALVR_INPUT_TRIGGER_CLICK, TriggerClick},
                {ALVR_INPUT_TRIGGER_TOUCH, TriggerTouch},
                MapEnd
            },
            RightMap { ButtonMap
                {ALVR_INPUT_SYSTEM_CLICK, MenuClick},
                {ALVR_INPUT_GRIP_CLICK, SqueezeClick},
                {ALVR_INPUT_A_CLICK, AClick},
                {ALVR_INPUT_A_TOUCH, ATouch},
                {ALVR_INPUT_B_CLICK, BClick},
                {ALVR_INPUT_B_TOUCH, BTouch},
                {ALVR_INPUT_JOYSTICK_CLICK, ThumbstickClick},
                {ALVR_INPUT_JOYSTICK_TOUCH, ThumbstickTouch},
                {ALVR_INPUT_TRIGGER_CLICK, TriggerClick},
                {ALVR_INPUT_TRIGGER_TOUCH, TriggerTouch},
                MapEnd
            },
        },
        .scalarMap {
            LeftMap { ButtonMap
                {ALVR_INPUT_GRIP_VALUE, SqueezeValue},
                {ALVR_INPUT_TRIGGER_VALUE, TriggerValue},
                MapEnd
            },
            RightMap { ButtonMap
                {ALVR_INPUT_GRIP_VALUE, SqueezeValue},
                {ALVR_INPUT_TRIGGER_VALUE, TriggerValue},
                MapEnd
            }
        },
        .vector2fMap {
            LeftMap { ButtonMap
                {ALVR_INPUT_JOYSTICK_X, ThumbstickPos},
                MapEnd
            },
            RightMap { ButtonMap
                {ALVR_INPUT_JOYSTICK_X, ThumbstickPos},
                MapEnd
            }
        },
        .path = "/interaction_profiles/bytedance/pico_neo3_controller",
        .extensionName = XR_BD_CONTROLLER_INTERACTION_EXTENSION_NAME,
        .quitPath = nullptr,
        .passthroughModes { PassthroughModeButtons {
            .blendMode {
                ALVR_BUTTON_FLAG(ALVR_INPUT_SYSTEM_CLICK),
                ALVR_BUTTON_FLAG(ALVR_INPUT_A_CLICK)
            },
            .maskMode {
                ALVR_BUTTON_FLAG(ALVR_INPUT_SYSTEM_CLICK),
                ALVR_BUTTON_FLAG(ALVR_INPUT_B_CLICK)
            }
        }}
    },
    InteractionProfile {
        .boolMap {
            LeftMap { ButtonMap
                {ALVR_INPUT_SYSTEM_CLICK, MenuClick},
                {ALVR_INPUT_GRIP_CLICK, SqueezeClick},
                {ALVR_INPUT_X_CLICK, XClick},
                {ALVR_INPUT_X_TOUCH, XTouch},
                {ALVR_INPUT_Y_CLICK, YClick},
                {ALVR_INPUT_Y_TOUCH, YTouch},
                {ALVR_INPUT_JOYSTICK_CLICK, ThumbstickClick},
                {ALVR_INPUT_JOYSTICK_TOUCH, ThumbstickTouch},
                {ALVR_INPUT_TRIGGER_CLICK, TriggerClick},
                {ALVR_INPUT_TRIGGER_TOUCH, TriggerTouch},
                MapEnd
            },
            RightMap { ButtonMap
                {ALVR_INPUT_GRIP_CLICK, SqueezeClick},
                {ALVR_INPUT_A_CLICK, AClick},
                {ALVR_INPUT_A_TOUCH, ATouch},
                {ALVR_INPUT_B_CLICK, BClick},
                {ALVR_INPUT_B_TOUCH, BTouch},
                {ALVR_INPUT_JOYSTICK_CLICK, ThumbstickClick},
                {ALVR_INPUT_JOYSTICK_TOUCH, ThumbstickTouch},
                {ALVR_INPUT_TRIGGER_CLICK, TriggerClick},
                {ALVR_INPUT_TRIGGER_TOUCH, TriggerTouch},
                MapEnd
            },
        },
        .scalarMap {
            LeftMap { ButtonMap
                {ALVR_INPUT_GRIP_VALUE, SqueezeValue},
                {ALVR_INPUT_TRIGGER_VALUE, TriggerValue},
                MapEnd
            },
            RightMap { ButtonMap
                {ALVR_INPUT_GRIP_VALUE, SqueezeValue},
                {ALVR_INPUT_TRIGGER_VALUE, TriggerValue},
                MapEnd
            }
        },
        .vector2fMap {
            LeftMap { ButtonMap
                {ALVR_INPUT_JOYSTICK_X, ThumbstickPos},
                MapEnd
            },
            RightMap { ButtonMap
                {ALVR_INPUT_JOYSTICK_X, ThumbstickPos},
                MapEnd
            }
        },
        .path = "/interaction_profiles/bytedance/pico4_controller",
        .extensionName = XR_BD_CONTROLLER_INTERACTION_EXTENSION_NAME,
        .quitPath = nullptr,
        .passthroughModes { PassthroughModeButtons {
            .blendMode {
                ALVR_BUTTON_FLAG(ALVR_INPUT_SYSTEM_CLICK),
                ALVR_BUTTON_FLAG(ALVR_INPUT_A_CLICK)
            },
            .maskMode {
                ALVR_BUTTON_FLAG(ALVR_INPUT_SYSTEM_CLICK),
                ALVR_BUTTON_FLAG(ALVR_INPUT_B_CLICK)
            }
        }}
    },
    InteractionProfile{
        .boolMap {
            LeftMap { ButtonMap
                {ALVR_INPUT_SYSTEM_CLICK, MenuClick},
                {ALVR_INPUT_GRIP_CLICK, SqueezeClick},
                {ALVR_INPUT_X_CLICK, XClick},
                {ALVR_INPUT_X_TOUCH, XTouch},
                {ALVR_INPUT_Y_CLICK, YClick},
                {ALVR_INPUT_Y_TOUCH, YTouch},
                {ALVR_INPUT_JOYSTICK_CLICK, ThumbstickClick},
                {ALVR_INPUT_JOYSTICK_TOUCH, ThumbstickTouch},
                {ALVR_INPUT_TRIGGER_CLICK, TriggerClick},
                {ALVR_INPUT_TRIGGER_TOUCH, TriggerTouch},
                MapEnd
            },
            RightMap { ButtonMap
                {ALVR_INPUT_SYSTEM_CLICK, MenuClick},
                {ALVR_INPUT_GRIP_CLICK, SqueezeClick},
                {ALVR_INPUT_A_CLICK, AClick},
                {ALVR_INPUT_A_TOUCH, ATouch},
                {ALVR_INPUT_B_CLICK, BClick},
                {ALVR_INPUT_B_TOUCH, BTouch},
                {ALVR_INPUT_JOYSTICK_CLICK, ThumbstickClick},
                {ALVR_INPUT_JOYSTICK_TOUCH, ThumbstickTouch},
                {ALVR_INPUT_TRIGGER_CLICK, TriggerClick},
                {ALVR_INPUT_TRIGGER_TOUCH, TriggerTouch},
                MapEnd
            },
        },
        .scalarMap {
            LeftMap { ButtonMap
                {ALVR_INPUT_GRIP_VALUE, SqueezeValue},
                {ALVR_INPUT_TRIGGER_VALUE, TriggerValue},
                MapEnd
            },
            RightMap { ButtonMap
                {ALVR_INPUT_GRIP_VALUE, SqueezeValue},
                {ALVR_INPUT_TRIGGER_VALUE, TriggerValue},
                MapEnd
            }
        },
        .vector2fMap {
            LeftMap { ButtonMap
                {ALVR_INPUT_JOYSTICK_X, ThumbstickPos},
                MapEnd
            },
            RightMap { ButtonMap
                {ALVR_INPUT_JOYSTICK_X, ThumbstickPos},
                MapEnd
            }
        },
        .path = "/interaction_profiles/bytedance/pico4s_controller",
        .extensionName = XR_BD_CONTROLLER_INTERACTION_EXTENSION_NAME,
        .quitPath = nullptr,
        .passthroughModes { PassthroughModeButtons {
            .blendMode {
                ALVR_BUTTON_FLAG(ALVR_INPUT_SYSTEM_CLICK),
                ALVR_BUTTON_FLAG(ALVR_INPUT_A_CLICK)
            },
            .maskMode {
                ALVR_BUTTON_FLAG(ALVR_INPUT_SYSTEM_CLICK),
                ALVR_BUTTON_FLAG(ALVR_INPUT_B_CLICK)
            }
        }}
    },
    InteractionProfile{
        .boolMap {
            LeftMap { ButtonMap
                {ALVR_INPUT_SYSTEM_CLICK, MenuClick},
                {ALVR_INPUT_JOYSTICK_CLICK, ThumbstickClick},
                {ALVR_INPUT_TRACKPAD_CLICK, TrackpadClick},
                {ALVR_INPUT_TRIGGER_CLICK, TriggerClick},
                MapEnd
            },
            RightMap { ButtonMap
                {ALVR_INPUT_SYSTEM_CLICK, MenuClick},
                {ALVR_INPUT_JOYSTICK_CLICK, ThumbstickClick},
                {ALVR_INPUT_TRACKPAD_CLICK, TrackpadClick},
                {ALVR_INPUT_TRIGGER_CLICK, TriggerClick},
                MapEnd
            },
        },
        .scalarMap {
            LeftMap { ButtonMap
                {ALVR_INPUT_TRIGGER_VALUE, TriggerValue},
                MapEnd
            },
            RightMap { ButtonMap
                {ALVR_INPUT_TRIGGER_VALUE, TriggerValue},
                MapEnd
            }
        },
        .vector2fMap {
            LeftMap { ButtonMap
                {ALVR_INPUT_JOYSTICK_X, ThumbstickPos},
                {ALVR_INPUT_TRACKPAD_X, TrackpadPos},
                MapEnd
            },
            RightMap { ButtonMap
                {ALVR_INPUT_JOYSTICK_X, ThumbstickPos},
                {ALVR_INPUT_TRACKPAD_X, TrackpadPos},
                MapEnd
            }
        },
        .path = "/interaction_profiles/bytedance/pico_g3_controller",
        .extensionName = XR_BD_CONTROLLER_INTERACTION_EXTENSION_NAME,
        .quitPath = nullptr,
    },
    InteractionProfile {
        .boolMap {
            LeftMap { ButtonMap
                {ALVR_INPUT_SYSTEM_CLICK, MenuClick},
                {ALVR_INPUT_GRIP_CLICK, SelectClick},                
                MapEnd
            },
            RightMap { ButtonMap
                {ALVR_INPUT_SYSTEM_CLICK, MenuClick},
                {ALVR_INPUT_GRIP_CLICK, SelectClick},                
                MapEnd
            },
        },
        .path = "/interaction_profiles/khr/simple_controller"
    },
    InteractionProfile {
        .boolMap {
            LeftMap { ButtonMap
                {ALVR_INPUT_SYSTEM_CLICK, MenuClick},
                {ALVR_INPUT_X_CLICK, XClick},
                {ALVR_INPUT_X_TOUCH, XTouch},
                {ALVR_INPUT_Y_CLICK, YClick},
                {ALVR_INPUT_Y_TOUCH, YTouch},
                {ALVR_INPUT_JOYSTICK_CLICK, ThumbstickClick},
                {ALVR_INPUT_JOYSTICK_TOUCH, ThumbstickTouch},
                {ALVR_INPUT_TRIGGER_TOUCH, TriggerTouch},
                {ALVR_INPUT_THUMB_REST_TOUCH, ThumbrestTouch},
                MapEnd
            },
            RightMap { ButtonMap
                {ALVR_INPUT_SYSTEM_CLICK, SystemClick},
                {ALVR_INPUT_A_CLICK, AClick},
                {ALVR_INPUT_A_TOUCH, ATouch},
                {ALVR_INPUT_B_CLICK, BClick},
                {ALVR_INPUT_B_TOUCH, BTouch},
                {ALVR_INPUT_JOYSTICK_CLICK, ThumbstickClick},
                {ALVR_INPUT_JOYSTICK_TOUCH, ThumbstickTouch},
                {ALVR_INPUT_TRIGGER_TOUCH, TriggerTouch},
                {ALVR_INPUT_THUMB_REST_TOUCH, ThumbrestTouch},
                MapEnd
            },
        },
        .scalarMap {
            LeftMap { ButtonMap
                {ALVR_INPUT_GRIP_VALUE, SqueezeValue},
                {ALVR_INPUT_TRIGGER_VALUE, TriggerValue},
                MapEnd
            },
            RightMap { ButtonMap
                {ALVR_INPUT_GRIP_VALUE, SqueezeValue},
                {ALVR_INPUT_TRIGGER_VALUE, TriggerValue},
                MapEnd
            }
        },
        .vector2fMap {
            LeftMap { ButtonMap
                {ALVR_INPUT_JOYSTICK_X, ThumbstickPos},
                MapEnd
            },
            RightMap { ButtonMap
                {ALVR_INPUT_JOYSTICK_X, ThumbstickPos},
                MapEnd
            }
        },
        .scalarToBoolMap {
            LeftMap { ButtonMap
                {ALVR_INPUT_GRIP_CLICK, SqueezeValue},
                {ALVR_INPUT_TRIGGER_CLICK, TriggerValue},
                MapEnd
            },
            RightMap { ButtonMap
                {ALVR_INPUT_GRIP_CLICK, SqueezeValue},
                {ALVR_INPUT_TRIGGER_CLICK, TriggerValue},
                MapEnd
            }
        },
        .path = "/interaction_profiles/oculus/touch_controller",
        .quitPath = nullptr,
        .passthroughModes { PassthroughModeButtons {
            .blendMode {
                ALVR_BUTTON_FLAG(ALVR_INPUT_SYSTEM_CLICK),
                ALVR_BUTTON_FLAG(ALVR_INPUT_A_CLICK)
            },
            .maskMode {
                ALVR_BUTTON_FLAG(ALVR_INPUT_SYSTEM_CLICK),
                ALVR_BUTTON_FLAG(ALVR_INPUT_B_CLICK)
            }
        }}
    },
    InteractionProfile {
        .boolMap {
            LeftMap { ButtonMap
                {ALVR_INPUT_SYSTEM_CLICK, MenuClick},
                {ALVR_INPUT_X_CLICK, XClick},
                {ALVR_INPUT_X_TOUCH, XTouch},
                {ALVR_INPUT_Y_CLICK, YClick},
                {ALVR_INPUT_Y_TOUCH, YTouch},
                {ALVR_INPUT_JOYSTICK_CLICK, ThumbstickClick},
                {ALVR_INPUT_JOYSTICK_TOUCH, ThumbstickTouch},
                {ALVR_INPUT_TRIGGER_TOUCH, TriggerTouch},
                {ALVR_INPUT_THUMB_REST_TOUCH, ThumbrestTouch},
                MapEnd
            },
            RightMap { ButtonMap
                {ALVR_INPUT_SYSTEM_CLICK, SystemClick},
                {ALVR_INPUT_A_CLICK, AClick},
                {ALVR_INPUT_A_TOUCH, ATouch},
                {ALVR_INPUT_B_CLICK, BClick},
                {ALVR_INPUT_B_TOUCH, BTouch},
                {ALVR_INPUT_JOYSTICK_CLICK, ThumbstickClick},
                {ALVR_INPUT_JOYSTICK_TOUCH, ThumbstickTouch},
                {ALVR_INPUT_TRIGGER_TOUCH, TriggerTouch},
                {ALVR_INPUT_THUMB_REST_TOUCH, ThumbrestTouch},
                MapEnd
            },
        },
        .scalarMap {
            LeftMap { ButtonMap
                {ALVR_INPUT_GRIP_VALUE, SqueezeValue},
                {ALVR_INPUT_TRIGGER_VALUE, TriggerValue},
                MapEnd
            },
            RightMap { ButtonMap
                {ALVR_INPUT_GRIP_VALUE, SqueezeValue},
                {ALVR_INPUT_TRIGGER_VALUE, TriggerValue},
                MapEnd
            }
        },
        .vector2fMap {
            LeftMap { ButtonMap
                {ALVR_INPUT_JOYSTICK_X, ThumbstickPos},
                MapEnd
            },
            RightMap { ButtonMap
                {ALVR_INPUT_JOYSTICK_X, ThumbstickPos},
                MapEnd
            }
        },
        .scalarToBoolMap {
            LeftMap { ButtonMap
                {ALVR_INPUT_GRIP_CLICK, SqueezeValue},
                {ALVR_INPUT_TRIGGER_CLICK, TriggerValue},
                MapEnd
            },
            RightMap { ButtonMap
                {ALVR_INPUT_GRIP_CLICK, SqueezeValue},
                {ALVR_INPUT_TRIGGER_CLICK, TriggerValue},
                MapEnd
            }
        },
        .path = "/interaction_profiles/facebook/touch_controller_pro",
        .extensionName = XR_FB_TOUCH_CONTROLLER_PRO_EXTENSION_NAME,
        .quitPath = nullptr,
        .passthroughModes { PassthroughModeButtons {
            .blendMode {
                ALVR_BUTTON_FLAG(ALVR_INPUT_SYSTEM_CLICK),
                ALVR_BUTTON_FLAG(ALVR_INPUT_A_CLICK)
            },
            .maskMode {
                ALVR_BUTTON_FLAG(ALVR_INPUT_SYSTEM_CLICK),
                ALVR_BUTTON_FLAG(ALVR_INPUT_B_CLICK)
            }
        }}
    },
    InteractionProfile {
        .boolMap {
            LeftMap { ButtonMap
                {ALVR_INPUT_SYSTEM_CLICK, MenuClick},
                {ALVR_INPUT_JOYSTICK_CLICK, TrackpadClick},
                {ALVR_INPUT_JOYSTICK_TOUCH, TrackpadTouch},
                {ALVR_INPUT_TRIGGER_CLICK, TriggerClick},
                MapEnd
            },
            RightMap { ButtonMap
                {ALVR_INPUT_SYSTEM_CLICK, MenuClick},
                {ALVR_INPUT_JOYSTICK_CLICK, TrackpadClick},
                {ALVR_INPUT_JOYSTICK_TOUCH, TrackpadTouch},
                {ALVR_INPUT_TRIGGER_CLICK, TriggerClick},
                MapEnd
            },
        },
        .scalarMap {
            LeftMap { ButtonMap
                {ALVR_INPUT_TRIGGER_VALUE, TriggerValue},
                MapEnd
            },
            RightMap { ButtonMap
                {ALVR_INPUT_TRIGGER_VALUE, TriggerValue},
                MapEnd
            }
        },
        .vector2fMap {
            LeftMap { ButtonMap
                {ALVR_INPUT_TRACKPAD_X, TrackpadPos},
                MapEnd
            },
            RightMap { ButtonMap
                {ALVR_INPUT_TRACKPAD_X, TrackpadPos},
                MapEnd
            }
        },
        .path = "/interaction_profiles/htc/vive_controller"
    },
    InteractionProfile {
        .boolMap {
            LeftMap { ButtonMap
                //{ALVR_INPUT_SYSTEM_CLICK, SystemClick},
                {ALVR_INPUT_A_CLICK, AClick},
                {ALVR_INPUT_A_TOUCH, ATouch},
                {ALVR_INPUT_B_CLICK, BClick},
                {ALVR_INPUT_B_TOUCH, BTouch},
                {ALVR_INPUT_JOYSTICK_CLICK, ThumbstickClick},
                {ALVR_INPUT_JOYSTICK_TOUCH, ThumbstickTouch},
                {ALVR_INPUT_TRIGGER_CLICK, TriggerClick},
                {ALVR_INPUT_TRIGGER_TOUCH, TriggerTouch},
                {ALVR_INPUT_TRACKPAD_TOUCH, TrackpadTouch},
                MapEnd
            },
            RightMap { ButtonMap
                //{ALVR_INPUT_SYSTEM_CLICK, SystemClick},
                {ALVR_INPUT_A_CLICK, AClick},
                {ALVR_INPUT_A_TOUCH, ATouch},
                {ALVR_INPUT_B_CLICK, BClick},
                {ALVR_INPUT_B_TOUCH, BTouch},
                {ALVR_INPUT_JOYSTICK_CLICK, ThumbstickClick},
                {ALVR_INPUT_JOYSTICK_TOUCH, ThumbstickTouch},
                {ALVR_INPUT_TRIGGER_CLICK, TriggerClick},
                {ALVR_INPUT_TRIGGER_TOUCH, TriggerTouch},
                {ALVR_INPUT_TRACKPAD_TOUCH, TrackpadTouch},
                MapEnd
            },
        },
        .scalarMap {
            LeftMap { ButtonMap
                {ALVR_INPUT_GRIP_VALUE, SqueezeValue},
                {ALVR_INPUT_TRIGGER_VALUE, TriggerValue},
                MapEnd
            },
            RightMap { ButtonMap
                {ALVR_INPUT_GRIP_VALUE, SqueezeValue},
                {ALVR_INPUT_TRIGGER_VALUE, TriggerValue},
                MapEnd
            }
        },
        .vector2fMap {
            LeftMap { ButtonMap
                {ALVR_INPUT_JOYSTICK_X, ThumbstickPos},
                {ALVR_INPUT_TRACKPAD_X, TrackpadPos},
                MapEnd
            },
            RightMap { ButtonMap
                {ALVR_INPUT_JOYSTICK_X, ThumbstickPos},
                {ALVR_INPUT_TRACKPAD_X, TrackpadPos},
                MapEnd
            }
        },
        .path = "/interaction_profiles/valve/index_controller",
        .quitPath = ThumbstickClick
    },
    InteractionProfile {
        .boolMap {
            LeftMap { ButtonMap
                {ALVR_INPUT_APPLICATION_MENU_CLICK, MenuClick},
                {ALVR_INPUT_GRIP_CLICK, SqueezeClick},
                {ALVR_INPUT_JOYSTICK_CLICK, ThumbstickClick},
                {ALVR_INPUT_TRACKPAD_CLICK, TrackpadClick},
                {ALVR_INPUT_TRACKPAD_TOUCH, TrackpadTouch},
                MapEnd
            },
            RightMap { ButtonMap
                {ALVR_INPUT_SYSTEM_CLICK, MenuClick},
                {ALVR_INPUT_GRIP_CLICK, SqueezeClick},
                {ALVR_INPUT_JOYSTICK_CLICK, ThumbstickClick},
                {ALVR_INPUT_TRACKPAD_CLICK, TrackpadClick},
                {ALVR_INPUT_TRACKPAD_TOUCH, TrackpadTouch},
                MapEnd
            },
        },
        .scalarMap {
            LeftMap { ButtonMap
                {ALVR_INPUT_TRIGGER_VALUE, TriggerValue},
                MapEnd
            },
            RightMap { ButtonMap
                {ALVR_INPUT_TRIGGER_VALUE, TriggerValue},
                MapEnd
            }
        },
        .vector2fMap {
            LeftMap { ButtonMap
                {ALVR_INPUT_JOYSTICK_X, ThumbstickPos},
                {ALVR_INPUT_TRACKPAD_X, TrackpadPos},
                MapEnd
            },
            RightMap { ButtonMap
                {ALVR_INPUT_JOYSTICK_X, ThumbstickPos},
                {ALVR_INPUT_TRACKPAD_X, TrackpadPos},
                MapEnd
            }
        },
        .boolToScalarMap {
            LeftMap { ButtonMap
                {ALVR_INPUT_GRIP_VALUE, SqueezeClick},
                MapEnd
            },
            RightMap { ButtonMap
                {ALVR_INPUT_GRIP_VALUE, SqueezeClick},
                MapEnd
            }
        },
        .path = "/interaction_profiles/microsoft/motion_controller"
    },
    InteractionProfile {
        .boolMap {
            LeftMap { ButtonMap
                {ALVR_INPUT_SYSTEM_CLICK, MenuClick},
                {ALVR_INPUT_GRIP_CLICK, SqueezeClick},
                {ALVR_INPUT_X_CLICK, XClick},
                {ALVR_INPUT_Y_CLICK, YClick},
                {ALVR_INPUT_JOYSTICK_CLICK, ThumbstickClick},
                {ALVR_INPUT_JOYSTICK_TOUCH, ThumbstickTouch},
                {ALVR_INPUT_TRIGGER_CLICK, TriggerClick},
                MapEnd
            },
            RightMap { ButtonMap
                //{ALVR_INPUT_SYSTEM_CLICK, SystemClick},
                {ALVR_INPUT_GRIP_CLICK, SqueezeClick},
                {ALVR_INPUT_A_CLICK, AClick},
                {ALVR_INPUT_B_CLICK, BClick},
                {ALVR_INPUT_JOYSTICK_CLICK, ThumbstickClick},
                {ALVR_INPUT_JOYSTICK_TOUCH, ThumbstickTouch},
                {ALVR_INPUT_TRIGGER_CLICK, TriggerClick},
                MapEnd
            },
        },
        .scalarMap {
            LeftMap { ButtonMap
                {ALVR_INPUT_TRIGGER_VALUE, TriggerValue},
                {ALVR_INPUT_JOYSTICK_X, ThumbstickX},
                {ALVR_INPUT_JOYSTICK_Y, ThumbstickY},
                MapEnd
            },
            RightMap { ButtonMap
                {ALVR_INPUT_TRIGGER_VALUE, TriggerValue},
                {ALVR_INPUT_JOYSTICK_X, ThumbstickX},
                {ALVR_INPUT_JOYSTICK_Y, ThumbstickY},
                MapEnd
            }
        },
        .path = "/interaction_profiles/htc/vive_cosmos_controller",
        .extensionName = XR_HTC_VIVE_COSMOS_CONTROLLER_INTERACTION_EXTENSION_NAME
    },
    InteractionProfile {
        .boolMap {
            LeftMap { ButtonMap
                {ALVR_INPUT_SYSTEM_CLICK, MenuClick},
                {ALVR_INPUT_GRIP_CLICK, SqueezeClick},
                {ALVR_INPUT_GRIP_TOUCH, SqueezeTouch},
                {ALVR_INPUT_X_CLICK, XClick},
                {ALVR_INPUT_Y_CLICK, YClick},
                {ALVR_INPUT_JOYSTICK_CLICK, ThumbstickClick},
                {ALVR_INPUT_JOYSTICK_TOUCH, ThumbstickTouch},
                {ALVR_INPUT_TRIGGER_CLICK, TriggerClick},
                {ALVR_INPUT_TRIGGER_TOUCH, TriggerTouch},
                {ALVR_INPUT_THUMB_REST_TOUCH, ThumbrestTouch},
                MapEnd
            },
            RightMap { ButtonMap
                //{ALVR_INPUT_SYSTEM_CLICK, SystemClick},
                {ALVR_INPUT_GRIP_CLICK, SqueezeClick},
                {ALVR_INPUT_GRIP_TOUCH, SqueezeTouch},
                {ALVR_INPUT_A_CLICK, AClick},
                {ALVR_INPUT_B_CLICK, BClick},
                {ALVR_INPUT_JOYSTICK_CLICK, ThumbstickClick},
                {ALVR_INPUT_JOYSTICK_TOUCH, ThumbstickTouch},
                {ALVR_INPUT_TRIGGER_CLICK, TriggerClick},
                {ALVR_INPUT_TRIGGER_TOUCH, TriggerTouch},
                {ALVR_INPUT_THUMB_REST_TOUCH, ThumbrestTouch},
                MapEnd
            },
        },
        .scalarMap {
            LeftMap { ButtonMap
                {ALVR_INPUT_GRIP_VALUE, SqueezeValue},
                {ALVR_INPUT_TRIGGER_VALUE, TriggerValue},
                {ALVR_INPUT_JOYSTICK_X, ThumbstickX},
                {ALVR_INPUT_JOYSTICK_Y, ThumbstickY},
                MapEnd
            },
            RightMap { ButtonMap
                {ALVR_INPUT_GRIP_VALUE, SqueezeValue},
                {ALVR_INPUT_TRIGGER_VALUE, TriggerValue},
                {ALVR_INPUT_JOYSTICK_X, ThumbstickX},
                {ALVR_INPUT_JOYSTICK_Y, ThumbstickY},
                MapEnd
            }
        },
        .path = "/interaction_profiles/htc/vive_focus3_controller",
        .extensionName = XR_HTC_VIVE_FOCUS3_CONTROLLER_INTERACTION_EXTENSION_NAME,
        .quitPath = nullptr,
        .passthroughModes { PassthroughModeButtons {
            .blendMode {
                ALVR_BUTTON_FLAG(ALVR_INPUT_SYSTEM_CLICK),
                ALVR_BUTTON_FLAG(ALVR_INPUT_A_CLICK)
            },
            .maskMode {
                ALVR_BUTTON_FLAG(ALVR_INPUT_SYSTEM_CLICK),
                ALVR_BUTTON_FLAG(ALVR_INPUT_B_CLICK)
            }
        }}
    },
    InteractionProfile{
        .boolMap {
            LeftMap { ButtonMap
                {ALVR_INPUT_SYSTEM_CLICK,   MenuClick},
                {ALVR_INPUT_GRIP_CLICK,     ShoulderClick},
                {ALVR_INPUT_TRIGGER_CLICK,  TriggerClick},
                {ALVR_INPUT_TRACKPAD_CLICK, TrackpadClick},
                {ALVR_INPUT_TRACKPAD_TOUCH, TrackpadTouch},
                MapEnd
            },
            RightMap { ButtonMap
                {ALVR_INPUT_SYSTEM_CLICK,   MenuClick},
                {ALVR_INPUT_GRIP_CLICK,     ShoulderClick},
                {ALVR_INPUT_TRIGGER_CLICK,  TriggerClick},
                {ALVR_INPUT_TRACKPAD_CLICK, TrackpadClick},
                {ALVR_INPUT_TRACKPAD_TOUCH, TrackpadTouch},
                MapEnd
            },
        },
        .scalarMap {
            LeftMap { ButtonMap
                {ALVR_INPUT_TRIGGER_VALUE, TriggerValue},
                MapEnd
            },
            RightMap { ButtonMap
                {ALVR_INPUT_TRIGGER_VALUE, TriggerValue},
                MapEnd
            },
        },
        .vector2fMap {
            LeftMap { ButtonMap
                {ALVR_INPUT_TRACKPAD_X, TrackpadPos},
                MapEnd
            },
            RightMap { ButtonMap
                {ALVR_INPUT_TRACKPAD_X, TrackpadPos},
                MapEnd
            }
        },
        .path = "/interaction_profiles/ml/ml2_controller",
        .extensionName = XR_ML_ML2_CONTROLLER_INTERACTION_EXTENSION_NAME,
        .quitPath = nullptr,
    },
};
}
#endif
