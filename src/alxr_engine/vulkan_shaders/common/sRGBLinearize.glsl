precision highp float;

const float delta  = (1.0 / 12.92);
const vec3 alpha3  = vec3(1.0 / 1.055);
const vec3 theta3  = vec3(0.04045);
const vec3 beta3   = vec3(0.055 / 1.055); // Precomputed (alpha3 * 0.055)
const vec3 gamma3  = vec3(2.4);

// conversion based on: https://www.khronos.org/registry/DataFormat/specs/1.4/dataformat.1.4.html#TRANSFER_SRGB
vec4 sRGBToLinearRGB(vec4 srgba)
{
    const vec3 srgb = srgba.rgb;
    const vec3 lower = srgb * delta;
    // alpha3 * (srgb + offset3)
    //      == (alpha3 * srgb) + (alpha3 * offset3)
    //      == (alpha3 * srgb) + beta3
    //      == mad(alpha3, srgb, beta3)
    //  where
    //    const vec3 offset3 = vec3(0.055);
    const vec3 upper = pow(fma(alpha3, srgb, beta3), gamma3);
    return vec4(mix(lower, upper, greaterThan(srgb, theta3)), srgba.a);
}
