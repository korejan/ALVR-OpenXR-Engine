#pragma warning( disable : 3571 ) // warn(-as-err) about pow

cbuffer YcbcrConstantsCB : register(b2) {
    float4x4 dequantizeColorMatrix;
};

float3 ConvertYUVtoRGB(float3 ycbcr) {
    return mul(dequantizeColorMatrix, float4(ycbcr, 1.f)).rgb;
}

// conversion based on: https://www.khronos.org/registry/DataFormat/specs/1.4/dataformat.1.4.html#TRANSFER_SRGB
float3 sRGBToLinearRGB(float3 srgb)
{
    static const float delta   = 1.0 / 12.92;
    static const float alpha   = 1.0 / 1.055;
    static const float beta    = 0.055 / 1.055;
    static const float3 alpha3 = float3(alpha, alpha, alpha);
    static const float3 theta3 = float3(0.04045, 0.04045, 0.04045);
    static const float3 beta3  = float3(beta, beta, beta); // Precomputed (alpha3 * 0.055)
    static const float3 gamma3 = float3(2.4, 2.4, 2.4);

    const float3 lower = srgb * delta;
    // alpha3 * (srgb + offset3)
    //      == (alpha3 * srgb) + (alpha3 * offset3)
    //      == (alpha3 * srgb) + beta3
    //      == mad(alpha3, srgb, beta3)
    //  where
    //    const float3 offset3 = float3(0.055,0.055,0.055);
    const float3 upper = pow((alpha3 * srgb) + beta3, gamma3);
    return lerp(lower, upper, srgb > theta3);
}
