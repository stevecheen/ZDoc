﻿/************************************************************************************

Filename    :   Render_GL_Device.cpp
Content     :   RenderDevice implementation for OpenGL
Created     :   September 10, 2012
Authors     :   Andrew Reisse

Copyright   :   Copyright 2012 Oculus VR, LLC. All Rights reserved.

Licensed under the Apache License, Version 2.0 (the "License");
you may not use this file except in compliance with the License.
You may obtain a copy of the License at

http://www.apache.org/licenses/LICENSE-2.0

Unless required by applicable law or agreed to in writing, software
distributed under the License is distributed on an "AS IS" BASIS,
WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
See the License for the specific language governing permissions and
limitations under the License.

************************************************************************************/

#include "../Render/Render_GL_Device.h"
#include "Kernel/OVR_Log.h"
#include <assert.h>

namespace OVR
{
namespace Render
{
namespace GL
{

OVR::GLEContext gleContext;

void InitGLExtensions()
{
    if(!gleContext.IsInitialized())
    {
        OVR::GLEContext::SetCurrentContext(&gleContext);
        gleContext.Init();
    }
}


// glsl2Prefix / glsl3Prefix
// These provide #defines for shader types that differ between GLSL 1.5 (OpenGL 3.2) and earlier versions.
static const char glsl2Prefix[] =
    "#version 110\n"
    "#extension GL_ARB_shader_texture_lod : enable\n"
    "#define _FRAGCOLOR_DECLARATION\n"
    "#define _VS_IN attribute\n"
    "#define _VS_OUT varying\n"
    "#define _FS_IN varying\n"
    "#define _TEXTURELOD texture2DLod\n"
    "#define _TEXTURE texture2D\n"
    "#define _FRAGCOLOR gl_FragColor\n";

static const char glsl3Prefix[] =
    "#version 150\n"
    "#define _FRAGCOLOR_DECLARATION out vec4 FragColor;\n"
    "#define _VS_IN in\n"
    "#define _VS_OUT out\n"
    "#define _FS_IN in\n"
    "#define _TEXTURELOD textureLod\n"
    "#define _TEXTURE texture\n"
    "#define _FRAGCOLOR FragColor\n";


static const char* StdVertexShaderSrc =
    "uniform mat4 Proj;\n"
    "uniform mat4 View;\n"

    "_VS_IN vec4 Position;\n"
    "_VS_IN vec4 Color;\n"
    "_VS_IN vec2 TexCoord;\n"
    "_VS_IN vec2 TexCoord1;\n"
    "_VS_IN vec3 Normal;\n"

    "_VS_OUT vec4 oColor;\n"
    "_VS_OUT vec2 oTexCoord;\n"
    "_VS_OUT vec2 oTexCoord1;\n"
    "_VS_OUT vec3 oNormal;\n"
    "_VS_OUT vec3 oVPos;\n"

    "void main()\n"
    "{\n"
    "   gl_Position = Proj * (View * Position);\n"
    "   oNormal = vec3(View * vec4(Normal,0));\n"
    "   oVPos = vec3(View * Position);\n"
    "   oTexCoord = TexCoord;\n"
    "   oTexCoord1 = TexCoord1;\n"
    "   oColor = Color;\n"
    "}\n";

static const char* DirectVertexShaderSrc =
    "uniform mat4 View;\n"

    "_VS_IN vec4 Position;\n"
    "_VS_IN vec4 Color;\n"
    "_VS_IN vec2 TexCoord;\n"
    "_VS_IN vec3 Normal;\n"

    "_VS_OUT vec4 oColor;\n"
    "_VS_OUT vec2 oTexCoord;\n"
    "_VS_OUT vec3 oNormal;\n"

    "void main()\n"
    "{\n"
    "   gl_Position = View * Position;\n"
    "   oTexCoord = TexCoord;\n"
    "   oColor = Color;\n"
    "   oNormal = vec3(View * vec4(Normal,0));\n"
    "}\n";

static const char* SolidFragShaderSrc =
    "uniform vec4 Color;\n"

    "_FRAGCOLOR_DECLARATION\n"

    "void main()\n"
    "{\n"
    "   _FRAGCOLOR = Color;\n"
    "}\n";

static const char* GouraudFragShaderSrc =
    "_FS_IN vec4 oColor;\n"

    "_FRAGCOLOR_DECLARATION\n"

    "void main()\n"
    "{\n"
    "   _FRAGCOLOR = oColor;\n"
    "}\n";

static const char* TextureFragShaderSrc =
    "uniform sampler2D Texture0;\n"

    "_FS_IN vec4 oColor;\n"
    "_FS_IN vec2 oTexCoord;\n"

    "_FRAGCOLOR_DECLARATION\n"

    "void main()\n"
    "{\n"
    "   _FRAGCOLOR = oColor * _TEXTURE(Texture0, oTexCoord);\n"
    "   if (_FRAGCOLOR.a < 0.4)\n"
    "       discard;\n"
    "}\n";

#define LIGHTING_COMMON                                                          \
    "uniform   vec3 Ambient;\n"                                                   \
    "uniform   vec4 LightPos[8];\n"                                                \
    "uniform   vec4 LightColor[8];\n"                                               \
    "uniform   float LightCount;\n"                                                  \
    "_FS_IN    vec4 oColor;\n"                                                        \
    "_FS_IN    vec2 oTexCoord;\n"                                                      \
    "_FS_IN    vec3 oNormal;\n"                                                         \
    "_FS_IN    vec3 oVPos;\n"                                                            \
    "vec4 DoLight()\n"                                                                    \
    "{\n"                                                                                  \
    "   vec3 norm = normalize(oNormal);\n"                                                  \
    "   vec3 light = Ambient;\n"                                                             \
    "   for (int i = 0; i < int(LightCount); i++)\n"                                          \
    "   {\n"                                                                                   \
    "       vec3 ltp = (LightPos[i].xyz - oVPos);\n"                                            \
    "       float  ldist = length(ltp);\n"                                                       \
    "       ltp = normalize(ltp);\n"                                                              \
    "       light += clamp(LightColor[i].rgb * oColor.rgb * (dot(norm, ltp) / ldist), 0.0,1.0);\n" \
    "   }\n"                                                                                        \
    "   return vec4(light, oColor.a);\n"                                                             \
    "}\n"

static const char* LitSolidFragShaderSrc =
    LIGHTING_COMMON

    "_FRAGCOLOR_DECLARATION\n"

    "void main()\n"
    "{\n"
    "   _FRAGCOLOR = DoLight() * oColor;\n"
    "}\n";

static const char* LitTextureFragShaderSrc =
    LIGHTING_COMMON

    "uniform sampler2D Texture0;\n"

    "_FRAGCOLOR_DECLARATION\n"

    "void main()\n"
    "{\n"
    "   _FRAGCOLOR = DoLight() * _TEXTURE(Texture0, oTexCoord);\n"
    "}\n";

static const char* AlphaTextureFragShaderSrc =
    "uniform sampler2D Texture0;\n"

    "_FS_IN vec4 oColor;\n"
    "_FS_IN vec2 oTexCoord;\n"

    "_FRAGCOLOR_DECLARATION\n"

    "void main()\n"
    "{\n"
    "   vec4 finalColor = oColor;\n"
    "   finalColor.a *= _TEXTURE(Texture0, oTexCoord).r;\n"
    // Blend state expects premultiplied alpha
    "   finalColor.rgb *= finalColor.a;\n"
    "   _FRAGCOLOR = finalColor;\n"
    "}\n";

static const char* AlphaBlendedTextureFragShaderSrc =
    "uniform sampler2D Texture0;\n"

    "_FS_IN vec4 oColor;\n"
    "_FS_IN vec2 oTexCoord;\n"

    "_FRAGCOLOR_DECLARATION\n"

    "void main()\n"
    "{\n"
    "   vec4 finalColor = oColor;\n"
    "   finalColor *= _TEXTURE(Texture0, oTexCoord);\n"
    // Blend state expects premultiplied alpha
    "   finalColor.rgb *= finalColor.a;\n"
    "   _FRAGCOLOR = finalColor;\n"
    "}\n";

static const char* AlphaPremultTexturePixelShaderSrc =
    "uniform sampler2D Texture0;\n"

    "_FS_IN vec4 oColor;\n"
    "_FS_IN vec2 oTexCoord;\n"

    "_FRAGCOLOR_DECLARATION\n"

    "void main()\n"
    "{\n"
    "   vec4 finalColor = oColor;\n"
    "   finalColor *= _TEXTURE(Texture0, oTexCoord);\n"
    // texture should already be in premultiplied alpha
    "   _FRAGCOLOR = finalColor;\n"
    "}\n";

static const char* MultiTextureFragShaderSrc =
    "uniform sampler2D Texture0;\n"
    "uniform sampler2D Texture1;\n"

    "_FS_IN vec4 oColor;\n"
    "_FS_IN vec2 oTexCoord;\n"
    "_FS_IN vec2 oTexCoord1;\n"

    "_FRAGCOLOR_DECLARATION\n"

    "void main()\n"
    "{\n"
    "    vec4 color1 = _TEXTURE(Texture0, oTexCoord);\n"
    "    vec4 color2 = _TEXTURE(Texture1, oTexCoord1);\n"
    "    color2.rgb = sqrt(color2.rgb);\n"
    "    color2.rgb = color2.rgb * mix(0.2, 1.2, clamp(length(color2.rgb),0.0,1.0));\n"
    "    _FRAGCOLOR = color1 * color2;\n"
    "   if (_FRAGCOLOR.a <= 0.6)\n"
    "        discard;\n"
    "}\n";

static const char* PostProcessMeshFragShaderSrc =
    "uniform sampler2D Texture;\n"

    "_FS_IN vec4 oColor;\n"
    "_FS_IN vec2 oTexCoord0;\n"
    "_FS_IN vec2 oTexCoord1;\n"
    "_FS_IN vec2 oTexCoord2;\n"

    "_FRAGCOLOR_DECLARATION\n"

    "void main()\n"
    "{\n"
    "   _FRAGCOLOR.r = oColor.r * _TEXTURE(Texture, oTexCoord0).r;\n"
    "   _FRAGCOLOR.g = oColor.g * _TEXTURE(Texture, oTexCoord1).g;\n"
    "   _FRAGCOLOR.b = oColor.b * _TEXTURE(Texture, oTexCoord2).b;\n"
    "   _FRAGCOLOR.a = 1.0;\n"
    "}\n";

static const char* PostProcessMeshTimewarpFragShaderSrc = PostProcessMeshFragShaderSrc;
static const char* PostProcessMeshPositionalTimewarpFragShaderSrc = PostProcessMeshFragShaderSrc;
static const char* PostProcessHeightmapTimewarpFragShaderSrc = PostProcessMeshFragShaderSrc;

static const char* PostProcessVertexShaderSrc =
    "uniform mat4 View;\n"
    "uniform mat4 Texm;\n"

    "_VS_IN vec4 Position;\n"
    "_VS_IN vec2 TexCoord;\n"

    "_VS_OUT vec2 oTexCoord;\n"

    "void main()\n"
    "{\n"
    "   gl_Position = View * Position;\n"
    "   oTexCoord = vec2(Texm * vec4(TexCoord,0,1));\n"
    "}\n";

static const char* PostProcessMeshVertexShaderSrc =
    "uniform vec2 EyeToSourceUVScale;\n"
    "uniform vec2 EyeToSourceUVOffset;\n"

