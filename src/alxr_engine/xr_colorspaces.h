#pragma once
#ifndef AXLR_XR_COLORSPACS_H
#define AXLR_XR_COLORSPACS_H

#include <optional>
#include <cmath>
#include "xr_eigen.h"

namespace ALXR {;

enum class YcbcrFormat : std::uint32_t {
    Unknown = 0,
    YUV420P,
    NV12 = YUV420P,
    YUV420P10LE,
    P010LE = YUV420P10LE,
    G8_B8_R8_3PLANE_420,
    G10X6_B10X6_R10X6_3PLANE_420
};

// equivalent to VkSamplerYcbcrModelConversion
enum class YcbcrModel : std::uint32_t {
    RGB_Identity = 0,
    Identity = 1,
    BT709 = 2,
    BT601 = 3,
    BT2020 = 4,
};

// equivalent to VkSamplerYcbcrRange
enum class YcbcrRange : std::uint32_t {
    ITU_Full = 0,
    ITU_Narrow = 1,
};

struct YcbcrDequantizationParams final {
    Eigen::Vector3f scales;
    Eigen::Vector3f offsets;
};

// https://registry.khronos.org/DataFormat/specs/1.4/dataformat.1.4.inline.html#MODEL_BT601
inline const Eigen::Matrix3f Bt601YcbcrToNonLinearSRGB {
    { 1.0f,                   0.0f,                       1.402f     },
    { 1.0f,        -(0.202008f / 0.587f),      -(0.419198f / 0.587f) },
    { 1.0f,                   1.772f,                     0.0f       }
};

// https://registry.khronos.org/DataFormat/specs/1.4/dataformat.1.4.inline.html#MODEL_BT709
inline const Eigen::Matrix3f Bt709YcbcrToNonLinearSRGB {
    { 1.0f,                   0.0f,                       1.5748f     },
    { 1.0f,      -(0.13397432f / 0.7152f),   -(0.33480248f / 0.7152f) },
    { 1.0f,                   1.8556f,                    0.0f        }
};

// https://registry.khronos.org/DataFormat/specs/1.4/dataformat.1.4.inline.html#MODEL_BT2020
inline const Eigen::Matrix3f Bt2020YcbcrToNonLinearSRGB {
    { 1.0f,                   0.0f,                       1.4746f     },
    { 1.0f,      -(0.11156702f / 0.6780f),   -(0.38737742f / 0.6780f) },
    { 1.0f,                   1.8814f,                    0.0f        }
};

constexpr inline std::size_t YcbcrPlaneCount(const YcbcrFormat f) {
    switch (f) {
    case YcbcrFormat::NV12:
    case YcbcrFormat::P010LE:
        return 2;
    case YcbcrFormat::G8_B8_R8_3PLANE_420:
    case YcbcrFormat::G10X6_B10X6_R10X6_3PLANE_420:
        return 3;
    default: return 0;
    }
}

constexpr inline std::uint8_t YcbcrBitDepth(const YcbcrFormat f) {
    switch (f) {
    case YcbcrFormat::NV12: // == YUV420P
    case YcbcrFormat::G8_B8_R8_3PLANE_420:
        return 8;
    case YcbcrFormat::P010LE: // == YUV420P10LE
    case YcbcrFormat::G10X6_B10X6_R10X6_3PLANE_420:
        return 10;
    default:
        return 0;
    }
}

constexpr inline std::uint64_t Pow2(const std::uint64_t ep) {
    //static_assert(ep < 64, "Exponent must be less than 64");
    return 1ULL << ep;
}

// Section 16.1. https://registry.khronos.org/DataFormat/specs/1.4/dataformat.1.4.html#QUANTIZATION_NARROW
inline YcbcrDequantizationParams YcbcrDequantizeNarrowUNormParams(const std::uint8_t bitDepth) {
    //static_assert(bitDepth >= 8, "bitDepth must >= 8 bits");
        
    const auto bn = static_cast<const float>(Pow2(bitDepth));
    const auto bn8 = static_cast<const float>(Pow2(bitDepth - 8));

    // Y'norm
    const float yScale  = (bn - 1.f) / (219.f * bn8);
    const float yOffset = -(16.f * bn8) / (219.f * bn8);

    // DC'b-norm / DC'r-norm
    const float cScale = (bn - 1.f) / (224.f * bn8);
    const float cOffset = -(128.f * bn8) / (224.f * bn8);

    return {
        .scales  { yScale,  cScale,  cScale  },
        .offsets { yOffset, cOffset, cOffset },
    };
}

// https://registry.khronos.org/DataFormat/specs/1.4/dataformat.1.4.html#QUANTIZATION_FULL
inline YcbcrDequantizationParams YcbcrDequantizeFullUNormParams(const std::uint8_t bitDepth) {
    //static_assert(bitDepth >= 8, "bitDepth must >= 8 bits");

    const auto bn = static_cast<const float>(Pow2(bitDepth));
    const auto bn1 = static_cast<const float>(Pow2(bitDepth - 1));

    // Y'norm
    const float yScale = 1.f;
    const float yOffset = 0.f;

    // DC'b-norm / DC'r-norm
    const float cScale = 1.f;
    const float cOffset = -bn1 / (bn - 1.f);

    return {
        .scales  { yScale,  cScale,  cScale  },
        .offsets { yOffset, cOffset, cOffset },
    };
}

inline YcbcrDequantizationParams YcbcrDequantizeUNormParams(const YcbcrRange ycbcrRange, const std::uint8_t bitDepth) {
    switch (ycbcrRange) {
    case YcbcrRange::ITU_Full:
        return YcbcrDequantizeFullUNormParams(bitDepth);
    case YcbcrRange::ITU_Narrow:
    default:
        return YcbcrDequantizeNarrowUNormParams(bitDepth);
    }
}

inline Eigen::Matrix4f CombineYcbcrDequantizeAndColorMatrix(
    const Eigen::Matrix3f& colorMat,
    const YcbcrDequantizationParams& dequanParams
) {
    // Scale each column of the color matrix by corresponding scale factor
    const Eigen::Matrix3f scaledColorMat = colorMat * dequanParams.scales.asDiagonal();
    // Calculate translation vector: colorMat * offsets
    const Eigen::Vector3f translation = colorMat * dequanParams.offsets;
    // Construct 4x4 affine transformation matrix
    Eigen::Matrix4f affineMat = Eigen::Matrix4f::Identity();
    affineMat.block<3, 3>(0, 0) = scaledColorMat;
    affineMat.block<3, 1>(0, 3) = translation;
    return affineMat;
}

inline std::optional<Eigen::Matrix4f> MakeYcbcrDequantizeColorMatrix(
    const YcbcrFormat ycbcrFormat,
    const YcbcrModel ycbcrModel,
    const YcbcrRange ycbcrRange
) {
    const std::uint8_t bitDepth = YcbcrBitDepth(ycbcrFormat);
    if (bitDepth == 0)
        return std::nullopt;
    const auto ycbcrDequantParams = YcbcrDequantizeUNormParams(ycbcrRange, bitDepth);
    switch (ycbcrModel) {
    case YcbcrModel::BT601:
        return CombineYcbcrDequantizeAndColorMatrix(
            Bt601YcbcrToNonLinearSRGB,
            ycbcrDequantParams
        );
    case YcbcrModel::BT709:
        return CombineYcbcrDequantizeAndColorMatrix(
            Bt709YcbcrToNonLinearSRGB,
            ycbcrDequantParams
        );
    case YcbcrModel::BT2020:
        return CombineYcbcrDequantizeAndColorMatrix(
            Bt2020YcbcrToNonLinearSRGB,
            ycbcrDequantParams
        );
    default: return std::nullopt;
    }
}
}
#endif
