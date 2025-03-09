cbuffer FoveationParams : register(b3) {
    float2 EyeSizeRatio;
    float2 EdgeRatio;
    float2 C1;
    float2 C2;
    float2 LoBound;
    float2 HiBound;
    float2 ALeft;
    float2 BLeft;
    float2 ARight;
    float2 BRight;
    float2 CRight;
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

float2 DecodeFoveationUV(const float2 uv, const float isRightEye) {
    const float2 eyeUV = TextureToEyeUV(uv, isRightEye);

    const float2 center    = (eyeUV - C1) * EdgeRatio / C2;
    const float2 leftEdge  = (-BLeft + sqrt(BLeft * BLeft + 4. * ALeft * eyeUV)) / (2. * ALeft);
    const float2 rightEdge = (-BRight + sqrt(BRight * BRight - 4. * (CRight - ARight * eyeUV))) / (2. * ARight);

    float2 uncompressedUV = float2(
        (eyeUV.x < LoBound.x) ? leftEdge.x : center.x,
        (eyeUV.y < LoBound.y) ? leftEdge.y : center.y
    );
    if (eyeUV.x > HiBound.x)
        uncompressedUV.x = rightEdge.x;
    if (eyeUV.y > HiBound.y)
        uncompressedUV.y = rightEdge.y;

    return EyeToTextureUV(uncompressedUV * EyeSizeRatio, isRightEye);
}