    "_VS_IN vec2 Position;\n"
    "_VS_IN vec4 Color;\n"
    "_VS_IN vec2 TexCoord0;\n"
    "_VS_IN vec2 TexCoord1;\n"
    "_VS_IN vec2 TexCoord2;\n"

    "_VS_OUT vec4 oColor;\n"
    "_VS_OUT vec2 oTexCoord0;\n"
    "_VS_OUT vec2 oTexCoord1;\n"
    "_VS_OUT vec2 oTexCoord2;\n"

    "void main()\n"
    "{\n"
    "   gl_Position.x = Position.x;\n"
    "   gl_Position.y = Position.y;\n"
    "   gl_Position.z = 0.5;\n"
    "   gl_Position.w = 1.0;\n"
    // Vertex inputs are in TanEyeAngle space for the R,G,B channels (i.e. after chromatic aberration and distortion).
    // Scale them into the correct [0-1],[0-1] UV lookup space (depending on eye)
    "   oTexCoord0 = TexCoord0 * EyeToSourceUVScale + EyeToSourceUVOffset;\n"
    "   oTexCoord0.y = 1.0-oTexCoord0.y;\n"
    "   oTexCoord1 = TexCoord1 * EyeToSourceUVScale + EyeToSourceUVOffset;\n"
    "   oTexCoord1.y = 1.0-oTexCoord1.y;\n"
    "   oTexCoord2 = TexCoord2 * EyeToSourceUVScale + EyeToSourceUVOffset;\n"
    "   oTexCoord2.y = 1.0-oTexCoord2.y;\n"
    "   oColor = Color;\n"              // Used for vignette fade.
    "}\n";

static const char* PostProcessMeshTimewarpVertexShaderSrc =
    "uniform vec2 EyeToSourceUVScale;\n"
    "uniform vec2 EyeToSourceUVOffset;\n"
    "uniform mat4 EyeRotationStart;\n"
    "uniform mat4 EyeRotationEnd;\n"

    "_VS_IN vec2 Position;\n"
    "_VS_IN vec4 Color;\n"
    "_VS_IN vec2 TexCoord0;\n"
    "_VS_IN vec2 TexCoord1;\n"
    "_VS_IN vec2 TexCoord2;\n"

    "_VS_OUT vec4 oColor;\n"
    "_VS_OUT vec2 oTexCoord0;\n"
    "_VS_OUT vec2 oTexCoord1;\n"
    "_VS_OUT vec2 oTexCoord2;\n"

    "void main()\n"
    "{\n"
    "   gl_Position.x = Position.x;\n"
    "   gl_Position.y = Position.y;\n"
    "   gl_Position.z = 0.0;\n"
    "   gl_Position.w = 1.0;\n"

    // Vertex inputs are in TanEyeAngle space for the R,G,B channels (i.e. after chromatic aberration and distortion).
    // These are now "real world" vectors in direction (x,y,1) relative to the eye of the HMD.
    "   vec3 TanEyeAngleR = vec3 ( TexCoord0.x, TexCoord0.y, 1.0 );\n"
    "   vec3 TanEyeAngleG = vec3 ( TexCoord1.x, TexCoord1.y, 1.0 );\n"
    "   vec3 TanEyeAngleB = vec3 ( TexCoord2.x, TexCoord2.y, 1.0 );\n"

    // Accurate time warp lerp vs. faster
#if 0
    // Apply the two 3x3 timewarp rotations to these vectors.
    "   vec3 TransformedRStart = (EyeRotationStart * vec4(TanEyeAngleR, 0)).xyz;\n"
    "   vec3 TransformedGStart = (EyeRotationStart * vec4(TanEyeAngleG, 0)).xyz;\n"
    "   vec3 TransformedBStart = (EyeRotationStart * vec4(TanEyeAngleB, 0)).xyz;\n"
    "   vec3 TransformedREnd   = (EyeRotationEnd * vec4(TanEyeAngleR, 0)).xyz;\n"
    "   vec3 TransformedGEnd   = (EyeRotationEnd * vec4(TanEyeAngleG, 0)).xyz;\n"
    "   vec3 TransformedBEnd   = (EyeRotationEnd * vec4(TanEyeAngleB, 0)).xyz;\n"
    // And blend between them.
    "   vec3 TransformedR = mix ( TransformedRStart, TransformedREnd, Color.a );\n"
    "   vec3 TransformedG = mix ( TransformedGStart, TransformedGEnd, Color.a );\n"
    "   vec3 TransformedB = mix ( TransformedBStart, TransformedBEnd, Color.a );\n"
#else
    "   mat3 EyeRotation;\n"
    "   EyeRotation[0] = mix ( EyeRotationStart[0], EyeRotationEnd[0], Color.a ).xyz;\n"
    "   EyeRotation[1] = mix ( EyeRotationStart[1], EyeRotationEnd[1], Color.a ).xyz;\n"
    "   EyeRotation[2] = mix ( EyeRotationStart[2], EyeRotationEnd[2], Color.a ).xyz;\n"
    "   vec3 TransformedR   = EyeRotation * TanEyeAngleR;\n"
    "   vec3 TransformedG   = EyeRotation * TanEyeAngleG;\n"
    "   vec3 TransformedB   = EyeRotation * TanEyeAngleB;\n"
#endif

    // Project them back onto the Z=1 plane of the rendered images.
    "   float RecipZR = 1.0 / TransformedR.z;\n"
    "   float RecipZG = 1.0 / TransformedG.z;\n"
    "   float RecipZB = 1.0 / TransformedB.z;\n"
    "   vec2 FlattenedR = vec2 ( TransformedR.x * RecipZR, TransformedR.y * RecipZR );\n"
    "   vec2 FlattenedG = vec2 ( TransformedG.x * RecipZG, TransformedG.y * RecipZG );\n"
    "   vec2 FlattenedB = vec2 ( TransformedB.x * RecipZB, TransformedB.y * RecipZB );\n"

    // These are now still in TanEyeAngle space.
    // Scale them into the correct [0-1],[0-1] UV lookup space (depending on eye)
    "   vec2 SrcCoordR = FlattenedR * EyeToSourceUVScale + EyeToSourceUVOffset;\n"
    "   vec2 SrcCoordG = FlattenedG * EyeToSourceUVScale + EyeToSourceUVOffset;\n"
    "   vec2 SrcCoordB = FlattenedB * EyeToSourceUVScale + EyeToSourceUVOffset;\n"
    "   oTexCoord0 = SrcCoordR;\n"
    "   oTexCoord0.y = 1.0-oTexCoord0.y;\n"
    "   oTexCoord1 = SrcCoordG;\n"
    "   oTexCoord1.y = 1.0-oTexCoord1.y;\n"
    "   oTexCoord2 = SrcCoordB;\n"
    "   oTexCoord2.y = 1.0-oTexCoord2.y;\n"
    "   oColor = vec4(Color.r, Color.r, Color.r, Color.r);\n"              // Used for vignette fade.
    "}\n";

static const char* PostProcessMeshPositionalTimewarpVertexShaderSrc =
#if 1 //TODO: Disabled until we fix positional timewarp and layering on GL.
    PostProcessMeshTimewarpVertexShaderSrc;
#else
    "#version 150\n"

    "uniform sampler2D Texture0;\n"
    "uniform vec2 EyeToSourceUVScale;\n"
    "uniform vec2 EyeToSourceUVOffset;\n"
    "uniform vec2 DepthProjector;\n"
    "uniform vec2 DepthDimSize;\n"
    "uniform mat4 EyeRotationStart;\n"
    "uniform mat4 EyeRotationEnd;\n"

    "_VS_IN vec2 Position;\n"
    "_VS_IN vec4 Color;\n"
    "_VS_IN vec2 TexCoord0;\n"
    "_VS_IN vec2 TexCoord1;\n"
    "_VS_IN vec2 TexCoord2;\n"

    "_VS_OUT vec4 oColor;\n"
    "_VS_OUT vec2 oTexCoord0;\n"
    "_VS_OUT vec2 oTexCoord1;\n"
    "_VS_OUT vec2 oTexCoord2;\n"

    "vec4 PositionFromDepth(vec2 inTexCoord)\n"
    "{\n"
    "   vec2 eyeToSourceTexCoord = inTexCoord * EyeToSourceUVScale + EyeToSourceUVOffset;\n"
    "   eyeToSourceTexCoord.y = 1.0 - eyeToSourceTexCoord.y;\n"
    "   float depth = texelFetch(Texture0, ivec2(eyeToSourceTexCoord * DepthDimSize), 0).x;\n" //FIXME: Use _TEXTURELOD for #version 110 support.
    "   float linearDepth = DepthProjector.y / (depth - DepthProjector.x);\n"
    "   vec4 retVal = vec4(inTexCoord, 1, 1);\n"
    "   retVal.xyz *= linearDepth;\n"
    "   return retVal;\n"
    "}\n"

    "vec2 TimewarpTexCoordToWarpedPos(vec2 inTexCoord, float a)\n"
    "{\n"
    // Vertex inputs are in TanEyeAngle space for the R,G,B channels (i.e. after chromatic aberration and distortion).
    // These are now "real world" vectors in direction (x,y,1) relative to the eye of the HMD.
    // Apply the 4x4 timewarp rotation to these vectors.
    "   vec4 inputPos = PositionFromDepth(inTexCoord);\n"
    "   vec3 transformed = mix ( EyeRotationStart * inputPos,  EyeRotationEnd * inputPos, a ).xyz;\n"
    // Project them back onto the Z=1 plane of the rendered images.
    "   vec2 flattened = transformed.xy / transformed.z;\n"
    // Scale them into ([0,0.5],[0,1]) or ([0.5,0],[0,1]) UV lookup space (depending on eye)
    "   vec2 noDepthUV = flattened * EyeToSourceUVScale + EyeToSourceUVOffset;\n"
    //"   float depth = _TEXTURE(Texture0, noDepthUV).r;\n"
    "   return noDepthUV.xy;\n"
    "}\n"

    "void main()\n"
    "{\n"
    "   gl_Position.x = Position.x;\n"
    "   gl_Position.y = Position.y;\n"
    "   gl_Position.z = 0.0;\n"
    "   gl_Position.w = 1.0;\n"

    // warped positions are a bit more involved, hence a separate function
    "   oTexCoord0 = TimewarpTexCoordToWarpedPos(TexCoord0, Color.a);\n"
    "   oTexCoord0.y = 1.0 - oTexCoord0.y;\n"
    "   oTexCoord1 = TimewarpTexCoordToWarpedPos(TexCoord1, Color.a);\n"
    "   oTexCoord1.y = 1.0 - oTexCoord1.y;\n"
    "   oTexCoord2 = TimewarpTexCoordToWarpedPos(TexCoord2, Color.a);\n"
    "   oTexCoord2.y = 1.0 - oTexCoord2.y;\n"

    "   oColor = vec4(Color.r, Color.r, Color.r, Color.r);  // Used for vignette fade.\n"
    "}\n";
#endif


static const char* PostProcessHeightmapTimewarpVertexShaderSrc =
#if 1 //TODO: Disabled until we fix positional timewarp and layering on GL.
    PostProcessMeshTimewarpVertexShaderSrc;
#else
    "#version 150\n"

    "uniform sampler2D Texture0;\n"
    "uniform vec2 EyeToSourceUVScale;\n"
    "uniform vec2 EyeToSourceUVOffset;\n"
    "uniform vec2 DepthDimSize;\n"
    "uniform mat4 EyeXformStart;\n"
    "uniform mat4 EyeXformEnd;\n"
    //"uniform mat4 Projection;\n"
    "uniform mat4 InvProjection;\n"

