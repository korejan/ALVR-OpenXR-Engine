#ifdef ENABLE_ARB_INCLUDE_EXT
    #extension GL_ARB_shading_language_include : require
#else
    // required by glslangValidator
    #extension GL_GOOGLE_include_directive : require
#endif

#extension GL_ARB_separate_shader_objects : enable
#extension GL_ARB_shading_language_420pack : enable

#ifdef ENABLE_MULTIVEW_EXT
    #extension GL_EXT_multiview : enable
#endif
precision highp float;

#include "sRGBLinearize.glsl"
#ifdef ENABLE_FOVEATION_DECODE
    #include "decodeFoveation.glsl"
#endif

layout(constant_id = 16) const bool EnableSRGBLinearize = true;

layout(binding = 0) uniform sampler2D tex_sampler;
layout(location = 0) in vec2 UV;

vec4 SampleVideoTexture() {
    vec4 result = texture
    (
        tex_sampler,
#ifdef ENABLE_FOVEATION_DECODE
        DecodeFoveationUV
        (
            UV,
#ifdef ENABLE_MULTIVEW_EXT
            float(gl_ViewIndex)
#else
            float(UV.x > 0.5)
#endif
        )
#else
        UV
#endif
    );
    return EnableSRGBLinearize ?
        sRGBToLinearRGB(result) : result;
}
