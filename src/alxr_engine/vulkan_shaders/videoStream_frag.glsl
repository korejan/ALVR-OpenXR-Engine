#version 460
#ifdef ENABLE_ARB_INCLUDE_EXT
    #extension GL_ARB_shading_language_include : require
#else
    // required by glslangValidator
    #extension GL_GOOGLE_include_directive : require
#endif
#pragma fragment

#include "common/baseVideoFrag.glsl"

layout(location = 0) out vec4 FragColor;


layout(early_fragment_tests) in;

void main()
{
    FragColor = SampleVideoTexture();
}