    "attribute vec2 Position;\n"
    "attribute vec3 TexCoord0;\n"

    "_VS_OUT vec2 oTexCoord0;\n"

    "vec4 PositionFromDepth(vec2 position, vec2 inTexCoord)\n"
    "{\n"
    "   float depth = texelFetch(Texture0, ivec2(inTexCoord * DepthDimSize), 0).x;\n" //FIXME: Use _TEXTURELOD for #version 110 support.
    "   vec4 retVal = vec4(position, depth, 1);\n"
    "   return retVal;\n"
    "}\n"

    "vec4 TimewarpPos(vec2 position, vec2 inTexCoord, mat4 rotMat)\n"
    "{\n"
    // Apply the 4x4 timewarp rotation to these vectors.
    "   vec4 transformed = PositionFromDepth(position, inTexCoord);\n"
    "   transformed = InvProjection * transformed;\n"
    "   transformed = rotMat * transformed;\n"
    //"   transformed = mul ( Projection, transformed );\n"
    "   return transformed;\n"
    "}\n"

    "void main()\n"
    "{\n"
    "   vec2 eyeToSrcTexCoord = TexCoord0.xy * EyeToSourceUVScale + EyeToSourceUVOffset;\n"
    "   oTexCoord0 = eyeToSrcTexCoord;\n"

    "   float timewarpLerpFactor = TexCoord0.z;\n"
    "   mat4 lerpedEyeRot;  // GL cannot mix() matrices :-( \n"
    "   lerpedEyeRot[0] = mix(EyeXformStart[0], EyeXformEnd[0], timewarpLerpFactor);\n"
    "   lerpedEyeRot[1] = mix(EyeXformStart[1], EyeXformEnd[1], timewarpLerpFactor);\n"
    "   lerpedEyeRot[2] = mix(EyeXformStart[2], EyeXformEnd[2], timewarpLerpFactor);\n"
    "   lerpedEyeRot[3] = mix(EyeXformStart[3], EyeXformEnd[3], timewarpLerpFactor);\n"
    //"    float4x4 lerpedEyeRot = EyeXformStart;\n"

    // warped positions are a bit more involved, hence a separate function
    "   gl_Position = TimewarpPos(Position.xy, oTexCoord0, lerpedEyeRot);\n"
    "}\n";
#endif

// Shader with lens distortion and chromatic aberration correction.
static const char* PostProcessFragShaderWithChromAbSrc =
    "uniform sampler2D Texture;\n"
    "uniform vec3 DistortionClearColor;\n"
    "uniform float EdgeFadeScale;\n"
    "uniform vec2 EyeToSourceUVScale;\n"
    "uniform vec2 EyeToSourceUVOffset;\n"
    "uniform vec2 EyeToSourceNDCScale;\n"
    "uniform vec2 EyeToSourceNDCOffset;\n"
    "uniform vec2 TanEyeAngleScale;\n"
    "uniform vec2 TanEyeAngleOffset;\n"
    "uniform vec4 HmdWarpParam;\n"
    "uniform vec4 ChromAbParam;\n"

    "_FS_IN vec4 oPosition;\n"
    "_FS_IN vec2 oTexCoord;\n"

    "_FRAGCOLOR_DECLARATION\n"

    "void main()\n"
    "{\n"
    // Input oTexCoord is [-1,1] across the half of the screen used for a single eye.
    "   vec2 TanEyeAngleDistorted = oTexCoord * TanEyeAngleScale + TanEyeAngleOffset;\n" // Scales to tan(thetaX),tan(thetaY), but still distorted (i.e. only the center is correct)
    "   float  RadiusSq = TanEyeAngleDistorted.x * TanEyeAngleDistorted.x + TanEyeAngleDistorted.y * TanEyeAngleDistorted.y;\n"
    "   float Distort = 1.0 / ( 1.0 + RadiusSq * ( HmdWarpParam.y + RadiusSq * ( HmdWarpParam.z + RadiusSq * ( HmdWarpParam.w ) ) ) );\n"
    "   float DistortR = Distort * ( ChromAbParam.x + RadiusSq * ChromAbParam.y );\n"
    "   float DistortG = Distort;\n"
    "   float DistortB = Distort * ( ChromAbParam.z + RadiusSq * ChromAbParam.w );\n"
    "   vec2 TanEyeAngleR = DistortR * TanEyeAngleDistorted;\n"
    "   vec2 TanEyeAngleG = DistortG * TanEyeAngleDistorted;\n"
    "   vec2 TanEyeAngleB = DistortB * TanEyeAngleDistorted;\n"

    // These are now in "TanEyeAngle" space.
    // The vectors (TanEyeAngleRGB.x, TanEyeAngleRGB.y, 1.0) are real-world vectors pointing from the eye to where the components of the pixel appear to be.
    // If you had a raytracer, you could just use them directly.

    // Scale them into ([0,0.5],[0,1]) or ([0.5,0],[0,1]) UV lookup space (depending on eye)
    "   vec2 SourceCoordR = TanEyeAngleR * EyeToSourceUVScale + EyeToSourceUVOffset;\n"
    "    SourceCoordR.y = 1.0 - SourceCoordR.y;\n"
    "   vec2 SourceCoordG = TanEyeAngleG * EyeToSourceUVScale + EyeToSourceUVOffset;\n"
    "    SourceCoordG.y = 1.0 - SourceCoordG.y;\n"
    "   vec2 SourceCoordB = TanEyeAngleB * EyeToSourceUVScale + EyeToSourceUVOffset;\n"
    "    SourceCoordB.y = 1.0 - SourceCoordB.y;\n"

    // Find the distance to the nearest edge.
    "   vec2 NDCCoord = TanEyeAngleG * EyeToSourceNDCScale + EyeToSourceNDCOffset;\n"
    "   float EdgeFadeIn = clamp ( EdgeFadeScale, 0.0, 1e5 ) * ( 1.0 - max ( abs ( NDCCoord.x ), abs ( NDCCoord.y ) ) );\n"
    "   if ( EdgeFadeIn < 0.0 )\n"
    "   {\n"
    "       _FRAGCOLOR = vec4(DistortionClearColor.r, DistortionClearColor.g, DistortionClearColor.b, 1.0);\n"
    "       return;\n"
    "   }\n"
    "   EdgeFadeIn = clamp ( EdgeFadeIn, 0.0, 1.0 );\n"

    // Actually do the lookups.
    "   float ResultR = _TEXTURE(Texture, SourceCoordR).r;\n"
    "   float ResultG = _TEXTURE(Texture, SourceCoordG).g;\n"
    "   float ResultB = _TEXTURE(Texture, SourceCoordB).b;\n"

