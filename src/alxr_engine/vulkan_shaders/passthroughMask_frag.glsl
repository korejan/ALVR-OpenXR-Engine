#version 460
#ifdef ENABLE_ARB_INCLUDE_EXT
    #extension GL_ARB_shading_language_include : require
#else
    // required by glslangValidator
    #extension GL_GOOGLE_include_directive : require
#endif
#pragma fragment

#include "common/baseVideoFrag.glsl"

layout(constant_id = 17) const float AlphaValue = 0.3f;
layout(constant_id = 18) const float KeyColorR = 0.01f;
layout(constant_id = 19) const float KeyColorG = 0.01f;
layout(constant_id = 20) const float KeyColorB = 0.01f;

const vec3 key_color = vec3(KeyColorR, KeyColorG, KeyColorB);

layout(location = 0) out vec4 FragColor;

layout(early_fragment_tests) in;

void main()
{
    vec4 color = SampleVideoTexture();
    color.a = all(lessThan(color.rgb, key_color)) ? AlphaValue : 1.0f;
    FragColor = color;
}
