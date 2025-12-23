#pragma once
#ifndef ALXR_NAL_UTILS_H
#define ALXR_NAL_UTILS_H

#include <span>
#include "packet_types.h"
#include "alxr_ctypes.h"

enum class NalType : std::uint8_t
{
    P = 1,
    IDR = 5,
    SPS = 7,
    HEVC_IDR_W_RADL = 19,
    HEVC_VPS = 32,
    Unknown = 0xFF
};
constexpr inline bool is_config(const NalType t, const ALVR_CODEC codec) {
    switch (codec) {
    case ALVR_CODEC_H264: return t == NalType::SPS;
    case ALVR_CODEC_H265: return t == NalType::HEVC_VPS;
    }
    return false;
}
constexpr inline bool is_idr(const NalType t, const ALVR_CODEC codec) {
    switch (codec) {
    case ALVR_CODEC_H264: return t == NalType::IDR;
    case ALVR_CODEC_H265: return t == NalType::HEVC_IDR_W_RADL;
    }
    return false;
}

using PacketType = std::span<std::uint8_t>;
using ConstPacketType = std::span<const std::uint8_t>;

constexpr inline NalType get_nal_type(const ConstPacketType& packet, const ALVR_CODEC codec)
{
    if (packet.size() < 5) return NalType::Unknown;
    return NalType(codec == ALVR_CODEC_H264 ?
        (packet[4] & std::uint8_t(0x1F)) :
        ((packet[4] >> 1) & std::uint8_t(0x3F)));
}

constexpr inline NalType get_nal_type(const ConstPacketType& packet, const ALXRCodecType codec) {
	return get_nal_type(packet, static_cast<ALVR_CODEC>(codec));
}

constexpr inline bool is_config(const ConstPacketType& packet, const ALVR_CODEC codec) {
    return is_config(get_nal_type(packet, codec), codec);
}

constexpr inline bool is_config(const ConstPacketType& packet, const ALXRCodecType codec) {
	return is_config(packet, static_cast<ALVR_CODEC>(codec));
}

constexpr inline bool is_idr(const ConstPacketType& packet, const ALVR_CODEC codec) {
    return is_idr(get_nal_type(packet, codec), codec);
}

constexpr inline bool is_idr(const ConstPacketType& packet, const ALXRCodecType codec) {
	return is_idr(packet, static_cast<ALVR_CODEC>(codec));
}

// This frame contains (VPS + )SPS + PPS + IDR on NVENC H.264 (H.265) stream.
 // (VPS + )SPS + PPS has short size (8bytes + 28bytes in some environment), so we can assume SPS + PPS is contained in first fragment.
inline ConstPacketType find_vpssps(const ConstPacketType& packet, const ALVR_CODEC codec)
{
    if (!is_config(packet, codec))
        return PacketType{};

    const std::size_t nalCount = [&]() -> std::size_t
    {
        switch (codec)
        {
        case ALVR_CODEC_H264: return 3;
        case ALVR_CODEC_H265: return 4;
        }
        return std::size_t(-1);
    }();
    std::size_t zeroes = 0, foundNals = 0;
    for (std::size_t i = 0; i < packet.size(); ++i)
    {
        if (packet[i] == 0)
            ++zeroes;
        else if (packet[i] == 1)
        {
            if (zeroes >= 2)
            {
                ++foundNals;
                if (foundNals >= nalCount)
                    return packet.subspan(0, i - 3);
            }
            zeroes = 0;
        }
        else
            zeroes = 0;
    }
    return PacketType{};
}

inline ConstPacketType find_vpssps(const ConstPacketType& packet, const ALXRCodecType codec) {
    return find_vpssps(packet, static_cast<ALVR_CODEC>(codec));
}

#endif