    "   _FRAGCOLOR = vec4(ResultR * EdgeFadeIn, ResultG * EdgeFadeIn, ResultB * EdgeFadeIn, 1.0);\n"
    "}\n";



static const char* VShaderSrcs[VShader_Count] =
{
    DirectVertexShaderSrc,
    StdVertexShaderSrc,
    PostProcessVertexShaderSrc,
    PostProcessMeshVertexShaderSrc,
    PostProcessMeshTimewarpVertexShaderSrc,
    PostProcessMeshPositionalTimewarpVertexShaderSrc,
    PostProcessHeightmapTimewarpVertexShaderSrc,
};
static const char* FShaderSrcs[FShader_Count] =
{
    SolidFragShaderSrc,
    GouraudFragShaderSrc,
    TextureFragShaderSrc,
    AlphaTextureFragShaderSrc,
    AlphaBlendedTextureFragShaderSrc,
    AlphaPremultTexturePixelShaderSrc,
    PostProcessFragShaderWithChromAbSrc,
    LitSolidFragShaderSrc,
    LitTextureFragShaderSrc,
    MultiTextureFragShaderSrc,
    PostProcessMeshFragShaderSrc,
    PostProcessMeshTimewarpFragShaderSrc,
    PostProcessMeshPositionalTimewarpFragShaderSrc,
    PostProcessHeightmapTimewarpFragShaderSrc
};

#if defined(OVR_BUILD_DEBUG)
static bool CheckFramebufferStatus(GLenum target)
{
    GLenum status = glCheckFramebufferStatus(target);
    switch (status)
    {
    case GL_FRAMEBUFFER_UNDEFINED:
        OVR_DEBUG_LOG(("framebuffer not complete: GL_FRAMEBUFFER_UNDEFINED - returned if the specified framebuffer is the default read or draw framebuffer, but the default framebuffer does not exist."));
        break;
    case GL_FRAMEBUFFER_INCOMPLETE_ATTACHMENT:
        OVR_DEBUG_LOG(("framebuffer not complete: GL_FRAMEBUFFER_INCOMPLETE_ATTACHMENT - returned if any of the framebuffer attachment points are framebuffer incomplete."));
        break;
    case GL_FRAMEBUFFER_INCOMPLETE_MISSING_ATTACHMENT:
        OVR_DEBUG_LOG(("framebuffer not complete: GL_FRAMEBUFFER_INCOMPLETE_MISSING_ATTACHMENT - returned if the framebuffer does not have at least one image attached to it."));
        break;
    case GL_FRAMEBUFFER_INCOMPLETE_DRAW_BUFFER:
        OVR_DEBUG_LOG(("framebuffer not complete: GL_FRAMEBUFFER_INCOMPLETE_DRAW_BUFFER - returned if the value of GL_FRAMEBUFFER_ATTACHMENT_OBJECT_TYPE is GL_NONE for any color attachment point named by GL_DRAW_BUFFERi."));
        break;
    case GL_FRAMEBUFFER_INCOMPLETE_READ_BUFFER:
        OVR_DEBUG_LOG(("framebuffer not complete: GL_FRAMEBUFFER_INCOMPLETE_READ_BUFFER - returned if GL_READ_BUFFER is not GL_NONE and the value of GL_FRAMEBUFFER_ATTACHMENT_OBJECT_TYPE is GL_NONE for the color attachment point named by GL_READ_BUFFER."));
        break;
    case GL_FRAMEBUFFER_UNSUPPORTED:
        OVR_DEBUG_LOG(("framebuffer not complete: GL_FRAMEBUFFER_UNSUPPORTED - returned if the combination of internal formats of the attached images violates an implementation-dependent set of restrictions."));
        break;
    case GL_FRAMEBUFFER_INCOMPLETE_MULTISAMPLE:
        OVR_DEBUG_LOG(("framebuffer not complete: GL_FRAMEBUFFER_INCOMPLETE_MULTISAMPLE - returned if the value of GL_RENDERBUFFER_SAMPLES is not the same for all attached renderbuffers; if the value of GL_TEXTURE_SAMPLES is the not same for all attached textures; or, if the attached images are a mix of renderbuffers and textures, the value of GL_RENDERBUFFER_SAMPLES does not match the value of GL_TEXTURE_SAMPLES. also returned if the value of GL_TEXTURE_FIXED_SAMPLE_LOCATIONS is not the same for all attached textures; or, if the attached images are a mix of renderbuffers and textures, the value of GL_TEXTURE_FIXED_SAMPLE_LOCATIONS is not GL_TRUE for all attached textures."));
        break;
    case GL_FRAMEBUFFER_INCOMPLETE_LAYER_TARGETS:
        OVR_DEBUG_LOG(("framebuffer not complete: GL_FRAMEBUFFER_INCOMPLETE_LAYER_TARGETS - returned if any framebuffer attachment is layered, and any populated attachment is not layered, or if all populated color attachments are not from textures of the same target."));
        break;
    default:
        OVR_DEBUG_LOG(("framebuffer not complete: status 0x%x (unknown)", status));
        break;
    case GL_FRAMEBUFFER_COMPLETE:
        return true;
    }
    return false;
}
#endif // defined(OVR_BUILD_DEBUG)

RenderDevice::RenderDevice(ovrHmd hmd, const RendererParams&)
    : Render::RenderDevice(hmd),
      VertexShaders(),
      FragShaders(),
      DefaultFill(),
      DefaultTextureFill(),
      DefaultTextureFillAlpha(),
      DefaultTextureFillPremult(),
      Proj(),
      Vao(0),
      CurRenderTarget(),
      DepthBuffers(),
      CurrentFbo(0),
      MsaaFbo(0),
      GLVersionInfo(),
      DebugCallbackControl(),
      Lighting(NULL)
{
    InitGLExtensions();
    DebugCallbackControl.Initialize();

    GetGLVersionAndExtensions(GLVersionInfo);

    OVR_ASSERT(GLVersionInfo.MajorVersion >= 2);
    const char* shaderPrefix = (GLVersionInfo.WholeVersion >= 302) ? glsl3Prefix : glsl2Prefix;
    const size_t shaderPrefixSize = strlen(shaderPrefix);

    for (int i = 0; i < VShader_Count; i++)
    {
        OVR_ASSERT ( VShaderSrcs[i] != NULL );      // You forgot a shader!
        const size_t shaderSize = strlen(VShaderSrcs[i]);
        const size_t sizeSum = shaderPrefixSize + shaderSize;
        char* pShaderSource = new char[sizeSum + 1];
        OVR_strcpy(pShaderSource, sizeSum + 1, shaderPrefix);
        OVR_strcpy(pShaderSource + shaderPrefixSize, shaderSize + 1, VShaderSrcs[i]);

        VertexShaders[i] = *new Shader(this, Shader_Vertex, pShaderSource);

        delete[] pShaderSource;
    }

    for (int i = 0; i < FShader_Count; i++)
    {
        const size_t shaderSize = strlen(FShaderSrcs[i]);
        const size_t sizeSum = shaderPrefixSize + shaderSize;
        char* pShaderSource = new char[sizeSum + 1];
        OVR_strcpy(pShaderSource, sizeSum + 1, shaderPrefix);
        OVR_strcpy(pShaderSource + shaderPrefixSize, shaderSize + 1, FShaderSrcs[i]);

        OVR_ASSERT ( FShaderSrcs[i] != NULL );      // You forgot a shader!

        FragShaders[i] = *new Shader(this, Shader_Fragment, pShaderSource);

        delete[] pShaderSource;
    }

    Ptr<ShaderSet> gouraudShaders = *new ShaderSet();
    gouraudShaders->SetShader(VertexShaders[VShader_MVP]);
    gouraudShaders->SetShader(FragShaders[FShader_Gouraud]);
    DefaultFill = *new ShaderFill(gouraudShaders);

    DefaultTextureFill        = CreateTextureFill ( NULL, false, false );
    DefaultTextureFillAlpha   = CreateTextureFill ( NULL, true, false );
    DefaultTextureFillPremult = CreateTextureFill ( NULL, false, true );
    // One day, I will understand smart pointers. Today is not that day...
    DefaultTextureFill->Release();
    DefaultTextureFillAlpha->Release();
    DefaultTextureFillPremult->Release();

    glGenFramebuffers(1, &CurrentFbo);
    glGenFramebuffers(1, &MsaaFbo);

    if (GLVersionInfo.SupportsVAO)
    {
        glGenVertexArrays(1, &Vao);
    }

    Blitter = *new GLUtil::Blitter();
    Blitter->Initialize();
}

RenderDevice::~RenderDevice()
{
    Shutdown();
}

void RenderDevice::Shutdown()
{
    // Release any other resources first.
    OVR::Render::RenderDevice::Shutdown();

    // This runs before the subclass's Shutdown(), where the context, etc, may be deleted.

    glDeleteFramebuffers(1, &CurrentFbo);
    glDeleteFramebuffers(1, &MsaaFbo);

    if (GLVersionInfo.SupportsVAO)
    {
        glDeleteVertexArrays(1, &Vao);
    }

    for (int i = 0; i < VShader_Count; ++i)
    {
        VertexShaders[i].Clear();
    }

    for (int i = 0; i < FShader_Count; ++i)
    {
        FragShaders[i].Clear();
    }

    DefaultFill.Clear();
    DepthBuffers.Clear();

    DebugCallbackControl.Shutdown();
}


void RenderDevice::FillTexturedRect(float left, float top, float right, float bottom, float ul, float vt, float ur, float vb, Color c, Ptr<OVR::Render::Texture> tex, const Matrix4f* view, bool premultAlpha /*= false*/)
{
    Render::RenderDevice::FillTexturedRect(left, top, right, bottom, ul, vt, ur, vb, c, tex, view, premultAlpha);
}


Shader *RenderDevice::LoadBuiltinShader(ShaderStage stage, int shader)
{
    switch (stage)
    {
    case Shader_Vertex:
        return VertexShaders[shader];
    case Shader_Fragment:
        return FragShaders[shader];
    default:
        return NULL;
    }
}


void RenderDevice::BeginRendering()
{
    glEnable(GL_DEPTH_TEST);
    glEnable(GL_CULL_FACE);
    glFrontFace(GL_CW);

    // All blending is premultiplied alpha. If the source actually needs lerp, the shader will convert it.
    glEnable(GL_LINE_SMOOTH);
    glEnable(GL_BLEND);
    glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
}

void RenderDevice::SetDepthMode(bool enable, bool write, CompareFunc func)
{
    if (enable)
    {
        glEnable(GL_DEPTH_TEST);
        glDepthMask(write);
        switch (func)
        {
        case Compare_Always:
            glDepthFunc(GL_ALWAYS);
            break;
        case Compare_Less:
            glDepthFunc(GL_LESS);
            break;
        case Compare_Greater:
            glDepthFunc(GL_GREATER);
            break;
        default:
            assert(0);
        }
    }
    else
        glDisable(GL_DEPTH_TEST);
}

void RenderDevice::SetViewport(const Recti& vp)
{
    glViewport(vp.x, vp.y, vp.w, vp.h);
}

void RenderDevice::Flush()
{
    glFlush();
}

void RenderDevice::WaitUntilGpuIdle()
{
    glFlush();
    glFinish();
}

void RenderDevice::Clear(float r, float g, float b, float a, float depth, bool clearColor /*= true*/, bool clearDepth /*= true*/)
{
    glClearColor(r,g,b,a);
    glClearDepth(depth);
    glClear(
        ( clearColor ? ( GL_COLOR_BUFFER_BIT ) : 0 ) |
        ( clearDepth ? ( GL_DEPTH_BUFFER_BIT ) : 0 )
    );
}

void RenderDevice::DeleteFills()
{
    DefaultTextureFill.Clear();
    DefaultTextureFillAlpha.Clear();
    DefaultTextureFillPremult.Clear();
}


Texture* RenderDevice::GetDepthBuffer(int w, int h, int ms)
{
    for (unsigned i = 0; i < DepthBuffers.GetSize(); i++)
        if (w == DepthBuffers[i]->Width && h == DepthBuffers[i]->Height && ms == DepthBuffers[i]->GetSamples())
            return DepthBuffers[i];

    Ptr<Texture> newDepth = *CreateTexture(Texture_Depth32f|Texture_RenderTarget|ms, w, h, NULL);
    DepthBuffers.PushBack(newDepth);
    return newDepth.GetPtr();
}

void RenderDevice::ResolveMsaa(OVR::Render::Texture* msaaTex, OVR::Render::Texture* outputTex)
{
    bool isMsaaTarget = msaaTex->GetSamples() > 1;
    glBindFramebuffer( GL_READ_FRAMEBUFFER, MsaaFbo );
    glFramebufferTexture2D( GL_READ_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                            isMsaaTarget ? GL_TEXTURE_2D_MULTISAMPLE : GL_TEXTURE_2D,
                            ((Texture*)msaaTex)->GetTexId(), 0);
    glFramebufferRenderbuffer(GL_READ_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, 0);
    OVR_ASSERT(CheckFramebufferStatus(GL_READ_FRAMEBUFFER));

    glBindFramebuffer( GL_DRAW_FRAMEBUFFER, CurrentFbo );
    glFramebufferTexture2D(GL_DRAW_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, ((Texture*)outputTex)->GetTexId(), 0);
    glFramebufferRenderbuffer(GL_DRAW_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, 0);
    OVR_ASSERT(CheckFramebufferStatus(GL_DRAW_FRAMEBUFFER));

    //glReadBuffer(GL_TEXTURE_2D_MULTISAMPLE);
    //glDrawBuffer(GL_COLOR_ATTACHMENT0);
    glBlitFramebuffer( 0, 0, msaaTex->GetWidth(), msaaTex->GetHeight(),
                       0, 0, outputTex->GetWidth(), outputTex->GetHeight(), GL_COLOR_BUFFER_BIT, GL_NEAREST );
    glBindFramebuffer( GL_FRAMEBUFFER, 0 );
    GLint err = glGetError();
    OVR_ASSERT_AND_UNUSED(!err, err);
}

void RenderDevice::SetCullMode(CullMode cullMode)
{
    switch (cullMode)
    {
    case OVR::Render::RenderDevice::Cull_Off:
        glDisable(GL_CULL_FACE);
        break;
    case OVR::Render::RenderDevice::Cull_Back:
        glEnable(GL_CULL_FACE);
        glFrontFace(GL_CW);
        break;
    case OVR::Render::RenderDevice::Cull_Front:
        glEnable(GL_CULL_FACE);
        glFrontFace(GL_CCW);
        break;
    default:
        OVR_FAIL();
        break;
    }
}

void RenderDevice::SetRenderTarget(Render::Texture* color, Render::Texture* depth, Render::Texture* stencil)
{
    OVR_UNUSED(stencil);

    CurRenderTarget = (Texture*)color;
    if (color == NULL)
    {
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        return;
    }

    int sampleCount = CurRenderTarget->GetSamples();

    if (depth == NULL)
        depth = GetDepthBuffer(color->GetWidth(), color->GetHeight(), sampleCount);

    glBindFramebuffer(GL_FRAMEBUFFER, CurrentFbo);

    GLenum texTarget = (sampleCount > 1) ? GL_TEXTURE_2D_MULTISAMPLE : GL_TEXTURE_2D;

    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, texTarget, ((Texture*)color)->GetTexId(), 0);
    if (depth)
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, texTarget, ((Texture*)depth)->GetTexId(), 0);
    else
        glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, 0);

    OVR_ASSERT(CheckFramebufferStatus(GL_DRAW_FRAMEBUFFER));

    if (color->GetFormat() & Texture_SRGB)
        glEnable(GL_FRAMEBUFFER_SRGB);
    else
        glDisable(GL_FRAMEBUFFER_SRGB);
}


void RenderDevice::SetWorldUniforms(const Matrix4f& proj)
{
    Proj = proj.Transposed();
}

void RenderDevice::SetTexture(Render::ShaderStage, int slot, const Texture* t)
{
    glActiveTexture(GL_TEXTURE0 + slot);
    glBindTexture((t->GetSamples() > 1) ? GL_TEXTURE_2D_MULTISAMPLE : GL_TEXTURE_2D, ((Texture*)t)->GetTexId());
}

Buffer* RenderDevice::CreateBuffer()
{
    return new Buffer(this);
}

Fill* RenderDevice::GetSimpleFill(int flags)
{
    OVR_UNUSED(flags);
    return DefaultFill;
}

