// Foveated decode: the inverse of the server's encoder (FoveatedRendering.hlsli on the server).
// For a display pixel at eye UV x, find where its content sits in the encoded video frame.
// x and y are decoded independently, each with the 1-D mapping below.
//
//   One display eye, in eye UV. LoBound and HiBound split each axis into an edge band, the
//   fovea and a second edge band. The edge bands are squeezed along their axis, the corners
//   along both (sizes here are illustrative; centerShift moves the fovea):
//
//             x=0         LoBound.x                          HiBound.x         x=1
//          y=0 +--------------+----------------------------------+--------------+
//              |    corner    |             top band             |    corner    |
//              | x+y squeezed |            y squeezed            | x+y squeezed |
//    LoBound.y +--------------+----------------------------------+--------------+
//              |              |                                  |              |
//              |   lo band    |              FOVEA               |   hi band    |
//              |  x squeezed  |           copied 1:1,            |  x squeezed  |
//              |  (temporal)  |         pixel for pixel          |   (nasal)    |
//              |              |                                  |              |
//    HiBound.y +--------------+----------------------------------+--------------+
//              |    corner    |           bottom band            |    corner    |
//          y=1 +--------------+----------------------------------+--------------+
//
//   The encoded video frame packs both eyes side by side. Edge bands shrink to 1/r of their
//   size on average, the fovea keeps its size. The right eye is mirrored, so the nasal sides
//   (eye x = 1) meet in the middle. Each eye is padded to a multiple of 32 pixels (::):
//
//   texture x   0                                 0.5                                  1
//               +--+------------------------+--+::::::::+--+------------------------+--+
//               |  |        top band        |  |::::::::|  |        top band        |  |
//               +--+------------------------+--+::::::::+--+------------------------+--+
//               |lo|                        |hi|::::::::|hi|                        |lo|
//               |  |         FOVEA          |  |::::::::|  |         FOVEA          |  |
//               |  |  same size in pixels   |  |::::::::|  |  same size in pixels   |  |
//               +--+------------------------+--+::::::::+--+------------------------+--+
//               |  |      bottom band       |  |::::::::|  |      bottom band       |  |
//               +--+------------------------+--+::::::::+--+------------------------+--+
//               :::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::: <- padding rows
//               |<------------ left eye ----------->|<----- right eye (mirrored) ----->|
//                  ^                        ^  ^
//                  EncLoBound.x
//                                           EncHiBound.x
//                                              EyeSizeRatio.x  (end of content)
//
//   Edge bands invert the encoder's EdgeToSource(t) = t * ((2r - 1) - (r - 1) * t) / r, r = EdgeRatio:
//     SourceToEdge(X) = 2rX / ((2r - 1) + sqrt((2r - 1)^2 - 4r(r - 1)X))
//   Fovea:    u = EncLoBound + (x - LoBound) / CenterScale    (1 display pixel = 1 encoded pixel)
//
// See ALXR::FoveatedDecodeParams (foveation.h).
cbuffer FoveationParams : register(b3) {
    float2 EyeSizeRatio;
    float2 EdgeRatio;
    float2 LoBound;
    float2 HiBound;
    float2 EncLoBound;
    float2 EncHiBound;
    float2 CenterScale;
    float2 EncHalfTexel;
};

float2 TextureToEyeUV(const float2 textureUV, const float isRightEye) {
    // flip distortion horizontally for right eye
    // left: x * 2; right: (1 - x) * 2
    return float2((textureUV.x + isRightEye * (1. - 2. * textureUV.x)) * 2., textureUV.y);
}

float2 EyeToTextureUV(const float2 eyeUV, const float isRightEye) {
    // left: x / 2; right 1 - (x / 2)
    return float2(eyeUV.x * 0.5 + isRightEye * (1. - eyeUV.x), eyeUV.y);
}

// The root form above as (k - sqrt(d)) / (2(r - 1)), d = k^2 - 4r(r - 1)X, k = 2r - 1. Inside a band X is in
// [0, 1], so d >= 1 and sqrt(d) == d * rsqrt(d). Cancellation for small X costs at most about 1e-7 of the band.
// At r == 1 the band is the identity.
float SourceToEdge(const float X, const float r) {
    const float k = 2. * r - 1.;
    const float d = k * k - 4. * r * (r - 1.) * X;
    return r == 1. ? X : (k - d * rsqrt(d)) / (2. * (r - 1.));
}

// One axis of DecodeFoveationUV in scalars (HLSL 2021, dxc's default, has no vector ternary). Each axis is in at
// most one band, inverted from that band's outer border. Branch-free, which is faster on desktop GPUs: outside
// both bands the profile may be NaN, but it is then not used.
float DecodeFoveationAxis
(
    const float x, const float loBound, const float hiBound,
    const float encLoBound, const float encHiBound, const float centerScale, const float edgeRatio
)
{
    const bool inLoBand = x < loBound;
    const float bandT = SourceToEdge((inLoBand ? x : 1. - x) / (inLoBand ? loBound : 1. - hiBound), edgeRatio);
    if (inLoBand)
        return encLoBound * bandT;
    if (x > hiBound)
        return 1. - (1. - encHiBound) * bandT;
    return encLoBound + (x - loBound) / centerScale;
}

float2 DecodeFoveationUV(const float2 uv, const float isRightEye) {
    const float2 eyeUV = TextureToEyeUV(uv, isRightEye);
    float2 encodedUV = float2(
        DecodeFoveationAxis(eyeUV.x, LoBound.x, HiBound.x, EncLoBound.x, EncHiBound.x, CenterScale.x, EdgeRatio.x),
        DecodeFoveationAxis(eyeUV.y, LoBound.y, HiBound.y, EncLoBound.y, EncHiBound.y, CenterScale.y, EdgeRatio.y)
    ) * EyeSizeRatio;
    // Keep the bilinear footprint away from the other eye. It can only be reached across the nasal edge (eye
    // x = 1): the sampler clamps at the frame's outer edges, and the encoder's padding repeats each eye's last
    // row and column.
    encodedUV.x = min(encodedUV.x, EyeSizeRatio.x - EncHalfTexel.x);
    return EyeToTextureUV(encodedUV, isRightEye);
}
