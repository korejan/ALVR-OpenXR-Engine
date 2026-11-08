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

precision highp float;

#define MAKE_VEC2_SP_CONST(name, start_id, xval, yval)              \
    layout(constant_id = start_id)     const float name##X = xval;  \
    layout(constant_id = (start_id+1)) const float name##Y = yval;  \
    const vec2 name = vec2(name##X, name##Y);

// See ALXR::FoveatedDecodeParams (foveation.h); the defaults are the identity mapping.
MAKE_VEC2_SP_CONST(EyeSizeRatio, 0,  1.0, 1.0)
MAKE_VEC2_SP_CONST(EdgeRatio,    2,  1.0, 1.0)
MAKE_VEC2_SP_CONST(LoBound,      4,  0.0, 0.0)
MAKE_VEC2_SP_CONST(HiBound,      6,  1.0, 1.0)
MAKE_VEC2_SP_CONST(EncLoBound,   8,  0.0, 0.0)
MAKE_VEC2_SP_CONST(EncHiBound,   10, 1.0, 1.0)
MAKE_VEC2_SP_CONST(CenterScale,  12, 1.0, 1.0)
MAKE_VEC2_SP_CONST(EncHalfTexel, 14, 0.0, 0.0)

vec2 TextureToEyeUV(const vec2 textureUV, const float isRightEye) {
    // flip distortion horizontally for right eye
    // left: x * 2; right: (1 - x) * 2
    return vec2(fma(isRightEye, fma(-2., textureUV.x, 1.), textureUV.x) * 2., textureUV.y);
}

vec2 EyeToTextureUV(const vec2 eyeUV, const float isRightEye) {
    // left: x / 2; right 1 - (x / 2)
    return vec2(fma(isRightEye, 1. - eyeUV.x, eyeUV.x * 0.5), eyeUV.y);
}

// The root form above as (k - sqrt(d)) / (2(r - 1)), d = k^2 - 4r(r - 1)X, k = 2r - 1: one inversesqrt, and a
// constant divisor once EdgeRatio is specialised. Only called inside a band, where X is in [0, 1] and so d >= 1.
// Cancellation for small X costs at most about 1e-7 of the band. At r == 1 the band is the identity.
float SourceToEdge(const float X, const float r) {
    const float k = 2. * r - 1.;
    const float d = k * k - 4. * r * (r - 1.) * X;
    return r == 1. ? X : (k - d * inversesqrt(d)) / (2. * (r - 1.));
}

vec2 DecodeFoveationUV(const vec2 uv, const float isRightEye) {
    const vec2 eyeUV = TextureToEyeUV(uv, isRightEye);
    const bvec2 inLoBand = lessThan(eyeUV, LoBound);
    const bvec2 inHiBand = greaterThan(eyeUV, HiBound);

    // Each axis is in at most one band, inverted from that band's outer border; the fovea, most of the eye,
    // skips it. The band widths' reciprocals are constants once specialised.
    const vec2 X = mix(1. - eyeUV, eyeUV, inLoBand) * mix(1. / (1. - HiBound), 1. / LoBound, inLoBand);
    vec2 bandT = vec2(0.);
    if (inLoBand.x || inHiBand.x)
        bandT.x = SourceToEdge(X.x, EdgeRatio.x);
    if (inLoBand.y || inHiBand.y)
        bandT.y = SourceToEdge(X.y, EdgeRatio.y);
    const vec2 center = EncLoBound + (eyeUV - LoBound) * (1. / CenterScale);

    const vec2 encodedUV = mix(mix(center, EncLoBound * bandT, inLoBand), 1. - (1. - EncHiBound) * bandT, inHiBand);
    // Keep the bilinear footprint away from the other eye. It can only be reached across the nasal edge (eye
    // x = 1): the sampler clamps at the frame's outer edges, and the encoder's padding repeats each eye's last
    // row and column.
    vec2 c = encodedUV * EyeSizeRatio;
    c.x = min(c.x, EyeSizeRatio.x - EncHalfTexel.x);
    return EyeToTextureUV(c, isRightEye);
}