Fill* RenderDevice::GetTextureFill(Render::Texture* t, bool useAlpha, bool usePremult)
{
    Fill *f = DefaultTextureFill;
    if ( usePremult )
    {
        f = DefaultTextureFillPremult;
    }
    else if ( useAlpha )
    {
        f = DefaultTextureFillAlpha;
    }
    f->SetTexture(0, t);
    return f;
}

void RenderDevice::Blt(Render::Texture* texture)
{
    Texture* tex = (Texture*)texture;
    Blitter->Blt(tex->GetTexId());
}

void RenderDevice::Render(const Matrix4f& matrix, Model* model)
{
    if (GLVersionInfo.SupportsVAO)
    {
        glBindVertexArray(Vao);
    }

    // Store data in buffers if not already
    if (!model->VertexBuffer)
    {
        Ptr<Render::Buffer> vb = *CreateBuffer();
        vb->Data(Buffer_Vertex | Buffer_ReadOnly, &model->Vertices[0], model->Vertices.GetSize() * sizeof(Vertex));
        model->VertexBuffer = vb;
    }

    if (!model->IndexBuffer)
    {
        Ptr<Render::Buffer> ib = *CreateBuffer();
        ib->Data(Buffer_Index | Buffer_ReadOnly, &model->Indices[0], model->Indices.GetSize() * 2);
        model->IndexBuffer = ib;
    }

    Render(model->Fill ? (const Fill*)model->Fill : (const Fill*)DefaultFill,
           model->VertexBuffer, model->IndexBuffer,
           matrix, 0, (int)model->Indices.GetSize(), model->GetPrimType());
}

void RenderDevice::Render(const Fill* fill, Render::Buffer* vertices, Render::Buffer* indices,
                          const Matrix4f& matrix, int offset, int count, PrimitiveType rprim, MeshType meshType /*= Mesh_Scene*/)
{
    ShaderSet* shaders = (ShaderSet*) ((ShaderFill*)fill)->GetShaders();

    GLenum prim;
    switch (rprim)
    {
    case Prim_Triangles:
        prim = GL_TRIANGLES;
        break;
    case Prim_Lines:
        prim = GL_LINES;
        break;
    case Prim_TriangleStrip:
        prim = GL_TRIANGLE_STRIP;
        break;
    default:
        assert(0);
        return;
    }

    fill->Set();
    if (shaders->ProjLoc >= 0)
        glUniformMatrix4fv(shaders->ProjLoc, 1, 0, &Proj.M[0][0]);
    if (shaders->ViewLoc >= 0)
        glUniformMatrix4fv(shaders->ViewLoc, 1, 0, &matrix.Transposed().M[0][0]);

    if (shaders->UsesLighting && Lighting->Version != shaders->LightingVer)
    {
        shaders->LightingVer = Lighting->Version;
        Lighting->Set(shaders);
    }

    glBindBuffer(GL_ARRAY_BUFFER, ((Buffer*)vertices)->GLBuffer);
    for (int i = 0; i < 5; i++)
        glEnableVertexAttribArray(i);

    switch (meshType)
    {
    case Mesh_Distortion:
        glVertexAttribPointer(0, 2, GL_FLOAT,         false, sizeof(DistortionVertex), reinterpret_cast<char*>(offset) + OVR_OFFSETOF(DistortionVertex, Pos));
        glVertexAttribPointer(1, 4, GL_UNSIGNED_BYTE, true,  sizeof(DistortionVertex), reinterpret_cast<char*>(offset) + OVR_OFFSETOF(DistortionVertex, Col));
        glVertexAttribPointer(2, 2, GL_FLOAT,         false, sizeof(DistortionVertex), reinterpret_cast<char*>(offset) + OVR_OFFSETOF(DistortionVertex, TexR));
        glVertexAttribPointer(3, 2, GL_FLOAT,         false, sizeof(DistortionVertex), reinterpret_cast<char*>(offset) + OVR_OFFSETOF(DistortionVertex, TexG));
        glVertexAttribPointer(4, 2, GL_FLOAT,         false, sizeof(DistortionVertex), reinterpret_cast<char*>(offset) + OVR_OFFSETOF(DistortionVertex, TexB));
        break;

    case Mesh_Heightmap:
        glVertexAttribPointer(0, 2, GL_FLOAT, false, sizeof(HeightmapVertex), reinterpret_cast<char*>(offset) + OVR_OFFSETOF(HeightmapVertex, Pos));
        glVertexAttribPointer(1, 2, GL_FLOAT, false, sizeof(HeightmapVertex), reinterpret_cast<char*>(offset) + OVR_OFFSETOF(HeightmapVertex, Tex));
        break;

    default:
        glVertexAttribPointer(0, 3, GL_FLOAT,         false, sizeof(Vertex), reinterpret_cast<char*>(offset) + OVR_OFFSETOF(Vertex, Pos));
        glVertexAttribPointer(1, 4, GL_UNSIGNED_BYTE, true,  sizeof(Vertex), reinterpret_cast<char*>(offset) + OVR_OFFSETOF(Vertex, C));
        glVertexAttribPointer(2, 2, GL_FLOAT,         false, sizeof(Vertex), reinterpret_cast<char*>(offset) + OVR_OFFSETOF(Vertex, U));
        glVertexAttribPointer(3, 2, GL_FLOAT,         false, sizeof(Vertex), reinterpret_cast<char*>(offset) + OVR_OFFSETOF(Vertex, U2));
        glVertexAttribPointer(4, 3, GL_FLOAT,         false, sizeof(Vertex), reinterpret_cast<char*>(offset) + OVR_OFFSETOF(Vertex, Norm));
    }

    if (indices)
    {
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ((Buffer*)indices)->GLBuffer);
        glDrawElements(prim, count, GL_UNSIGNED_SHORT, NULL);
    }
    else
    {
        glDrawArrays(prim, 0, count);
    }

    for (int i = 0; i < 5; i++)
        glDisableVertexAttribArray(i);
}

void RenderDevice::RenderWithAlpha(const Fill* fill, Render::Buffer* vertices, Render::Buffer* indices,
                                   const Matrix4f& matrix, int offset, int count, PrimitiveType rprim)
{
    //glEnable(GL_BLEND);
    Render(fill, vertices, indices, matrix, offset, count, rprim);
    //glDisable(GL_BLEND);
}

void RenderDevice::SetLighting(const LightingParams* lt)
{
    Lighting = lt;
}

Buffer::~Buffer()
{
    if (GLBuffer)
        glDeleteBuffers(1, &GLBuffer);
}

bool Buffer::Data(int use, const void* buffer, size_t size)
{
    switch (use & Buffer_TypeMask)
    {
    case Buffer_Index:
        Use = GL_ELEMENT_ARRAY_BUFFER;
        break;
    default:
        Use = GL_ARRAY_BUFFER;
        break;
    }

    if (!GLBuffer)
        glGenBuffers(1, &GLBuffer);

    int mode = GL_DYNAMIC_DRAW;
    if (use & Buffer_ReadOnly)
        mode = GL_STATIC_DRAW;

    glBindBuffer(Use, GLBuffer);
    glBufferData(Use, size, buffer, mode);
    return 1;
}

void* Buffer::Map(size_t, size_t, int)
{
    int mode = GL_WRITE_ONLY;
    //if (flags & Map_Unsynchronized)
    //    mode |= GL_MAP_UNSYNCHRONIZED;

    glBindBuffer(Use, GLBuffer);
    void* v = glMapBuffer(Use, mode);
    return v;
}

bool Buffer::Unmap(void*)
{
    glBindBuffer(Use, GLBuffer);
    int r = glUnmapBuffer(Use);
    return r != 0;
}

Shader::~Shader()
{
    if (GLShader)
        glDeleteShader(GLShader);
}

bool Shader::Compile(const char* src)
{
    if (!GLShader)
        GLShader = glCreateShader(GLStage());

    glShaderSource(GLShader, 1, &src, 0);
    glCompileShader(GLShader);
    GLint r;
    glGetShaderiv(GLShader, GL_COMPILE_STATUS, &r);
    if (!r)
    {
        GLchar msg[1024];
        glGetShaderInfoLog(GLShader, sizeof(msg), 0, msg);
        if (msg[0])
            OVR_DEBUG_LOG(("Compiling shader\n%s\nfailed: %s\n", src, msg));
        if (!r)
            return 0;
    }
    return 1;
}

ShaderSet::ShaderSet() :
    //Prog(0),
    UniformInfo(),
    ProjLoc(0),
    ViewLoc(0),
    //TexLoc[8];
    UsesLighting(false),
    LightingVer(0)
{
    memset(TexLoc, 0, sizeof(TexLoc));
    Prog = glCreateProgram();
}
ShaderSet::~ShaderSet()
{
    glDeleteProgram(Prog);
}

void ShaderSet::SetShader(Render::Shader *s)
{
    Shaders[s->GetStage()] = s;
    Shader* gls = (Shader*)s;
    glAttachShader(Prog, gls->GLShader);
    if (Shaders[Shader_Vertex] && Shaders[Shader_Fragment])
        Link();
}

void ShaderSet::UnsetShader(int stage)
{
    Shader* gls = (Shader*)(Render::Shader*)Shaders[stage];
    if (gls)
        glDetachShader(Prog, gls->GLShader);
    Shaders[stage] = NULL;
}

bool ShaderSet::Link()
{
    glBindAttribLocation(Prog, 0, "Position");
    glBindAttribLocation(Prog, 1, "Color");
    glBindAttribLocation(Prog, 2, "TexCoord");
    glBindAttribLocation(Prog, 3, "TexCoord1");
    glBindAttribLocation(Prog, 4, "Normal");

    glLinkProgram(Prog);
    GLint r;
    glGetProgramiv(Prog, GL_LINK_STATUS, &r);
    if (!r)
    {
        GLchar msg[1024];
        glGetProgramInfoLog(Prog, sizeof(msg), 0, msg);
        OVR_DEBUG_LOG(("Linking shaders failed: %s\n", msg));
        if (!r)
            return 0;
    }
    glUseProgram(Prog);

    UniformInfo.Clear();
    LightingVer = 0;
    UsesLighting = 0;

    GLint uniformCount = 0;
    glGetProgramiv(Prog, GL_ACTIVE_UNIFORMS, &uniformCount);
    OVR_ASSERT(uniformCount >= 0);

    for(GLuint i = 0; i < (GLuint)uniformCount; i++)
    {
        GLsizei namelen;
        GLint size = 0;
        GLenum type;
        GLchar name[32];
        glGetActiveUniform(Prog, i, sizeof(name), &namelen, &size, &type, name);

        if (size)
        {
            int l = glGetUniformLocation(Prog, name);
            char *np = name;
            while (*np)
            {
                if (*np == '[')
                    *np = 0;
                np++;
            }
            Uniform u;
            u.Name = name;
            u.Location = l;
            u.Size = size;
            switch (type)
            {
            case GL_FLOAT:
                u.Type = 1;
                break;
            case GL_FLOAT_VEC2:
                u.Type = 2;
                break;
            case GL_FLOAT_VEC3:
                u.Type = 3;
                break;
            case GL_FLOAT_VEC4:
                u.Type = 4;
                break;
            case GL_FLOAT_MAT3:
                u.Type = 12;
                break;
            case GL_FLOAT_MAT4:
                u.Type = 16;
                break;
            default:
                continue;
            }
            UniformInfo.PushBack(u);
            if (!strcmp(name, "LightCount"))
                UsesLighting = 1;
        }
        else
            break;
    }

    ProjLoc = glGetUniformLocation(Prog, "Proj");
    ViewLoc = glGetUniformLocation(Prog, "View");
    for (int i = 0; i < 8; i++)
    {
        char texv[32];
        OVR_sprintf(texv, 10, "Texture%d", i);
        TexLoc[i] = glGetUniformLocation(Prog, texv);
        if (TexLoc[i] < 0)
            break;

        glUniform1i(TexLoc[i], i);
    }
    if (UsesLighting)
        OVR_ASSERT(ProjLoc >= 0 && ViewLoc >= 0);
    return 1;
}

