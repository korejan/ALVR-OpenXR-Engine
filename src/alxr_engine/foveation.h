#pragma once
#ifndef ALXR_FOVEATION_H
#define ALXR_FOVEATION_H
#include <type_traits>
#include <cstdint>
#include <openxr/openxr.h>
#include "alxr_ctypes.h"

namespace ALXR {
    struct alignas(16) FoveatedDecodeParams {
        XrVector2f eyeSizeRatio; // encoded content size / encoded eye size (rounded up to a multiple of 32)
        XrVector2f edgeRatio;    // average compression of the edge bands
        XrVector2f loBound;      // fovea bounds in eye UV
        XrVector2f hiBound;
        XrVector2f encLoBound;   // fovea bounds in encoded content UV
        XrVector2f encHiBound;
        XrVector2f centerScale;  // eye UV per encoded content UV inside the fovea
        XrVector2f encHalfTexel; // half an encoded texel in encoded eye UV
    };
    static_assert(std::is_standard_layout<FoveatedDecodeParams>());

    // Foveated layout of one axis in whole encoded pixels: a 1:1 fovea between two edge bands.
    // Each block of 2 * edgeRatio source pixels in a band becomes 2 encoded pixels.
    struct FoveationAxisLayout {
        float  eyeSize;            // unfoveated eye size, pixels
        float  edgeRatio;
        double loBand;             // encoded pixels before the fovea
        double hiBand;             // encoded pixels after the fovea
        double encodedSize;        // encoded content size, pixels
        std::uint32_t alignedSize; // encoded eye size, rounded up to a multiple of 32
    };

    FoveationAxisLayout MakeFoveationAxisLayout
    (
        const float eyeSize,
        const float centerSize,
        const float centerShift,
        const float edgeRatio
    );

    struct FoveationAxisParams {
        float eyeSizeRatio, edgeRatio, loBound, hiBound, encLoBound, encHiBound, centerScale;
    };

    inline FoveationAxisParams MakeFoveationAxisParams(const FoveationAxisLayout& l) {
        return {
            .eyeSizeRatio = static_cast<float>(l.encodedSize / l.alignedSize),
            .edgeRatio    = l.edgeRatio,
            .loBound      = static_cast<float>(l.loBand * l.edgeRatio / l.eyeSize),
            .hiBound      = static_cast<float>(1. - l.hiBand * l.edgeRatio / l.eyeSize),
            .encLoBound   = static_cast<float>(l.loBand / l.encodedSize),
            .encHiBound   = static_cast<float>(1. - l.hiBand / l.encodedSize),
            .centerScale  = static_cast<float>(l.encodedSize / l.eyeSize),
        };
    }

    inline FoveatedDecodeParams MakeFoveatedDecodeParams
    (
        const XrVector2f& targetEyeSize,
        const XrVector2f& centerSize,
        const XrVector2f& centerShift,
        const XrVector2f& edgeRatio
    )
    {
        const auto layoutX = MakeFoveationAxisLayout(targetEyeSize.x, centerSize.x, centerShift.x, edgeRatio.x);
        const auto layoutY = MakeFoveationAxisLayout(targetEyeSize.y, centerSize.y, centerShift.y, edgeRatio.y);
        const auto x = MakeFoveationAxisParams(layoutX);
        const auto y = MakeFoveationAxisParams(layoutY);
        return {
            .eyeSizeRatio { x.eyeSizeRatio, y.eyeSizeRatio },
            .edgeRatio    { x.edgeRatio,    y.edgeRatio    },
            .loBound      { x.loBound,      y.loBound      },
            .hiBound      { x.hiBound,      y.hiBound      },
            .encLoBound   { x.encLoBound,   y.encLoBound   },
            .encHiBound   { x.encHiBound,   y.encHiBound   },
            .centerScale  { x.centerScale,  y.centerScale  },
            .encHalfTexel { 0.5f / layoutX.alignedSize, 0.5f / layoutY.alignedSize },
        };
    }

    inline FoveatedDecodeParams MakeFoveatedDecodeParams(const ALXRRenderConfig& rc) {
        return MakeFoveatedDecodeParams
        (
            XrVector2f{ float(rc.eyeWidth), float(rc.eyeHeight) },
            XrVector2f{ rc.foveationCenterSizeX,  rc.foveationCenterSizeY  },
            XrVector2f{ rc.foveationCenterShiftX, rc.foveationCenterShiftY },
            XrVector2f{ rc.foveationEdgeRatioX,   rc.foveationEdgeRatioY   }
        );
    }
}
#endif