void ShaderSet::Set(PrimitiveType) const
{
    glUseProgram(Prog);
}

bool ShaderSet::SetUniform(const char* name, int n, const float* v)
{
    for (unsigned int i = 0; i < UniformInfo.GetSize(); i++)
        if (!strcmp(UniformInfo[i].Name.ToCStr(), name))
        {
            OVR_ASSERT(UniformInfo[i].Location >= 0);
            glUseProgram(Prog);
            switch (UniformInfo[i].Type)
            {
            case 1:
                glUniform1fv(UniformInfo[i].Location, n, v);
                break;
            case 2:
                glUniform2fv(UniformInfo[i].Location, n/2, v);
                break;
            case 3:
                glUniform3fv(UniformInfo[i].Location, n/3, v);
                break;
            case 4:
                glUniform4fv(UniformInfo[i].Location, n/4, v);
                break;
            case 12:
                glUniformMatrix3fv(UniformInfo[i].Location, 1, 1, v);
                break;
            case 16:
                glUniformMatrix4fv(UniformInfo[i].Location, 1, 1, v);
                break;
            default:
                OVR_ASSERT(0);
            }
            return 1;
        }

    OVR_DEBUG_LOG(("Warning: uniform %s not present in selected shader", name));
    return 0;
}

bool ShaderSet::SetUniform4x4f(const char* name, const Matrix4f& m)
{
    for (unsigned int i = 0; i < UniformInfo.GetSize(); i++)
        if (!strcmp(UniformInfo[i].Name.ToCStr(), name))
        {
            glUseProgram(Prog);
            glUniformMatrix4fv(UniformInfo[i].Location, 1, 1, &m.M[0][0]);
            return 1;
        }

    OVR_DEBUG_LOG(("Warning: uniform %s not present in selected shader", name));
    return 0;
}

Texture::Texture(ovrHmd hmd, RenderDevice* r, int fmt, int w, int h, int samples)
    : Hmd(hmd)
    , Ren(r)
    , Format(fmt)
    , Width(w)
    , Height(h)
    , Samples(samples)
    , TextureSet(nullptr)
    , MirrorTexture(nullptr)
    , TexId(0)
{
}

Texture::~Texture()
{
    if (TextureSet)
    {
        ovr_DestroySwapTextureSet(Hmd, TextureSet);
        TextureSet = nullptr;
    }

    if (MirrorTexture)
    {
        ovr_DestroyMirrorTexture(Hmd, MirrorTexture);
        MirrorTexture = nullptr;
    }
}

void Texture::Set(int slot, Render::ShaderStage stage) const
{
    Ren->SetTexture(stage, slot, this);
}

void Texture::SetSampleMode(int sm)
{
    glBindTexture((GetSamples() > 1) ? GL_TEXTURE_2D_MULTISAMPLE : GL_TEXTURE_2D, GetTexId());
    switch (sm & Sample_FilterMask)
    {
    case Sample_Linear:
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        if(GLE_EXT_texture_filter_anisotropic)
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_ANISOTROPY_EXT, 1);
        break;

    case Sample_Anisotropic:
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        if(GLE_EXT_texture_filter_anisotropic)
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_ANISOTROPY_EXT, 4);
        break;

    case Sample_Nearest:
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        if(GLE_EXT_texture_filter_anisotropic)
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_ANISOTROPY_EXT, 1);
        break;
    }

    switch (sm & Sample_AddressMask)
    {
    case Sample_Repeat:
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
        break;

    case Sample_Clamp:
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        break;

    case Sample_ClampBorder:
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_BORDER);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_BORDER);
        break;
    }
}

ovrTexture Texture::Get_ovrTexture()
{
    ovrTexture tex;
    OVR::Sizei newRTSize(Width, Height);

    ovrGLTextureData* texData = (ovrGLTextureData*)&tex;
    texData->Header.API            = ovrRenderAPI_OpenGL;
    texData->Header.TextureSize    = newRTSize;
    texData->TexId                 = GetTexId();

    return tex;
}


Texture* RenderDevice::CreateTexture(int format, int width, int height, const void* data, int mipcount, ovrResult* error)
{
    if (error != nullptr)
        *error = ovrSuccess;

    static bool knownVendor = false;
    static const char* atiVendor = nullptr;

    // If this is a simple texture, complete our initialization.
    // AMD cards require this initialization as well since they
    // do not properly implement the DX/GL interop
    if (!knownVendor)
    {
        const GLubyte* vendor = glGetString(GL_VENDOR);

        if (vendor)
        {
            atiVendor = strstr((const char*)vendor, "ATI");
            knownVendor = true;
        }
    }
    bool furtherInitialization = true;

    bool isDepth = ((format & Texture_DepthMask) != 0);
    bool isSRGB = !isDepth && ((format & Texture_SRGB) != 0);

    GLenum   glformat, gltype = GL_UNSIGNED_BYTE;
    switch(format & Texture_TypeMask)
    {
    case Texture_BGRA:
        glformat = GL_BGRA;
        break;
    case Texture_RGBA:
        glformat = GL_RGBA;
        break;
    case Texture_R:
        glformat = GL_RED;
        break;
    case Texture_Depth32f:
        glformat = GL_DEPTH_COMPONENT;
        gltype = GL_FLOAT;
        break;
    case Texture_Depth16:
        OVR_ASSERT(false); /* untested */ glformat = GL_DEPTH_COMPONENT16;
        gltype = GL_UNSIGNED_SHORT;
        break;
    case Texture_Depth24Stencil8:
        OVR_ASSERT(false); /* untested */ glformat = GL_DEPTH24_STENCIL8;
        gltype = GL_UNSIGNED_INT;
        break;
    case Texture_Depth32fStencil8:
        OVR_ASSERT(false); /* untested */ glformat = GL_DEPTH32F_STENCIL8;
        gltype = GL_FLOAT;
        break;
    case Texture_DXT1:
        glformat = isSRGB ? GL_COMPRESSED_SRGB_ALPHA_S3TC_DXT1_EXT : GL_COMPRESSED_RGBA_S3TC_DXT1_EXT;
        break;
    case Texture_DXT3:
        glformat = isSRGB ? GL_COMPRESSED_SRGB_ALPHA_S3TC_DXT3_EXT : GL_COMPRESSED_RGBA_S3TC_DXT3_EXT;
        break;
    case Texture_DXT5:
        glformat = isSRGB ? GL_COMPRESSED_SRGB_ALPHA_S3TC_DXT5_EXT : GL_COMPRESSED_RGBA_S3TC_DXT5_EXT;
        break;
    default:
        return NULL;
    }
    int samples = format & Texture_SamplesMask;
    if(samples < 1 || !GLE_ARB_texture_multisample) // disallow MSAA for OpenGL versions that don't support it.
    {
        samples = 1;
    }

    GLenum textureTarget = (samples > 1) ? GL_TEXTURE_2D_MULTISAMPLE : GL_TEXTURE_2D;

    Texture* NewTex = new Texture(Hmd, this, format, width, height, samples);

    if (format & Texture_Compressed)
    {
        glGenTextures(1, &NewTex->TexId);
        glBindTexture(textureTarget, NewTex->GetTexId());
        GLint err = glGetError();

#if ! defined(OVR_OS_MAC)
        OVR_ASSERT(!err);
#endif

        if (err)
        {
            printf("RenderDevice::CreateTexture glGetError result: %d\n", err);
        }

        if (GLE_EXT_texture_compression_s3tc) // If compressed textures are supported (they typically are)...
        {
            const unsigned char* level = (const unsigned char*)data;
            int w = width, h = height;
            for (int i = 0; i < mipcount; i++)
            {
                int mipsize = GetTextureSize(format, w, h);
                glCompressedTexImage2D(GL_TEXTURE_2D, i, glformat, w, h, 0, mipsize, level);

                level += mipsize;
                w >>= 1;
                h >>= 1;
                if (w < 1) w = 1;
                if (h < 1) h = 1;
            }
        }
        else
        {
            int w = width, h = height;
            unsigned char r = 64 + (rand() % (256 - 64));

            for (int i = 0; i < mipcount; i++)
            {
                unsigned char* redData = new unsigned char[w * h];
                memset(redData, r, w * h);
                glTexImage2D(GL_TEXTURE_2D, i, GL_RED, w, h, 0, GL_RED, GL_UNSIGNED_BYTE, redData);
                delete[] redData;

                w >>= 1;
                h >>= 1;
                if (w < 1) w = 1;
                if (h < 1) h = 1;
            }
        }
    }
    else
    {
        GLenum internalFormat = (isSRGB) ? GL_SRGB8_ALPHA8 : (isDepth) ?
                                (GLE_ARB_depth_buffer_float ? GL_DEPTH_COMPONENT32F : GL_DEPTH_COMPONENT) : glformat;

        if (format & Texture_Mirror)
        {
            ovrResult result = ovr_CreateMirrorTextureGL(Hmd, internalFormat, width, height, &NewTex->MirrorTexture);
            if (error != nullptr)
                *error = result;

            if (result == ovrError_DisplayLost || !NewTex->MirrorTexture)
            {
                OVR_ASSERT(false);
                return nullptr;
            }

            NewTex->TexId = ((ovrGLTexture*)NewTex->MirrorTexture)->OGL.TexId;
            furtherInitialization = false;
        }
        else if (format & Texture_SwapTextureSet)
        {
            // Can do this with rendertargets, depth buffers, or normal textures, but *not* MSAA.
            OVR_ASSERT ( samples == 1);
            ovrResult result = ovr_CreateSwapTextureSetGL(Hmd, internalFormat, width, height, &NewTex->TextureSet);
            if (error != nullptr)
                *error = result;

            if (result == ovrError_DisplayLost || !NewTex->TextureSet)
            {
                OVR_ASSERT(false);
                return nullptr;
            }

            furtherInitialization = false;
        }
        else
        {
            glGenTextures(1, &NewTex->TexId);
        }

        OVR_ASSERT(NewTex->GetTexId());

        glBindTexture(textureTarget, NewTex->GetTexId());
        GLint err = glGetError();

#if ! defined(OVR_OS_MAC)
        OVR_ASSERT(!err);
#endif

        if (err)
        {
            printf("RenderDevice::CreateTexture glGetError result: %d\n", err);
        }

        if (samples > 1)
            glTexImage2DMultisample(GL_TEXTURE_2D_MULTISAMPLE, samples, internalFormat, width, height, false);
        else if ((format & Texture_Mirror) == 0)
        {
            int textureCount = 1;
            if (format & Texture_SwapTextureSet)
            {
                textureCount = NewTex->TextureSet->TextureCount;
            }

            GLuint* textureId = new GLuint[textureCount];

            if (textureCount == 1)
            {
                textureId[0] = NewTex->GetTexId();
            }
            else
            {
                for (int i = 0; i < textureCount; ++i)
                {
                    textureId[i] = ((ovrGLTexture*)&NewTex->TextureSet->Textures[i])->OGL.TexId;
                }
            }

            for (int i = 0; i < textureCount; ++i)
            {
                glBindTexture(textureTarget, textureId[i]);

                // For DX interop textures glTexImage2D needs to be called on AMD hardware. The data parameter
                // is not honored however, so we will need to initialize it with data later.
                if (furtherInitialization || atiVendor)
                {
                    glTexImage2D(GL_TEXTURE_2D, 0, internalFormat, width, height, 0, glformat, gltype, data);
                }

                // For Nvidia, glTexImage2D cannot be called or an error will be thrown. Instead we initialize
                // data with glTexSubImage2D which on Nvidia works fine. This call on AMD locks up machines
                // hard, so we have to use glBlitFramebuffer from a non-shared texture to do the update.
                if ((format & Texture_SwapTextureSet) && data)
                {
                    if (!atiVendor)
                    {
                        // NVIDIA path
                        glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, width, height, glformat, gltype, data);
                    }
                    else
                    {
                        // AMD path
                        GLuint fb[2];
                        GLuint tex;

                        // Save relevant state.
                        GLint readFrameBufferBindingSaved, drawBufferBindingSaved;
                        glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &readFrameBufferBindingSaved);
                        glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &drawBufferBindingSaved);

                        GLint readBufferSaved;
                        glGetIntegerv(GL_READ_BUFFER, &readBufferSaved);

                        // Generate temporary texture and fbs
                        glGenTextures(1, &tex);
                        glGenFramebuffers(2, fb);

                        // Create temporary texture initialized with our data and bind it to a framebuffer
                        glBindTexture(GL_TEXTURE_2D, tex);
                        glTexImage2D(GL_TEXTURE_2D, 0, internalFormat, width, height, 0, glformat, gltype, data);
                        glBindFramebuffer(GL_FRAMEBUFFER, fb[0]);
                        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, tex, 0);
                        OVR_ASSERT(CheckFramebufferStatus(GL_DRAW_FRAMEBUFFER));

                        // Bind our shared texture to another frame buffer
                        glBindFramebuffer(GL_FRAMEBUFFER, fb[1]);
                        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, textureId[i], 0);
                        glDrawBuffer(GL_COLOR_ATTACHMENT0);
                        OVR_ASSERT(CheckFramebufferStatus(GL_DRAW_FRAMEBUFFER));

                        // Do blit
                        glBindFramebuffer(GL_READ_FRAMEBUFFER, fb[0]);
                        glBindFramebuffer(GL_DRAW_FRAMEBUFFER, fb[1]);
                        glBlitFramebuffer(0, 0, width, height, 0, 0, width, height, GL_COLOR_BUFFER_BIT, GL_LINEAR);
                        OVR_ASSERT(!glGetError());

                        // Clean up
                        glDeleteFramebuffers(2, fb);
                        glDeleteTextures(1, &tex);

                        // Restore state
                        glReadBuffer(readBufferSaved);
                        glBindFramebuffer(GL_DRAW_FRAMEBUFFER, drawBufferBindingSaved);
                        glBindFramebuffer(GL_READ_FRAMEBUFFER, readFrameBufferBindingSaved);
                    }
                }
            }

            delete[] textureId;
        }

    }

    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

    if (format == (Texture_RGBA|Texture_GenMipmaps)) // not render target
    {
        int srcw = width, srch = height;
        int level = 0;
        uint8_t* mipmaps = NULL;
        do
        {
            level++;
            int mipw = srcw >> 1;
            if (mipw < 1) mipw = 1;
            int miph = srch >> 1;
            if (miph < 1) miph = 1;
            if (mipmaps == NULL)
                mipmaps = (uint8_t*)OVR_ALLOC(mipw * miph * 4);
            FilterRgba2x2(level == 1 ? (const uint8_t*)data : mipmaps, srcw, srch, mipmaps);
            glTexImage2D(GL_TEXTURE_2D, level, glformat, mipw, miph, 0, glformat, gltype, mipmaps);
            srcw = mipw;
            srch = miph;
        }
        while (srcw > 1 || srch > 1);
        if (mipmaps)
            OVR_FREE(mipmaps);
        glTexParameteri(textureTarget, GL_TEXTURE_MAX_LEVEL, level);
    }
    else if (furtherInitialization || atiVendor)
    {
        glTexParameteri(textureTarget, GL_TEXTURE_MAX_LEVEL, mipcount - 1);
    }

    OVR_ASSERT(!glGetError());
    return NewTex;
}

RBuffer::RBuffer(GLenum format, GLint w, GLint h)
{
    Width = w;
    Height = h;
    glGenRenderbuffers(1, &BufId);
    glBindRenderbuffer(GL_RENDERBUFFER, BufId);
    glRenderbufferStorage(GL_RENDERBUFFER, format, w, h);
    glBindRenderbuffer(GL_RENDERBUFFER, 0);
}

RBuffer::~RBuffer()
{
    if (BufId)
        glDeleteRenderbuffers(1, &BufId);
}






DebugCallback::DebugCallback()
    : Initialized(false),
      MinLogSeverity(SeverityHigh),
      MinAssertSeverity(SeverityHigh)
      //glDebugMessageCallback(NULL),
      //glDebugMessageControl(NULL),
      //glDebugMessageCallbackARB(NULL),
      //glDebugMessageControlARB(NULL),
      //glDebugMessageCallbackAMD(NULL),
      //glDebugMessageControlAMD(NULL)
{
}


DebugCallback::~DebugCallback()
{
    Shutdown();
}


bool DebugCallback::GetGLDebugCallback(PFNGLDEBUGMESSAGECALLBACKPROC* debugCallback, const void** userParam) const
{
    // Curiously, the KHR and ARB callbacks use the same glGetPointerv defines, which means you can only have
    // one of them active concurrently. This also implies that an OpenGL implementation which implements both
    // KHR and ARB implements the latter as simply a passthrough (or alias) of the former.
    if(GLE_ARB_debug_output || GLE_KHR_debug)
    {
        // glGetPointerv requires at least OpenGL 4.3 headers and implementation,
        // but will be present in the headers if GL_ARB_debug_output or GL_KHR_debug are.
        glGetPointerv(GL_DEBUG_CALLBACK_FUNCTION, reinterpret_cast<GLvoid**>(debugCallback));
        glGetPointerv(GL_DEBUG_CALLBACK_USER_PARAM, const_cast<GLvoid**>(userParam));
        return true;
    }

    // AMD_debug_output doesn't provide an option to get the debug callback.
    debugCallback = NULL;
    userParam = NULL;
    return false;
}


void DebugCallback::DebugCallbackInternal(Severity s, const char* pSource, const char* pType, GLuint id, const char* pSeverity, const char* message) const
{
    if(s >= MinLogSeverity)
    {
        OVR::LogError("{ERR-xxxx} [GL Error] %s %s %#x %s: %s", pSource, pType, id, pSeverity, message);
    }

    if(s >= MinAssertSeverity)
    {
        OVR_FAIL_M("s < MinAssertSeverity");
    }
}


void DebugCallback::Initialize()
{
    if(!Initialized)
    {
        Initialized = true;

        int err = glGetError();
        OVR_UNUSED(err);

        // Used to see if a callback was already registered.
        PFNGLDEBUGMESSAGECALLBACKPROC debugCallbackPrev = NULL;
        const void* userParamPrev = NULL;

        GetGLDebugCallback(&debugCallbackPrev, &userParamPrev);

        if(!debugCallbackPrev) // If a callback isn't already registered...
        {
            // Try getting the KHR interface.
            if(GLE_KHR_debug)
            {
                glDebugMessageCallback(DebugMessageCallback, this);
                err = glGetError();
                if(err)
                {
                    OVR_DEBUG_LOG(("glDebugMessageCallback error: %x (%d)\n", err, err));
                }

                glEnable(GL_DEBUG_OUTPUT_SYNCHRONOUS);
                err = glGetError();
                if(err)
                {
                    OVR_DEBUG_LOG(("GL_DEBUG_OUTPUT_SYNCHRONOUS error: %x (%d)\n", err, err));
                }

                // To consider: disable marker/push/pop
                // glDebugMessageControl(GL_DEBUG_SOURCE_APPLICATION, GL_DEBUG_TYPE_MARKER,     GL_DONT_CARE, 0, NULL, GL_FALSE);
                // glDebugMessageControl(GL_DEBUG_SOURCE_APPLICATION, GL_DEBUG_TYPE_PUSH_GROUP, GL_DONT_CARE, 0, NULL, GL_FALSE);
                // glDebugMessageControl(GL_DEBUG_SOURCE_APPLICATION, GL_DEBUG_TYPE_POP_GROUP,  GL_DONT_CARE, 0, NULL, GL_FALSE);
            }
            else if(GLE_ARB_debug_output) // If KHR_debug wasn't found, try ARB_debug_output.
            {
                GetGLDebugCallback(&debugCallbackPrev, &userParamPrev);

                glDebugMessageCallbackARB(DebugMessageCallback, this);
                err = glGetError();
                if(err)
                {
                    OVR_DEBUG_LOG(("glDebugMessageCallbackARB error: %x (%d)\n", err, err));
                }

                glEnable(GL_DEBUG_OUTPUT_SYNCHRONOUS);
                err = glGetError();
                if(err)
                {
                    OVR_DEBUG_LOG(("GL_DEBUG_OUTPUT_SYNCHRONOUS error: %x (%d)\n", err, err));
                }

                // To consider: disable marker/push/pop
                // glDebugMessageControlARB(GL_DEBUG_SOURCE_APPLICATION, GL_DEBUG_TYPE_MARKER,     GL_DONT_CARE, 0, NULL, GL_FALSE);
                // glDebugMessageControlARB(GL_DEBUG_SOURCE_APPLICATION, GL_DEBUG_TYPE_PUSH_GROUP, GL_DONT_CARE, 0, NULL, GL_FALSE);
                // glDebugMessageControlARB(GL_DEBUG_SOURCE_APPLICATION, GL_DEBUG_TYPE_POP_GROUP,  GL_DONT_CARE, 0, NULL, GL_FALSE);
            }
            else if(GLE_AMD_debug_output) // If ARB_debug_output also wasn't found, try AMD_debug_output.
            {
                glDebugMessageCallbackAMD(DebugMessageCallbackAMD, this);
                err = glGetError();
                if(err)
                {
                    OVR_DEBUG_LOG(("glDebugMessageCallbackAMD error: %x (%d)\n", err, err));
                }
                // There is no control for synchronous/asynchronous with AMD_debug_output.
            }
        }
    }
}


void DebugCallback::Shutdown()
{
    if(Initialized)
    {
        // Nothing currently required.
        Initialized = false;
    }
}


void DebugCallback::SetMinSeverity(Severity minLogSeverity, Severity minAssertSeverity)
{
    MinLogSeverity    = minLogSeverity;
    MinAssertSeverity = minAssertSeverity;
}


DebugCallback::Implementation DebugCallback::GetImplementation() const
{
    if(GLE_KHR_debug)
        return ImplementationAMD;

    if(GLE_ARB_debug_output)
        return ImplementationARB;

    if(GLE_AMD_debug_output)
        return ImplementationKHR;

    return ImplementationNone;
}


void GLAPIENTRY DebugCallback::DebugMessageCallback(GLenum source, GLenum type, GLuint id, GLenum severity, GLsizei /*length*/, const GLchar* message, const void* userParam)
{
    const char* pSource   = GetSource(source);
    const char* pType     = GetType(type);
    const char* pSeverity = GetSeverity(severity);
    Severity    s;

    switch(severity)
    {
    default:
    case GL_DEBUG_SEVERITY_NOTIFICATION:
        s = SeverityNotification;
        break;
    case GL_DEBUG_SEVERITY_LOW:
        s = SeverityLow;
        break;
    case GL_DEBUG_SEVERITY_MEDIUM:
        s = SeverityMedium;
        break;
    case GL_DEBUG_SEVERITY_HIGH:
        s = SeverityHigh;
        break;
    }

    const DebugCallback* pThis = reinterpret_cast<const DebugCallback*>(userParam);
    pThis->DebugCallbackInternal(s, pSource, pType, id, pSeverity, message);
}


const char* DebugCallback::GetSource(GLenum Source)
{
    // There is one contiguous section of GL_DEBUG_SOURCE values.
    static_assert((GL_DEBUG_SOURCE_OTHER - GL_DEBUG_SOURCE_API) == 5, "GL_DEBUG_SOURCE constants are not contiguous.");

    static const char* GL_SourceStrings[] =
    {
        "API",             // GL_DEBUG_SOURCE_API
        "System",          // GL_DEBUG_SOURCE_WINDOW_SYSTEM
        "ShaderCompiler",  // GL_DEBUG_SOURCE_SHADER_COMPILER
        "ThirdParty",      // GL_DEBUG_SOURCE_THIRD_PARTY
        "Application",     // GL_DEBUG_SOURCE_APPLICATION
        "Other"            // GL_DEBUG_SOURCE_OTHER
    };

    if ((Source >= GL_DEBUG_SOURCE_API) && (Source <= GL_DEBUG_SOURCE_OTHER))
        return GL_SourceStrings[Source - GL_DEBUG_SOURCE_API];

    return "Unknown";
}



const char* DebugCallback::GetType(GLenum Type)
{
    // There are two contiguous sections of GL_DEBUG_TYPE values.
    static_assert((GL_DEBUG_TYPE_OTHER - GL_DEBUG_TYPE_ERROR) == 5, "GL_DEBUG_TYPE constants are not contiguous.");
    static const char* TypeStrings[] =
    {
        "Error",                // GL_DEBUG_TYPE_ERROR
        "Deprecated",           // GL_DEBUG_TYPE_DEPRECATED_BEHAVIOR
        "UndefinedBehavior",    // GL_DEBUG_TYPE_UNDEFINED_BEHAVIOR
        "Portability",          // GL_DEBUG_TYPE_PORTABILITY
        "Performance",          // GL_DEBUG_TYPE_PERFORMANCE
        "Other"                 // GL_DEBUG_TYPE_OTHER
    };

    if ((Type >= GL_DEBUG_TYPE_ERROR) && (Type <= GL_DEBUG_TYPE_OTHER))
        return TypeStrings[Type - GL_DEBUG_TYPE_ERROR];

    // KHR_debug marker/push/pop functionality.
    static_assert((GL_DEBUG_TYPE_POP_GROUP - GL_DEBUG_TYPE_MARKER) == 2, "GL_DEBUG_TYPE constants are not contiguous.");
    static const char* TypeStrings2[] =
    {
        "Marker",       // GL_DEBUG_TYPE_MARKER
        "PushGroup",    // GL_DEBUG_TYPE_PUSH_GROUP
        "PopGroup",     // GL_DEBUG_TYPE_POP_GROUP
    };

    if ((Type >= GL_DEBUG_TYPE_MARKER) && (Type <= GL_DEBUG_TYPE_POP_GROUP))
        return TypeStrings2[Type - GL_DEBUG_TYPE_MARKER];

    return "Unknown";
}


const char* DebugCallback::GetSeverity(GLenum Severity)
{
    // There are two sections of GL_DEBUG_SEVERITY.
    static_assert((GL_DEBUG_SEVERITY_LOW - GL_DEBUG_SEVERITY_HIGH) == 2, "GL_DEBUG_SEVERITY constants are not contiguous.");
    static const char* SeverityStrings[] =
    {
        "High",
        "Medium",
        "Low"
    };

    if ((Severity >= GL_DEBUG_SEVERITY_HIGH) && (Severity <= GL_DEBUG_SEVERITY_LOW))
        return SeverityStrings[Severity - GL_DEBUG_SEVERITY_HIGH];

    // There is just one value in this second section.
    if(Severity == GL_DEBUG_SEVERITY_NOTIFICATION)
        return "Notification";

    return "Unknown";
}


void DebugCallback::DebugMessageCallbackAMD(GLuint id, GLenum category, GLenum severity, GLsizei /*length*/, const GLchar* message, GLvoid* userParam)
{
    static_assert(GL_DEBUG_SEVERITY_LOW_AMD == GL_DEBUG_SEVERITY_LOW, "Severity mismatch"); // Verify that AMD_debug_output severity constants are identical to KHR_debug severity contstants.

    const char* pSource   = GetCategoryAMD(category);
    const char* pSeverity = GetSeverity(severity);
    Severity    s;

    switch(severity)
    {
    default:
    case GL_DEBUG_SEVERITY_NOTIFICATION:
        s = SeverityNotification;
        break;
    case GL_DEBUG_SEVERITY_LOW:
        s = SeverityLow;
        break;
    case GL_DEBUG_SEVERITY_MEDIUM:
        s = SeverityMedium;
        break;
    case GL_DEBUG_SEVERITY_HIGH:
        s = SeverityHigh;
        break;
    }

    DebugCallback* pThis = reinterpret_cast<DebugCallback*>(userParam);
    pThis->DebugCallbackInternal(s, pSource, "Other", id, pSeverity, message);
}


const char* DebugCallback::GetCategoryAMD(GLenum Category)
{
    static_assert((GL_DEBUG_CATEGORY_OTHER_AMD - GL_DEBUG_CATEGORY_API_ERROR_AMD) == 7, "GL_DEBUG_CATEGORY constants are not contiguous.");
    static const char* CategoryStrings[] =
    {
        "API",                  // GL_DEBUG_CATEGORY_API_ERROR_AMD
        "System",               // GL_DEBUG_CATEGORY_WINDOW_SYSTEM_AMD
        "Deprecation",          // GL_DEBUG_CATEGORY_DEPRECATION_AMD
        "UndefinedBehavior",    // GL_DEBUG_CATEGORY_UNDEFINED_BEHAVIOR_AMD
        "Performance",          // GL_DEBUG_CATEGORY_PERFORMANCE_AMD
        "ShaderCompiler",       // GL_DEBUG_CATEGORY_SHADER_COMPILER_AMD
        "Application",          // GL_DEBUG_CATEGORY_APPLICATION_AMD
        "Other"                 // GL_DEBUG_CATEGORY_OTHER_AMD
    };

    if((Category >= GL_DEBUG_CATEGORY_API_ERROR_AMD) && (Category <= GL_DEBUG_CATEGORY_OTHER_AMD))
        return CategoryStrings[Category - GL_DEBUG_CATEGORY_API_ERROR_AMD];

    return "Unknown";
}








void GLVersionAndExtensions::ParseGLVersion()
{
    const char* version = (const char*)glGetString(GL_VERSION);
    int fields = 0, major = 0, minor = 0;
    bool isGLES = false;

    OVR_ASSERT(version);
    if (version)
    {
        OVR_DEBUG_LOG(("GL_VERSION: %s", (const char*)version));

        // Skip all leading non-digits before reading %d.
        // Example GL_VERSION strings:
        //   "1.5 ATI-1.4.18"
        //   "OpenGL ES-CM 3.2"
        OVR_DISABLE_MSVC_WARNING(4996) // "scanf may be unsafe"
        fields = sscanf(version, isdigit(*version) ? "%d.%d" : "%*[^0-9]%d.%d", &major, &minor);
        isGLES = (strstr(version, "OpenGL ES") != NULL);
        OVR_RESTORE_MSVC_WARNING()
    }
    else
    {
        LogText("Warning: GL_VERSION was NULL\n");
    }

    // If two fields were not found,
    if (fields != 2)
    {
        static_assert(sizeof(major) == sizeof(GLint), "type mis-match");

        glGetIntegerv(GL_MAJOR_VERSION, &major);
        glGetIntegerv(GL_MINOR_VERSION, &minor);
    }

    // Write version data
    MajorVersion = major;
    MinorVersion = minor;
    WholeVersion = (major * 100) + minor;
    IsGLES = isGLES;
    IsCoreProfile = (MajorVersion >= 3); // Until we get a better way to detect core profiles, we err on the conservative side and set to true if the version is >= 3.
}


bool GLVersionAndExtensions::HasGLExtension(const char* searchKey) const
{
    if (Extensions && Extensions[0]) // If we have an extension string to search for individual extensions...
    {
        const int searchKeyLen = (int)strlen(searchKey);
        const char* p = Extensions;

        for (;;)
        {
            p = strstr(p, searchKey);

            // If not found,
            if (p == NULL)
            {
                break;
            }

            // Only match full string
            if ((p == Extensions || p[-1] == ' ') &&
                    (p[searchKeyLen] == '\0' || p[searchKeyLen] == ' '))
            {
                return true;
            }

            // Skip ahead
            p += searchKeyLen;
        }
    }
    else
    {
        if (MajorVersion >= 3) // If glGetIntegerv(GL_NUM_EXTENSIONS, ...) is supported...
        {
            GLint extensionCount = 0;
            glGetIntegerv(GL_NUM_EXTENSIONS, &extensionCount);
            GLenum err = glGetError();

            if (err == 0)
            {
                for (GLint i = 0; i != extensionCount; ++i)
                {
                    const char* extension = (const char*)glGetStringi(GL_EXTENSIONS, (GLuint)i);

                    if (extension) // glGetStringi returns NULL upon error.
                    {
                        if (strcmp(extension, searchKey) == 0)
                            return true;
                    }
                    else
                        break;
                }
            }
        }
    }

    return false;
}

void GLVersionAndExtensions::ParseGLExtensions()
{
    if (MajorVersion >= 3)
    {
        // Set to empty because we need to use glGetStringi to read extensions on recent OpenGL.
        Extensions = "";
    }
    else
    {
        const char* extensions = (const char*)glGetString(GL_EXTENSIONS);

        OVR_ASSERT(extensions);
        if (!extensions)
        {
            extensions = ""; // Note: glGetString() can return null
            LogText("Warning: GL_EXTENSIONS was NULL\n");
        }
        else
        {
            // Cannot print this to debug log: It's too long!
            //OVR_DEBUG_LOG(("GL_EXTENSIONS: %s", (const char*)extensions));
        }

        Extensions = extensions;
    }

    // To do: revise the code below to loop through calls to glGetStringi(GL_EXTENSIONS, ...) so that all extensions below
    // can be searched with a single pass over the extensions instead of a full loop per HasGLExtensionCall.

    if (MajorVersion >= 3)
    {
        SupportsVAO = true;
    }
    else
    {
        SupportsVAO =
            HasGLExtension("GL_ARB_vertex_array_object") ||
            HasGLExtension("GL_APPLE_vertex_array_object");
    }

    SupportsDrawBuffers = HasGLExtension("GL_EXT_draw_buffers2");

    // Add more extension checks here...
}

void GetGLVersionAndExtensions(GLVersionAndExtensions& versionInfo)
{
    versionInfo.ParseGLVersion();
    // GL Version must be parsed before parsing extensions:
    versionInfo.ParseGLExtensions();
    // To consider: Call to glGetStringi(GL_SHADING_LANGUAGE_VERSION, ...) check/validate the GLSL support.
}


}
}
} // namespace OVR::Render::GL