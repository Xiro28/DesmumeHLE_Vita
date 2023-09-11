/*
	Copyright (C) 2006 yopyop
	Copyright (C) 2006-2007 shash
	Copyright (C) 2008-2016 DeSmuME team

	This file is free software: you can redistribute it and/or modify
	it under the terms of the GNU General Public License as published by
	the Free Software Foundation, either version 2 of the License, or
	(at your option) any later version.

	This file is distributed in the hope that it will be useful,
	but WITHOUT ANY WARRANTY; without even the implied warranty of
	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
	GNU General Public License for more details.

	You should have received a copy of the GNU General Public License
	along with the this software.  If not, see <http://www.gnu.org/licenses/>.
*/

#include "OGLES2Renderer.h"

#include <stdlib.h>
#include <string.h>
#include <algorithm>

#include "common.h"
#include "debug.h"
#include "gfx3d.h"
#include "NDSSystem.h"
#include "texcache.h"


uint32_t top_screen_fbo, bottom_screen_fbo;
uint32_t top_screen_tex, bottom_screen_tex;

// Lookup Tables
CACHE_ALIGN GLuint dsDepthToD24S8_LUT[32768] = {0};
const GLfloat divide5bitBy31_LUT[32]	= {0.0, 0.03225806451613, 0.06451612903226, 0.09677419354839,
									   0.1290322580645, 0.1612903225806, 0.1935483870968, 0.2258064516129,
									   0.258064516129, 0.2903225806452, 0.3225806451613, 0.3548387096774,
									   0.3870967741935, 0.4193548387097, 0.4516129032258, 0.4838709677419,
									   0.5161290322581, 0.5483870967742, 0.5806451612903, 0.6129032258065,
									   0.6451612903226, 0.6774193548387, 0.7096774193548, 0.741935483871,
									   0.7741935483871, 0.8064516129032, 0.8387096774194, 0.8709677419355,
									   0.9032258064516, 0.9354838709677, 0.9677419354839, 1.0};


#define RGB15TO32_NOALPHA(col) 

//static OGLVersion _OGLDriverVersion = {0, 0};
static OpenGLES2Renderer *_OGLRenderer = NULL;

bool BEGINGL()
{
	return true;
}

void ENDGL()
{

}

//------------------------------------------------------------

// VAO
OGLEXT(PFNGLGENVERTEXARRAYSOESPROC, glGenVertexArraysOES)
OGLEXT(PFNGLDELETEVERTEXARRAYSOESPROC, glDeleteVertexArraysOES)
OGLEXT(PFNGLBINDVERTEXARRAYOESPROC, glBindVertexArrayOES)

// VBO
OGLEXT(PFNGLMAPBUFFEROESPROC, glMapBufferOES)
OGLEXT(PFNGLUNMAPBUFFEROESPROC, glUnmapBufferOES)

void OGLESLoadEntryPoints()
{
	// VAO
	INITOGLEXT(PFNGLGENVERTEXARRAYSOESPROC, glGenVertexArraysOES)
	INITOGLEXT(PFNGLDELETEVERTEXARRAYSOESPROC, glDeleteVertexArraysOES)
	INITOGLEXT(PFNGLBINDVERTEXARRAYOESPROC, glBindVertexArrayOES)

	// VBO
	INITOGLEXT(PFNGLMAPBUFFEROESPROC, glMapBufferOES)
	INITOGLEXT(PFNGLUNMAPBUFFEROESPROC, glUnmapBufferOES)
}

// Vertex shader for geometry, GLSL 1.50
static const char *GeometryVtxShader_150 = {"\
	#version 150 \n\
	\n\
	in vec4 inPosition; \n\
	in vec2 inTexCoord0; \n\
	in vec3 inColor; \n\
	\n\
	uniform usamplerBuffer PolyStates;\n\
	uniform int polyIndex;\n\
	\n\
	out vec4 vtxPosition; \n\
	out vec2 vtxTexCoord; \n\
	out vec4 vtxColor; \n\
	flat out uint polyEnableTexture;\n\
	flat out uint polyEnableFog;\n\
	flat out uint polyEnableDepthWrite;\n\
	flat out uint polySetNewDepthForTranslucent;\n\
	flat out uint polyMode;\n\
	flat out uint polyID;\n\
	flat out uint texSingleBitAlpha;\n\
	\n\
	void main() \n\
	{ \n\
		uvec4 polyStateFlags = texelFetch(PolyStates, (polyIndex*3)+0);\n\
		uvec4 polyStateValues = texelFetch(PolyStates, (polyIndex*3)+1);\n\
		uvec4 polyStateTexParams = texelFetch(PolyStates, (polyIndex*3)+2);\n\
		\n\
		float polyAlpha = float(polyStateValues[0]) / 31.0;\n\
		vec2 polyTexScale = vec2(1.0 / float(8 << polyStateTexParams[0]), 1.0 / float(8 << polyStateTexParams[1]));\n\
		\n\
		polyEnableTexture = polyStateFlags[0];\n\
		polyEnableFog = polyStateFlags[1];\n\
		polyEnableDepthWrite = polyStateFlags[2];\n\
		polySetNewDepthForTranslucent = polyStateFlags[3];\n\
		polyMode = polyStateValues[1];\n\
		polyID = polyStateValues[2];\n\
		texSingleBitAlpha = polyStateTexParams[2];\n\
		\n\
		mat2 texScaleMtx	= mat2(	vec2(polyTexScale.x,            0.0), \n\
									vec2(           0.0, polyTexScale.y)); \n\
		\n\
		vtxPosition = inPosition; \n\
		vtxTexCoord = texScaleMtx * inTexCoord0; \n\
		vtxColor = vec4(inColor * 4.0, polyAlpha); \n\
		\n\
		gl_Position = vtxPosition; \n\
	} \n\
"};

// Fragment shader for geometry, GLSL 1.50
static const char *GeometryFragShader_150 = {"\
	#version 150 \n\
	\n\
	in vec4 vtxPosition; \n\
	in vec2 vtxTexCoord; \n\
	in vec4 vtxColor; \n\
	flat in uint polyEnableTexture;\n\
	flat in uint polyEnableFog;\n\
	flat in uint polyEnableDepthWrite;\n\
	flat in uint polySetNewDepthForTranslucent;\n\
	flat in uint polyMode;\n\
	flat in uint polyID;\n\
	flat in uint texSingleBitAlpha;\n\
	\n\
	layout (std140) uniform RenderStates\n\
	{\n\
		vec2 framebufferSize;\n\
		int toonShadingMode;\n\
		bool enableAlphaTest;\n\
		bool enableAntialiasing;\n\
		bool enableEdgeMarking;\n\
		bool enableFogAlphaOnly;\n\
		bool useWDepth;\n\
		float alphaTestRef;\n\
		float fogOffset;\n\
		float fogStep;\n\
		float pad_0;\n\
		vec4 fogColor;\n\
		float fogDensity[32];\n\
		vec4 edgeColor[8];\n\
		vec4 toonColor[32];\n\
	} state;\n\
	\n\
	uniform sampler2D texRenderObject; \n\
	uniform usamplerBuffer PolyStates;\n\
	uniform int polyIndex;\n\
	\n\
	out vec4 outFragColor;\n\
	out vec4 outFragDepth;\n\
	out vec4 outPolyID;\n\
	out vec4 outFogAttributes;\n\
	\n\
	vec3 packVec3FromFloat(const float value)\n\
	{\n\
		float expandedValue = value * 16777215.0;\n\
		vec3 packedValue = vec3( fract(expandedValue/256.0), fract(((expandedValue/256.0) - fract(expandedValue/256.0)) / 256.0), fract(((expandedValue/65536.0) - fract(expandedValue/65536.0)) / 256.0) );\n\
		return packedValue;\n\
	}\n\
	\n\
	void main() \n\
	{ \n\
		vec4 mainTexColor = bool(polyEnableTexture) ? texture(texRenderObject, vtxTexCoord) : vec4(1.0, 1.0, 1.0, 1.0);\n\
		\n\
		if (bool(texSingleBitAlpha))\n\
		{\n\
			if (mainTexColor.a < 0.500)\n\
			{\n\
				mainTexColor.a = 0.0;\n\
			}\n\
			else\n\
			{\n\
				mainTexColor.rgb = mainTexColor.rgb / mainTexColor.a;\n\
				mainTexColor.a = 1.0;\n\
			}\n\
		}\n\
		\n\
		vec4 newFragColor = mainTexColor * vtxColor; \n\
		\n\
		if (polyMode == 1u) \n\
		{ \n\
			newFragColor.rgb = bool(polyEnableTexture) ? mix(vtxColor.rgb, mainTexColor.rgb, mainTexColor.a) : vtxColor.rgb;\n\
			newFragColor.a = vtxColor.a; \n\
		} \n\
		else if (polyMode == 2u) \n\
		{ \n\
			vec3 newToonColor = state.toonColor[int((vtxColor.r * 31.0) + 0.5)].rgb;\n\
			newFragColor.rgb = (state.toonShadingMode == 0) ? mainTexColor.rgb * newToonColor.rgb : min((mainTexColor.rgb * vtxColor.r) + newToonColor.rgb, 1.0); \n\
		} \n\
		else if (polyMode == 3u) \n\
		{ \n\
			if (polyID != 0u) \n\
			{ \n\
				newFragColor = vtxColor; \n\
			} \n\
		} \n\
		\n\
		if (newFragColor.a < 0.001 || (state.enableAlphaTest && newFragColor.a < state.alphaTestRef))\n\
		{\n\
			discard;\n\
		}\n\
		\n\
		float vertW = (vtxPosition.w == 0.0) ? 0.00000001 : vtxPosition.w; \n\
		// hack: when using z-depth, drop some LSBs so that the overworld map in Dragon Quest IV shows up correctly\n\
		float newFragDepth = (state.useWDepth) ? vtxPosition.w/4096.0 : clamp( (floor((((vtxPosition.z/vertW) * 0.5 + 0.5) * 16777215.0) / 4.0) * 4.0) / 16777215.0, 0.0, 1.0); \n\
		\n\
		outFragColor = newFragColor;\n\
		outFragDepth = vec4( packVec3FromFloat(newFragDepth), float(bool(polyEnableDepthWrite) && (newFragColor.a > 0.999 || bool(polySetNewDepthForTranslucent))));\n\
		outPolyID = vec4(float(polyID)/63.0, 0.0, 0.0, float(newFragColor.a > 0.999));\n\
		outFogAttributes = vec4(float(polyEnableFog), 0.0, 0.0, float((newFragColor.a > 0.999) ? 1.0 : 0.5));\n\
		gl_FragDepth = newFragDepth;\n\
	} \n\
"};

// Vertex shader for applying edge marking, GLSL 1.50
static const char *EdgeMarkVtxShader_150 = {"\
	#version 150\n\
	\n\
	in vec2 inPosition;\n\
	in vec2 inTexCoord0;\n\
	layout (std140) uniform RenderStates\n\
	{\n\
		vec2 framebufferSize;\n\
		int toonShadingMode;\n\
		bool enableAlphaTest;\n\
		bool enableAntialiasing;\n\
		bool enableEdgeMarking;\n\
		bool enableFogAlphaOnly;\n\
		bool useWDepth;\n\
		float alphaTestRef;\n\
		float fogOffset;\n\
		float fogStep;\n\
		float pad_0;\n\
		vec4 fogColor;\n\
		float fogDensity[32];\n\
		vec4 edgeColor[8];\n\
		vec4 toonColor[32];\n\
	} state;\n\
	\n\
	out vec2 texCoord[5];\n\
	\n\
	void main()\n\
	{\n\
		vec2 texInvScale = vec2(1.0/state.framebufferSize.x, 1.0/state.framebufferSize.y);\n\
		\n\
		texCoord[0] = inTexCoord0; // Center\n\
		texCoord[1] = inTexCoord0 + (vec2( 1.0, 0.0) * texInvScale); // Right\n\
		texCoord[2] = inTexCoord0 + (vec2( 0.0, 1.0) * texInvScale); // Down\n\
		texCoord[3] = inTexCoord0 + (vec2(-1.0, 0.0) * texInvScale); // Left\n\
		texCoord[4] = inTexCoord0 + (vec2( 0.0,-1.0) * texInvScale); // Up\n\
		\n\
		gl_Position = vec4(inPosition, 0.0, 1.0);\n\
	}\n\
"};

// Fragment shader for applying edge marking, GLSL 1.50
static const char *EdgeMarkFragShader_150 = {"\
	#version 150\n\
	\n\
	in vec2 texCoord[5];\n\
	\n\
	layout (std140) uniform RenderStates\n\
	{\n\
		vec2 framebufferSize;\n\
		int toonShadingMode;\n\
		bool enableAlphaTest;\n\
		bool enableAntialiasing;\n\
		bool enableEdgeMarking;\n\
		bool enableFogAlphaOnly;\n\
		bool useWDepth;\n\
		float alphaTestRef;\n\
		float fogOffset;\n\
		float fogStep;\n\
		float pad_0;\n\
		vec4 fogColor;\n\
		float fogDensity[32];\n\
		vec4 edgeColor[8];\n\
		vec4 toonColor[32];\n\
	} state;\n\
	\n\
	uniform sampler2D texInFragDepth;\n\
	uniform sampler2D texInPolyID;\n\
	\n\
	out vec4 outFragColor;\n\
	\n\
	float unpackFloatFromVec3(const vec3 value)\n\
	{\n\
		const vec3 unpackRatio = vec3(256.0, 65536.0, 16777216.0);\n\
		return (dot(value, unpackRatio) / 16777215.0);\n\
	}\n\
	\n\
	void main()\n\
	{\n\
		int polyID[5];\n\
		polyID[0] = int((texture(texInPolyID, texCoord[0]).r * 63.0) + 0.5);\n\
		polyID[1] = int((texture(texInPolyID, texCoord[1]).r * 63.0) + 0.5);\n\
		polyID[2] = int((texture(texInPolyID, texCoord[2]).r * 63.0) + 0.5);\n\
		polyID[3] = int((texture(texInPolyID, texCoord[3]).r * 63.0) + 0.5);\n\
		polyID[4] = int((texture(texInPolyID, texCoord[4]).r * 63.0) + 0.5);\n\
		\n\
		float depth[5];\n\
		depth[0] = unpackFloatFromVec3(texture(texInFragDepth, texCoord[0]).rgb);\n\
		depth[1] = unpackFloatFromVec3(texture(texInFragDepth, texCoord[1]).rgb);\n\
		depth[2] = unpackFloatFromVec3(texture(texInFragDepth, texCoord[2]).rgb);\n\
		depth[3] = unpackFloatFromVec3(texture(texInFragDepth, texCoord[3]).rgb);\n\
		depth[4] = unpackFloatFromVec3(texture(texInFragDepth, texCoord[4]).rgb);\n\
		\n\
		vec4 newEdgeColor = vec4(0.0, 0.0, 0.0, 0.0);\n\
		\n\
		for (int i = 1; i < 5; i++)\n\
		{\n\
			if (polyID[0] != polyID[i] && depth[0] >= depth[i])\n\
			{\n\
				newEdgeColor = state.edgeColor[polyID[i]/8];\n\
				break;\n\
			}\n\
		}\n\
		\n\
		outFragColor = newEdgeColor;\n\
	}\n\
"};

// Vertex shader for applying fog, GLSL 1.50
static const char *FogVtxShader_150 = {"\
	#version 150\n\
	\n\
	in vec2 inPosition;\n\
	in vec2 inTexCoord0;\n\
	out vec2 texCoord;\n\
	\n\
	void main()\n\
	{\n\
		texCoord = inTexCoord0;\n\
		gl_Position = vec4(inPosition, 0.0, 1.0);\n\
	}\n\
"};

// Fragment shader for applying fog, GLSL 1.50
static const char *FogFragShader_150 = {"\
	#version 150\n\
	\n\
	in vec2 texCoord;\n\
	\n\
	layout (std140) uniform RenderStates\n\
	{\n\
		vec2 framebufferSize;\n\
		int toonShadingMode;\n\
		bool enableAlphaTest;\n\
		bool enableAntialiasing;\n\
		bool enableEdgeMarking;\n\
		bool enableFogAlphaOnly;\n\
		bool useWDepth;\n\
		float alphaTestRef;\n\
		float fogOffset;\n\
		float fogStep;\n\
		float pad_0;\n\
		vec4 fogColor;\n\
		float fogDensity[32];\n\
		vec4 edgeColor[8];\n\
		vec4 toonColor[32];\n\
	} state;\n\
	\n\
	uniform sampler2D texInFragColor;\n\
	uniform sampler2D texInFragDepth;\n\
	uniform sampler2D texInFogAttributes;\n\
	\n\
	out vec4 outFragColor;\n\
	\n\
	float unpackFloatFromVec3(const vec3 value)\n\
	{\n\
		const vec3 unpackRatio = vec3(256.0, 65536.0, 16777216.0);\n\
		return (dot(value, unpackRatio) / 16777215.0);\n\
	}\n\
	\n\
	void main()\n\
	{\n\
		vec4 inFragColor = texture(texInFragColor, texCoord);\n\
		vec4 inFogAttributes = texture(texInFogAttributes, texCoord);\n\
		bool polyEnableFog = (inFogAttributes.r > 0.999);\n\
		vec4 newFoggedColor = inFragColor;\n\
		\n\
		if (polyEnableFog)\n\
		{\n\
			float inFragDepth = unpackFloatFromVec3(texture(texInFragDepth, texCoord).rgb);\n\
			float fogMixWeight = 0.0;\n\
			\n\
			if (inFragDepth <= min(state.fogOffset + state.fogStep, 1.0))\n\
			{\n\
				fogMixWeight = state.fogDensity[0];\n\
			}\n\
			else if (inFragDepth >= min(state.fogOffset + (state.fogStep*32.0), 1.0))\n\
			{\n\
				fogMixWeight = state.fogDensity[31];\n\
			}\n\
			else\n\
			{\n\
				for (int i = 1; i < 32; i++)\n\
				{\n\
					float currentFogStep = min(state.fogOffset + (state.fogStep * float(i+1)), 1.0);\n\
					if (inFragDepth <= currentFogStep)\n\
					{\n\
						float previousFogStep = min(state.fogOffset + (state.fogStep * float(i)), 1.0);\n\
						fogMixWeight = mix(state.fogDensity[i-1], state.fogDensity[i], (inFragDepth - previousFogStep) / (currentFogStep - previousFogStep));\n\
						break;\n\
					}\n\
				}\n\
			}\n\
			\n\
			newFoggedColor = mix(inFragColor, (state.enableFogAlphaOnly) ? vec4(inFragColor.rgb, state.fogColor.a) : state.fogColor, fogMixWeight);\n\
		}\n\
		\n\
		outFragColor = newFoggedColor;\n\
	}\n\
"};

// Vertex shader for the final framebuffer, GLSL 1.50
static const char *FramebufferOutputVtxShader_150 = {"\
	#version 150\n\
	\n\
	in vec2 inPosition;\n\
	in vec2 inTexCoord0;\n\
	out vec2 texCoord;\n\
	\n\
	void main()\n\
	{\n\
		texCoord = inTexCoord0;\n\
		gl_Position = vec4(inPosition, 0.0, 1.0);\n\
	}\n\
"};

// Fragment shader for the final framebuffer, GLSL 1.50
static const char *FramebufferOutputFragShader_150 = {"\
	#version 150\n\
	\n\
	in vec2 texCoord;\n\
	\n\
	uniform sampler2D texInFragColor;\n\
	\n\
	out vec4 outFragColor;\n\
	\n\
	void main()\n\
	{\n\
		vec4 colorRGBA6665 = texture(texInFragColor, texCoord);\n\
		colorRGBA6665     = floor((colorRGBA6665 * 255.0) + 0.5);\n\
		colorRGBA6665.rgb = floor(colorRGBA6665.rgb / 4.0);\n\
		colorRGBA6665.a   = floor(colorRGBA6665.a   / 8.0);\n\
		\n\
		outFragColor = (colorRGBA6665 / 255.0);\n\
	}\n\
"};

// Vertex Shader GLSL 1.00
static const char *vertexShader_100 = {"\
	attribute vec4 inPosition; \n\
	attribute vec2 inTexCoord0; \n\
	attribute vec3 inColor; \n\
	\n\
	uniform float polyAlpha; \n\
	uniform vec2 texScale; \n\
	\n\
	varying vec4 vtxPosition; \n\
	varying vec2 vtxTexCoord; \n\
	varying vec4 vtxColor; \n\
	\n\
	void main() \n\
	{ \n\
		mat2 texScaleMtx	= mat2(	vec2(texScale.x,        0.0), \n\
									vec2(       0.0, texScale.y)); \n\
		\n\
		vtxPosition = inPosition; \n\
		vtxTexCoord = texScaleMtx * inTexCoord0; \n\
		vtxColor = vec4(inColor * 4.0, polyAlpha); \n\
		\n\
		gl_Position = vtxPosition; \n\
	} \n\
"};

// Fragment Shader GLSL 1.00
static const char *fragmentShader_100 = {"\
	precision mediump float; \n\
	varying vec4 vtxPosition; \n\
	varying vec2 vtxTexCoord; \n\
	varying vec4 vtxColor; \n\
	\n\
	uniform sampler2D texMainRender; \n\
	uniform sampler2D texToonTable; \n\
	uniform int polyID; \n\
	uniform bool hasTexture; \n\
	uniform int polygonMode; \n\
	uniform int toonShadingMode; \n\
	uniform int oglWBuffer; \n\
	uniform bool enableAlphaTest; \n\
	uniform float alphaTestRef; \n\
	\n\
	void main() \n\
	{ \n\
		vec4 texColor = vec4(1.0, 1.0, 1.0, 1.0); \n\
		vec4 fragColor; \n\
		\n\
		if(hasTexture) \n\
		{ \n\
			texColor = texture2D(texMainRender, vtxTexCoord); \n\
		} \n\
		\n\
		fragColor = texColor; \n\
		\n\
		if(polygonMode == 0) \n\
		{ \n\
			fragColor = vtxColor * texColor; \n\
		} \n\
		else if(polygonMode == 1) \n\
		{ \n\
			if (texColor.a == 0.0 || !hasTexture) \n\
			{ \n\
				fragColor.rgb = vtxColor.rgb; \n\
			} \n\
			else if (texColor.a == 1.0) \n\
			{ \n\
				fragColor.rgb = texColor.rgb; \n\
			} \n\
			else \n\
			{ \n\
				fragColor.rgb = texColor.rgb * (1.0-texColor.a) + vtxColor.rgb * texColor.a;\n\
			} \n\
			\n\
			fragColor.a = vtxColor.a; \n\
		} \n\
		else if(polygonMode == 2) \n\
		{ \n\
			if (toonShadingMode == 0) \n\
			{ \n\
				vec3 toonColor = vec3(texture2D(texToonTable, vec2(vtxColor.r,0)).rgb); \n\
				fragColor.rgb = texColor.rgb * toonColor.rgb;\n\
				fragColor.a = texColor.a * vtxColor.a;\n\
			} \n\
			else \n\
			{ \n\
				vec3 toonColor = vec3(texture2D(texToonTable, vec2(vtxColor.r,0)).rgb); \n\
				fragColor.rgb = texColor.rgb * vtxColor.rgb + toonColor.rgb; \n\
				fragColor.a = texColor.a * vtxColor.a; \n\
			} \n\
		} \n\
		else if(polygonMode == 3) \n\
		{ \n\
			if (polyID != 0) \n\
			{ \n\
				fragColor = vtxColor; \n\
			} \n\
		} \n\
		\n\
		if (fragColor.a == 0.0 || (enableAlphaTest && fragColor.a < alphaTestRef)) \n\
		{ \n\
			discard; \n\
		} \n\
		\n\
		gl_FragColor = fragColor; \n\
	} \n\
"};

static void texDeleteCallback(TexCacheItem *item, void *arg0, void *arg1)
{
	_OGLRenderer->DeleteTexture(item);
}

FORCEINLINE u32 RGBA8888_32_To_RGBA6665_32Rev(const u32 srcPix)
{
	const u32 dstPix = (srcPix >> 2) & 0x3F3F3F3F;
	
	return	 (dstPix & 0xFF000000) >> 24 |		// R
			 (dstPix & 0x00FF0000) >> 16 |		// G
			 (dstPix & 0x0000FF00) >> 8 |		// B
			((dstPix >> 1) & 0x000000FF) << 24;	// A
}

FORCEINLINE u32 RGBA8888_32Rev_To_RGBA6665_32Rev(const u32 srcPix)
{
	const u32 dstPix = (srcPix >> 2) & 0x3F3F3F3F;
	
	//return	 (dstPix & 0x00FF0000) |		// R
	//		 (dstPix & 0x0000FF00) |		// G
	//		 (dstPix & 0x000000FF) |		// B
	//		((dstPix >> 1) & 0xFF000000);	// A
	return ((dstPix >> 1) & 0xFF000000) | (dstPix & 0x00FFFFFF);
}

void OGLCreateRendererES(OpenGLES2Renderer **rendererPtr)
{

}

bool OpenGLES2Renderer::IsExtensionPresent(const std::set<std::string> *oglExtensionSet, const std::string extensionName) const
{
	if (oglExtensionSet == NULL || oglExtensionSet->size() == 0)
	{
		return false;
	}
	
	return (oglExtensionSet->find(extensionName) != oglExtensionSet->end());
}

bool OpenGLES2Renderer::ValidateShaderCompile(GLuint theShader) const
{
	bool isCompileValid = false;
	GLint status = GL_FALSE;
	
	glGetShaderiv(theShader, GL_COMPILE_STATUS, &status);
	if(status == GL_TRUE)
	{
		isCompileValid = true;
	}
	else
	{
		GLint logSize;
		GLchar *log = NULL;
		
		glGetShaderiv(theShader, GL_INFO_LOG_LENGTH, &logSize);
		log = new GLchar[logSize];
		glGetShaderInfoLog(theShader, logSize, &logSize, log);
		
		INFO("OpenGLES2: SEVERE - FAILED TO COMPILE SHADER : %s\n", log);
		delete[] log;
	}
	
	return isCompileValid;
}

bool OpenGLES2Renderer::ValidateShaderProgramLink(GLuint theProgram) const
{
	bool isLinkValid = false;
	GLint status = GL_FALSE;
	
	glGetProgramiv(theProgram, GL_LINK_STATUS, &status);
	if(status == GL_TRUE)
	{
		isLinkValid = true;
	}
	else
	{
		GLint logSize;
		GLchar *log = NULL;
		
		glGetProgramiv(theProgram, GL_INFO_LOG_LENGTH, &logSize);
		log = new GLchar[logSize];
		glGetProgramInfoLog(theProgram, logSize, &logSize, log);
		
		INFO("OpenGLES2: SEVERE - FAILED TO LINK SHADER PROGRAM : %s\n", log);
		delete[] log;
	}
	
	return isLinkValid;
}

void OpenGLES2Renderer::GetVersion(unsigned int *major, unsigned int *minor) const
{
}

void OpenGLES2Renderer::SetVersion(unsigned int major, unsigned int minor)
{
	
}

void OpenGLES2Renderer::ConvertFramebuffer(const u32 *__restrict srcBuffer, u32 *dstBuffer)
{
	if (srcBuffer == NULL || dstBuffer == NULL)
	{
		return;
	}
	
	// Convert from 32-bit BGRA8888/RGBA8888 format to 32-bit RGBA6665 reversed format. OpenGLES2
	// stores pixels using a flipped Y-coordinate, so this needs to be flipped back
	// to the DS Y-coordinate.
	for(int i = 0, y = 191; y >= 0; y--)
	{
		u32 *__restrict dst = dstBuffer + (y << 8); // Same as dstBuffer + (y * 256)
		
		for(unsigned int x = 0; x < 256; x++, i++)
		{
			// Use the correct endian format since OpenGLES2 uses the native endian of
			// the architecture it is running on.
#ifdef WORDS_BIGENDIAN
			//*dst++ = BGRA8888_32_To_RGBA6665_32(srcBuffer[i]);
			*dst++ = RGBA8888_32_To_RGBA6665_32Rev(srcBuffer[i]);
#else
			//*dst++ = BGRA8888_32Rev_To_RGBA6665_32Rev(srcBuffer[i]);
			*dst++ = RGBA8888_32Rev_To_RGBA6665_32Rev(srcBuffer[i]);
#endif
		}
	}
}

OpenGLES2Renderer::OpenGLES2Renderer()
{
	isFBOSupported = false;
	isVAOSupported = false;
	
	// Init OpenGLES2 rendering states
	ref = new OGLESRenderRef;
}


Render3DError OpenGLES2Renderer::InitExtensions()
{
	printf("InitExntesions\n");
	Render3DError error = OGLERROR_NOERR;
	OGLESRenderRef &OGLRef = *this->ref;
	
	// Get OpenGLES2 extensions
	std::set<std::string> oglExtensionSet;
	this->GetExtensionSet(&oglExtensionSet);
	
	// Initialize OpenGLES2
	this->InitTables();
	
	std::string vertexShaderProgram;
	std::string fragmentShaderProgram;
	error = this->LoadShaderPrograms(&vertexShaderProgram, &fragmentShaderProgram);
	if (error != OGLERROR_NOERR)
	{
		return error;
	}
	
	error = this->CreateShaders(&vertexShaderProgram, &fragmentShaderProgram);
	if (error != OGLERROR_NOERR)
	{
		return error;
	}
	
	this->CreateToonTable();
	
	this->CreateVBOs();
#ifdef __vita__
	this->isVAOSupported = false;
#else
	this->isVAOSupported = this->IsExtensionPresent(&oglExtensionSet, "GL_OES_vertex_array_object");
#endif
	if (this->isVAOSupported)
	{
		this->CreateVAOs();
	}
	
	//this->isFBOSupported	= this->IsExtensionPresent(&oglExtensionSet, "GL_OES_depth_texture") &&
	//						  this->IsExtensionPresent(&oglExtensionSet, "GL_OES_packed_depth_stencil");
	
#ifdef __vita__
	glGenTextures(1, &top_screen_tex);
	glGenTextures(1, &bottom_screen_tex);
	glBindTexture(GL_TEXTURE_2D, top_screen_tex);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 256, 192, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
	glBindTexture(GL_TEXTURE_2D, bottom_screen_tex);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 256, 192, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
	glGenFramebuffers(1, &top_screen_fbo);
	glGenFramebuffers(1, &bottom_screen_fbo);
	glBindFramebuffer(GL_FRAMEBUFFER, top_screen_fbo);
	glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, top_screen_tex, 0);
	glBindFramebuffer(GL_FRAMEBUFFER, bottom_screen_fbo);
	glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, bottom_screen_tex, 0);
	glBindFramebuffer(GL_FRAMEBUFFER, 0);
#endif
	this->isFBOSupported = false;
	if (this->isFBOSupported)
	{
		error = this->CreateFBOs();
		if (error != OGLERROR_NOERR)
		{
			OGLRef.fboFinalOutputID = 0;
			this->isFBOSupported = false;
		}
	}
	else
	{
		OGLRef.fboFinalOutputID = 0;
		INFO("OpenGLES2: FBOs are unsupported. Some emulation features will be disabled.\n");
	}
	
	this->InitTextures();
	this->InitFinalRenderStates(&oglExtensionSet); // This must be done last
	
	return OGLERROR_NOERR;
}

Render3DError OpenGLES2Renderer::CreateVBOs()
{
	OGLESRenderRef &OGLRef = *this->ref;
	
	glGenBuffers(1, &OGLRef.vboVertexID);
	glBindBuffer(GL_ARRAY_BUFFER, OGLRef.vboVertexID);
	glBufferData(GL_ARRAY_BUFFER, VERTLIST_SIZE * sizeof(VERT), NULL, GL_STREAM_DRAW);
	glBindBuffer(GL_ARRAY_BUFFER, 0);
	
	glGenBuffers(1, &OGLRef.iboIndexID);
	glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, OGLRef.iboIndexID);
	glBufferData(GL_ELEMENT_ARRAY_BUFFER, OGLRENDER_VERT_INDEX_BUFFER_COUNT * sizeof(GLushort), NULL, GL_STREAM_DRAW);
	glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
	
	return OGLERROR_NOERR;
}

void OpenGLES2Renderer::DestroyVBOs()
{
	OGLESRenderRef &OGLRef = *this->ref;
	
	glBindBuffer(GL_ARRAY_BUFFER, 0);
	glDeleteBuffers(1, &OGLRef.vboVertexID);
	
	glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
	glDeleteBuffers(1, &OGLRef.iboIndexID);
}

Render3DError OpenGLES2Renderer::LoadShaderPrograms(std::string *outVertexShaderProgram, std::string *outFragmentShaderProgram)
{
	outVertexShaderProgram->clear();
	outFragmentShaderProgram->clear();
	
	*outVertexShaderProgram += std::string(vertexShader_100);
	*outFragmentShaderProgram += std::string(fragmentShader_100);
	
	return OGLERROR_NOERR;
}

Render3DError OpenGLES2Renderer::SetupShaderIO()
{
	OGLESRenderRef &OGLRef = *this->ref;
	
	glBindAttribLocation(OGLRef.shaderProgram, OGLVertexAttributeID_Position, "inPosition");
	glBindAttribLocation(OGLRef.shaderProgram, OGLVertexAttributeID_TexCoord0, "inTexCoord0");
	glBindAttribLocation(OGLRef.shaderProgram, OGLVertexAttributeID_Color, "inColor");
	
	return OGLERROR_NOERR;
}

Render3DError OpenGLES2Renderer::CreateShaders(const std::string *vertexShaderProgram, const std::string *fragmentShaderProgram)
{
	OGLESRenderRef &OGLRef = *this->ref;
	
	OGLRef.vertexShaderID = glCreateShader(GL_VERTEX_SHADER);
	if(!OGLRef.vertexShaderID)
	{
		INFO("OpenGLES2: Failed to create the vertex shader.\n");		
		return OGLERROR_SHADER_CREATE_ERROR;
	}
	
	const char *vertexShaderProgramChar = vertexShaderProgram->c_str();
	glShaderSource(OGLRef.vertexShaderID, 1, (const GLchar **)&vertexShaderProgramChar, NULL);
	glCompileShader(OGLRef.vertexShaderID);
	if (!this->ValidateShaderCompile(OGLRef.vertexShaderID))
	{
		glDeleteShader(OGLRef.vertexShaderID);
		INFO("OpenGLES2: Failed to compile the vertex shader.\n");
		return OGLERROR_SHADER_CREATE_ERROR;
	}
	
	OGLRef.fragmentShaderID = glCreateShader(GL_FRAGMENT_SHADER);
	if(!OGLRef.fragmentShaderID)
	{
		glDeleteShader(OGLRef.vertexShaderID);
		INFO("OpenGLES2: Failed to create the fragment shader.\n");
		return OGLERROR_SHADER_CREATE_ERROR;
	}
	
	const char *fragmentShaderProgramChar = fragmentShaderProgram->c_str();
	glShaderSource(OGLRef.fragmentShaderID, 1, (const GLchar **)&fragmentShaderProgramChar, NULL);
	glCompileShader(OGLRef.fragmentShaderID);
	if (!this->ValidateShaderCompile(OGLRef.fragmentShaderID))
	{
		glDeleteShader(OGLRef.vertexShaderID);
		glDeleteShader(OGLRef.fragmentShaderID);
		INFO("OpenGLES2: Failed to compile the fragment shader.\n");
		return OGLERROR_SHADER_CREATE_ERROR;
	}
	
	OGLRef.shaderProgram = glCreateProgram();
	if(!OGLRef.shaderProgram)
	{
		glDeleteShader(OGLRef.vertexShaderID);
		glDeleteShader(OGLRef.fragmentShaderID);
		INFO("OpenGLES2: Failed to create the shader program.\n");
		return OGLERROR_SHADER_CREATE_ERROR;
	}
	
	glAttachShader(OGLRef.shaderProgram, OGLRef.vertexShaderID);
	glAttachShader(OGLRef.shaderProgram, OGLRef.fragmentShaderID);
	
	this->SetupShaderIO();
	
	glLinkProgram(OGLRef.shaderProgram);
	if (!this->ValidateShaderProgramLink(OGLRef.shaderProgram))
	{
#ifndef __vita__
		glDetachShader(OGLRef.shaderProgram, OGLRef.vertexShaderID);
		glDetachShader(OGLRef.shaderProgram, OGLRef.fragmentShaderID);
#endif
		glDeleteProgram(OGLRef.shaderProgram);
		glDeleteShader(OGLRef.vertexShaderID);
		glDeleteShader(OGLRef.fragmentShaderID);
		INFO("OpenGLES2: Failed to link the shader program.\n");
		return OGLERROR_SHADER_CREATE_ERROR;
	}
#ifndef __vita__
	glValidateProgram(OGLRef.shaderProgram);
#endif
	glUseProgram(OGLRef.shaderProgram);
	
	// Set up shader uniforms
	GLint uniformTexSampler = glGetUniformLocation(OGLRef.shaderProgram, "texMainRender");
	glUniform1i(uniformTexSampler, 0);
	
	uniformTexSampler = glGetUniformLocation(OGLRef.shaderProgram, "texToonTable");
	glUniform1i(uniformTexSampler, OGLTextureUnitID_ToonTable);
	
	OGLRef.uniformPolyAlpha			= glGetUniformLocation(OGLRef.shaderProgram, "polyAlpha");
	OGLRef.uniformTexScale			= glGetUniformLocation(OGLRef.shaderProgram, "texScale");
	OGLRef.uniformPolyID			= glGetUniformLocation(OGLRef.shaderProgram, "polyID");
	OGLRef.uniformHasTexture		= glGetUniformLocation(OGLRef.shaderProgram, "hasTexture");
	OGLRef.uniformPolygonMode		= glGetUniformLocation(OGLRef.shaderProgram, "polygonMode");
	OGLRef.uniformToonShadingMode	= glGetUniformLocation(OGLRef.shaderProgram, "toonShadingMode");
	OGLRef.uniformWBuffer			= glGetUniformLocation(OGLRef.shaderProgram, "oglWBuffer");
	OGLRef.uniformEnableAlphaTest	= glGetUniformLocation(OGLRef.shaderProgram, "enableAlphaTest");
	OGLRef.uniformAlphaTestRef		= glGetUniformLocation(OGLRef.shaderProgram, "alphaTestRef");
	
	INFO("OpenGLES2: Successfully created shaders.\n");
	
	return OGLERROR_NOERR;
}

void OpenGLES2Renderer::DestroyShaders()
{
	OGLESRenderRef &OGLRef = *this->ref;
	
	glUseProgram(0);
#ifndef __vita__
	glDetachShader(OGLRef.shaderProgram, OGLRef.vertexShaderID);
	glDetachShader(OGLRef.shaderProgram, OGLRef.fragmentShaderID);
#endif
	glDeleteProgram(OGLRef.shaderProgram);
	glDeleteShader(OGLRef.vertexShaderID);
	glDeleteShader(OGLRef.fragmentShaderID);
	
	this->DestroyToonTable();
}

Render3DError OpenGLES2Renderer::CreateVAOs()
{
	OGLESRenderRef &OGLRef = *this->ref;
	
	glGenVertexArraysOES(1, &OGLRef.vaoMainStatesID);
	glBindVertexArrayOES(OGLRef.vaoMainStatesID);
	
	glBindBuffer(GL_ARRAY_BUFFER, OGLRef.vboVertexID);
	glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, OGLRef.iboIndexID);
	
	glEnableVertexAttribArray(OGLVertexAttributeID_Position);
	glEnableVertexAttribArray(OGLVertexAttributeID_TexCoord0);
	glEnableVertexAttribArray(OGLVertexAttributeID_Color);
	
	glVertexAttribPointer(OGLVertexAttributeID_Position, 4, GL_FLOAT, GL_FALSE, sizeof(VERT), (const GLvoid *)offsetof(VERT, coord));
	glVertexAttribPointer(OGLVertexAttributeID_TexCoord0, 2, GL_FLOAT, GL_FALSE, sizeof(VERT), (const GLvoid *)offsetof(VERT, texcoord));
	glVertexAttribPointer(OGLVertexAttributeID_Color, 3, GL_UNSIGNED_BYTE, GL_TRUE, sizeof(VERT), (const GLvoid *)offsetof(VERT, color));
	
	glBindVertexArrayOES(0);
	
	return OGLERROR_NOERR;
}

void OpenGLES2Renderer::DestroyVAOs()
{
	if (!this->isVAOSupported)
	{
		return;
	}
	
	glBindVertexArrayOES(0);
	glDeleteVertexArraysOES(1, &this->ref->vaoMainStatesID);
	
	this->isVAOSupported = false;
}

Render3DError OpenGLES2Renderer::CreateFBOs()
{
	OGLESRenderRef &OGLRef = *this->ref;
	
	// Set up FBO render targets
	this->CreateClearImage();
	
	// Set up FBOs
	glGenFramebuffers(1, &OGLRef.fboClearImageID);
	
	glBindFramebuffer(GL_FRAMEBUFFER, OGLRef.fboClearImageID);
	glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, OGLRef.texClearImageColorID, 0);
#ifndef __vita__
	glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, OGLRef.texClearImageDepthStencilID, 0);
	glFramebufferTexture2D(GL_FRAMEBUFFER, GL_STENCIL_ATTACHMENT, GL_TEXTURE_2D, OGLRef.texClearImageDepthStencilID, 0);
#endif
	if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
	{
		INFO("OpenGLES2: Failed to created FBOs. Some emulation features will be disabled.\n");
		
		glBindFramebuffer(GL_FRAMEBUFFER, 0);
		glDeleteFramebuffers(1, &OGLRef.fboClearImageID);
		this->DestroyClearImage();
		
		this->isFBOSupported = false;
		return OGLERROR_FBO_CREATE_ERROR;
	}
	
	// Set up final output FBO
	OGLRef.fboFinalOutputID = 0;
	glBindFramebuffer(GL_FRAMEBUFFER, OGLRef.fboFinalOutputID);
	
	INFO("OpenGLES2: Successfully created FBOs.\n");
	
	return OGLERROR_NOERR;
}

void OpenGLES2Renderer::DestroyFBOs()
{
	if (!this->isFBOSupported)
	{
		return;
	}
	
	OGLESRenderRef &OGLRef = *this->ref;
	
	glBindFramebuffer(GL_FRAMEBUFFER, 0);
	glDeleteFramebuffers(1, &OGLRef.fboClearImageID);
	this->DestroyClearImage();
	this->isFBOSupported = false;
}

Render3DError OpenGLES2Renderer::InitFinalRenderStates(const std::set<std::string> *oglExtensionSet)
{
	OGLESRenderRef &OGLRef = *this->ref;
	
	bool isBlendEquationSeparateSupported = this->IsExtensionPresent(oglExtensionSet, "GL_EXT_blend_minmax");
	
	// Blending Support
	if (isBlendEquationSeparateSupported)
	{
		// we want to use alpha destination blending so we can track the last-rendered alpha value
		// test: new super mario brothers renders the stormclouds at the beginning
		glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_SRC_ALPHA, GL_DST_ALPHA);
		glBlendEquationSeparate(GL_FUNC_ADD, GL_MAX_EXT);
	}
	else
	{
		glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ONE, GL_DST_ALPHA);
	}
	
	// Mirrored Repeat Mode Support
	OGLRef.stateTexMirroredRepeat = GL_MIRRORED_REPEAT;
	
	// Always enable depth test, and control using glDepthMask().
	glEnable(GL_DEPTH_TEST);
	
	return OGLERROR_NOERR;
}

Render3DError OpenGLES2Renderer::InitTextures()
{
	this->ExpandFreeTextures();
	
	return OGLERROR_NOERR;
}

Render3DError OpenGLES2Renderer::InitTables()
{
	static bool needTableInit = true;
	
	if (needTableInit)
	{
		for (unsigned int i = 0; i < 32768; i++)
			dsDepthToD24S8_LUT[i] = (GLuint)DS_DEPTH15TO24(i) << 8;
		
		needTableInit = false;
	}
	
	return OGLERROR_NOERR;
}

Render3DError OpenGLES2Renderer::CreateToonTable()
{
	OGLESRenderRef &OGLRef = *this->ref;
	
	// The toon table is a special 1D texture where each pixel corresponds
	// to a specific color in the toon table.
	glGenTextures(1, &OGLRef.texToonTableID);
	glActiveTexture(GL_TEXTURE0 + OGLTextureUnitID_ToonTable);
	
	glBindTexture(GL_TEXTURE_2D, OGLRef.texToonTableID);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	glBindTexture(GL_TEXTURE_2D, 0);
	
	glActiveTexture(GL_TEXTURE0);
	
	return OGLERROR_NOERR;
}

Render3DError OpenGLES2Renderer::DestroyToonTable()
{
	glActiveTexture(GL_TEXTURE0 + OGLTextureUnitID_ToonTable);
	glBindTexture(GL_TEXTURE_2D, 0);
	glActiveTexture(GL_TEXTURE0);
	glDeleteTextures(1, &this->ref->texToonTableID);
	
	return OGLERROR_NOERR;
}

Render3DError OpenGLES2Renderer::UploadToonTable(const GLuint *toonTableBuffer)
{
	glActiveTexture(GL_TEXTURE0 + OGLTextureUnitID_ToonTable);
	glBindTexture(GL_TEXTURE_2D, this->ref->texToonTableID);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 32, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, toonTableBuffer);
	glActiveTexture(GL_TEXTURE0);
	
	return OGLERROR_NOERR;
}

Render3DError OpenGLES2Renderer::CreateClearImage()
{
	OGLESRenderRef &OGLRef = *this->ref;
	
	glGenTextures(1, &OGLRef.texClearImageColorID);
#ifndef __vita__
	glGenTextures(1, &OGLRef.texClearImageDepthStencilID);
#endif
	glActiveTexture(GL_TEXTURE0 + OGLTextureUnitID_ClearImage);
	
	glBindTexture(GL_TEXTURE_2D, OGLRef.texClearImageColorID);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 256, 192, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
#ifndef __vita__
	glBindTexture(GL_TEXTURE_2D, OGLRef.texClearImageDepthStencilID);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
	//glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_COMPARE_MODE, GL_NONE);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH24_STENCIL8_OES, 256, 192, 0, GL_DEPTH_STENCIL_OES, GL_UNSIGNED_INT_24_8_OES, NULL);
#endif
	glActiveTexture(GL_TEXTURE0);
	
	return OGLERROR_NOERR;
}

Render3DError OpenGLES2Renderer::DestroyClearImage()
{
	OGLESRenderRef &OGLRef = *this->ref;
	
	glActiveTexture(GL_TEXTURE0 + OGLTextureUnitID_ClearImage);
	glBindTexture(GL_TEXTURE_2D, 0);
	glActiveTexture(GL_TEXTURE0);
	glDeleteTextures(1, &OGLRef.texClearImageColorID);
	glDeleteTextures(1, &OGLRef.texClearImageDepthStencilID);
	
	return OGLERROR_NOERR;
}

Render3DError OpenGLES2Renderer::UploadClearImage(const GLushort *clearImageColorBuffer, const GLint *clearImageDepthBuffer)
{
	OGLESRenderRef &OGLRef = *this->ref;
	
	glActiveTexture(GL_TEXTURE0 + OGLTextureUnitID_ClearImage);
	
	glBindTexture(GL_TEXTURE_2D, OGLRef.texClearImageColorID);
	glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, 256, 192, GL_RGBA, GL_UNSIGNED_BYTE, clearImageColorBuffer);
	glBindTexture(GL_TEXTURE_2D, OGLRef.texClearImageDepthStencilID);
	glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, 256, 192, GL_DEPTH_STENCIL_OES, GL_UNSIGNED_INT_24_8_OES, clearImageDepthBuffer);
	glBindTexture(GL_TEXTURE_2D, 0);
	
	glActiveTexture(GL_TEXTURE0);
	
	return OGLERROR_NOERR;
}

void OpenGLES2Renderer::GetExtensionSet(std::set<std::string> *oglExtensionSet)
{
	std::string oglExtensionString = std::string((const char *)glGetString(GL_EXTENSIONS));
	
	size_t extStringStartLoc = 0;
	size_t delimiterLoc = oglExtensionString.find_first_of(' ', extStringStartLoc);
	while (delimiterLoc != std::string::npos)
	{
		std::string extensionName = oglExtensionString.substr(extStringStartLoc, delimiterLoc - extStringStartLoc);
		oglExtensionSet->insert(extensionName);
		
		extStringStartLoc = delimiterLoc + 1;
		delimiterLoc = oglExtensionString.find_first_of(' ', extStringStartLoc);
	}
	
	if (extStringStartLoc - oglExtensionString.length() > 0)
	{
		std::string extensionName = oglExtensionString.substr(extStringStartLoc, oglExtensionString.length() - extStringStartLoc);
		oglExtensionSet->insert(extensionName);
	}

	INFO("{ ExtensionSet : %s }\n", oglExtensionString.c_str());
}

Render3DError OpenGLES2Renderer::ExpandFreeTextures()
{
	static const int kInitTextures = 128;
	GLuint oglTempTextureID[kInitTextures];
	glGenTextures(kInitTextures, oglTempTextureID);
	
	for(int i=0;i<kInitTextures;i++)
	{
		this->ref->freeTextureIDs.push(oglTempTextureID[i]);
	}
	
	return OGLERROR_NOERR;
}

Render3DError OpenGLES2Renderer::SetupVertices(const VERTLIST *vertList, const POLYLIST *polyList, const INDEXLIST *indexList, GLushort *outIndexBuffer, unsigned int *outIndexCount)
{
	OGLESRenderRef &OGLRef = *this->ref;
	const unsigned int polyCount = polyList->count;
	unsigned int vertIndexCount = 0;
	
	for(unsigned int i = 0; i < polyCount; i++)
	{
		const POLY *poly = &polyList->list[indexList->list[i]];
		const unsigned int polyType = poly->type;
		
		for(unsigned int j = 0; j < polyType; j++)
		{
			const GLushort vertIndex = poly->vertIndexes[j];
				
			// While we're looping through our vertices, add each vertex index to
			// a buffer. For GFX3D_QUADS and GFX3D_QUAD_STRIP, we also add additional
			// vertices here to convert them to GL_TRIANGLES, which are much easier
			// to work with and won't be deprecated in future OpenGLES2 versions.
			outIndexBuffer[vertIndexCount++] = vertIndex;
			if (poly->vtxFormat == GFX3D_QUADS || poly->vtxFormat == GFX3D_QUAD_STRIP)
			{
				if (j == 2)
				{
					outIndexBuffer[vertIndexCount++] = vertIndex;
				}
				else if (j == 3)
				{
					outIndexBuffer[vertIndexCount++] = poly->vertIndexes[0];
				}
			}
		}
	}
	
	*outIndexCount = vertIndexCount;
	
	return OGLERROR_NOERR;
}

Render3DError OpenGLES2Renderer::EnableVertexAttributes(const VERTLIST *vertList, const GLushort *indexBuffer, const unsigned int vertIndexCount)
{
	OGLESRenderRef &OGLRef = *this->ref;
	
	if (this->isVAOSupported)
	{
		glBindVertexArrayOES(OGLRef.vaoMainStatesID);
		glBufferSubData(GL_ARRAY_BUFFER, 0, sizeof(VERT) * vertList->count, vertList);
		glBufferSubData(GL_ELEMENT_ARRAY_BUFFER, 0, vertIndexCount * sizeof(GLushort), indexBuffer);
	}
	else
	{
		glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, OGLRef.iboIndexID);
		glBufferSubData(GL_ELEMENT_ARRAY_BUFFER, 0, vertIndexCount * sizeof(GLushort), indexBuffer);
		
		glBindBuffer(GL_ARRAY_BUFFER, OGLRef.vboVertexID);
		glBufferSubData(GL_ARRAY_BUFFER, 0, sizeof(VERT) * vertList->count, vertList);
		
		glEnableVertexAttribArray(OGLVertexAttributeID_Position);
		glEnableVertexAttribArray(OGLVertexAttributeID_TexCoord0);
		glEnableVertexAttribArray(OGLVertexAttributeID_Color);
		
		glVertexAttribPointer(OGLVertexAttributeID_Position, 4, GL_FLOAT, GL_FALSE, sizeof(VERT), (const GLvoid *)offsetof(VERT, coord));
		glVertexAttribPointer(OGLVertexAttributeID_TexCoord0, 2, GL_FLOAT, GL_FALSE, sizeof(VERT), (const GLvoid *)offsetof(VERT, texcoord));
		glVertexAttribPointer(OGLVertexAttributeID_Color, 3, GL_UNSIGNED_BYTE, GL_TRUE, sizeof(VERT), (const GLvoid *)offsetof(VERT, color));
	}
	
	return OGLERROR_NOERR;
}

Render3DError OpenGLES2Renderer::DisableVertexAttributes()
{
	if (this->isVAOSupported)
	{
		glBindVertexArrayOES(0);
	}
	else
	{
		glDisableVertexAttribArray(OGLVertexAttributeID_Position);
		glDisableVertexAttribArray(OGLVertexAttributeID_TexCoord0);
		glDisableVertexAttribArray(OGLVertexAttributeID_Color);
		
		glBindBuffer(GL_ARRAY_BUFFER, 0);
		glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
	}
	
	return OGLERROR_NOERR;
}

Render3DError OpenGLES2Renderer::SelectRenderingFramebuffer()
{
	OGLESRenderRef &OGLRef = *this->ref;
#ifdef __vita__
	glBindFramebuffer(GL_FRAMEBUFFER, this->doubleBufferIndex ? bottom_screen_fbo : top_screen_fbo);
	glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
#else
	OGLRef.selectedRenderingFBO = OGLRef.fboFinalOutputID;
	glBindFramebuffer(GL_FRAMEBUFFER, OGLRef.selectedRenderingFBO);
#endif
	return OGLERROR_NOERR;
}

Render3DError OpenGLES2Renderer::ReadBackPixels()
{
	const unsigned int i = this->doubleBufferIndex;
#ifndef __vita__
	OGLESRenderRef &OGLRef = *this->ref;
	u32 *__restrict workingBuffer = this->GPU_screen3D[i];
	glReadPixels(0, 0, 256, 192, GL_RGBA, GL_UNSIGNED_BYTE, workingBuffer);
	this->ConvertFramebuffer(workingBuffer, (u32 *)GPU->GetEngineMain()->Get3DFramebufferRGBA6665());
#endif
	return OGLERROR_NOERR;
}

Render3DError OpenGLES2Renderer::DeleteTexture(const TexCacheItem *item)
{
	this->ref->freeTextureIDs.push((GLuint)item->texid);
	if(this->currTexture == item)
	{
		this->currTexture = NULL;
	}
	
	return OGLERROR_NOERR;
}

Render3DError OpenGLES2Renderer::BeginRender(const GFX3D_State &renderState)
{
	OGLESRenderRef &OGLRef = *this->ref;
	this->doubleBufferIndex = (this->doubleBufferIndex + 1) & 0x01;
	
	this->SelectRenderingFramebuffer();
	
	glUniform1i(OGLRef.uniformEnableAlphaTest, renderState.enableAlphaTest ? GL_TRUE : GL_FALSE);
	glUniform1f(OGLRef.uniformAlphaTestRef, divide5bitBy31_LUT[renderState.alphaTestRef]);
	glUniform1i(OGLRef.uniformToonShadingMode, renderState.shading);
	glUniform1i(OGLRef.uniformWBuffer, renderState.wbuffer);
	
	if(renderState.enableAlphaBlending)
	{
		glEnable(GL_BLEND);
	}
	else
	{
		glDisable(GL_BLEND);
	}
	
	glDepthMask(GL_TRUE);
	
	return OGLERROR_NOERR;
}

Render3DError OpenGLES2Renderer::PreRender(const GFX3D_State *renderState, const VERTLIST *vertList, const POLYLIST *polyList, const INDEXLIST *indexList)
{
	printf("PreRender called with %u vertices\n", vertList->count);
	OGLESRenderRef &OGLRef = *this->ref;
	unsigned int vertIndexCount = 0;
	
	this->SetupVertices(vertList, polyList, indexList, OGLRef.vertIndexBuffer, &vertIndexCount);
	this->EnableVertexAttributes(vertList, OGLRef.vertIndexBuffer, vertIndexCount);
	
	return OGLERROR_NOERR;
}

Render3DError OpenGLES2Renderer::RenderGeometry(const GFX3D_State *renderState, const VERTLIST *vertList, const POLYLIST *polyList, const INDEXLIST *indexList)
{
	if (polyList->count > 0)
		this->PreRender(renderState, vertList, polyList, indexList);
	printf("DoRender called\n");
	OGLESRenderRef &OGLRef = *this->ref;
	u32 lastTexParams = 0;
	u32 lastTexPalette = 0;
	u32 lastPolyAttr = 0;
	u32 lastViewport = 0xFFFFFFFF;
	const unsigned int polyCount = polyList->count;
	//GLushort *indexBufferPtr = this->isVBOSupported ? 0 : OGLRef.vertIndexBuffer;
	GLushort *indexBufferPtr = 0;
	
	// Map GFX3D_QUADS and GFX3D_QUAD_STRIP to GL_TRIANGLES since we will convert them.
	//
	// Also map GFX3D_TRIANGLE_STRIP to GL_TRIANGLES. This is okay since this is actually
	// how the POLY struct stores triangle strip vertices, which is in sets of 3 vertices
	// each. This redefinition is necessary since uploading more than 3 indices at a time
	// will cause glDrawElements() to draw the triangle strip incorrectly.
	static const GLenum oglPrimitiveType[]	= {GL_TRIANGLES, GL_TRIANGLES, GL_TRIANGLES, GL_TRIANGLES,
											   GL_LINE_LOOP, GL_LINE_LOOP, GL_LINE_STRIP, GL_LINE_STRIP};
	
	static const unsigned int indexIncrementLUT[] = {3, 6, 3, 6, 3, 4, 3, 4};
	
	for(unsigned int i = 0; i < polyCount; i++)
	{
		const POLY *poly = &polyList->list[indexList->list[i]];
		
		// Set up the polygon if it changed
		if(lastPolyAttr != poly->polyAttr || i == 0)
		{
			lastPolyAttr = poly->polyAttr;
			this->SetupPolygon(poly);
		}
		
		// Set up the texture if it changed
		if(lastTexParams != poly->texParam || lastTexPalette != poly->texPalette || i == 0)
		{
			lastTexParams = poly->texParam;
			lastTexPalette = poly->texPalette;
			this->SetupTexture(poly, renderState->enableTexturing);
		}
		
		// Set up the viewport if it changed
		//if(lastViewport != poly->viewport || i == 0)
		{
			lastViewport = poly->viewport;
			this->SetupViewport(poly);
		}
		
		// In wireframe mode, redefine all primitives as GL_LINE_LOOP rather than
		// setting the polygon mode to GL_LINE though glPolygonMode(). Not only is
		// drawing more accurate this way, but it also allows GFX3D_QUADS and
		// GFX3D_QUAD_STRIP primitives to properly draw as wireframe without the
		// extra diagonal line.
		const GLenum polyPrimitive = !poly->isWireframe() ? oglPrimitiveType[poly->vtxFormat] : GL_LINE_LOOP;
		
		// Render the polygon
		const unsigned int vertIndexCount = indexIncrementLUT[poly->vtxFormat];
		//printf("Drawerino\n");
		glDrawElements(polyPrimitive, vertIndexCount, GL_UNSIGNED_SHORT, indexBufferPtr);
		//printf("Drawerino end\n");
		indexBufferPtr += vertIndexCount;
	}
	
	this->PostRender();
	return OGLERROR_NOERR;
}

Render3DError OpenGLES2Renderer::PostRender()
{
	this->DisableVertexAttributes();
	
	return OGLERROR_NOERR;
}

Render3DError OpenGLES2Renderer::EndRender(const u64 frameCount)
{
	//needs to happen before endgl because it could free some textureids for expired cache items
	TexCache_EvictFrame();

	printf("END\n");
	
	this->ReadBackPixels();
#ifdef __vita__
	glBindFramebuffer(GL_FRAMEBUFFER, 0);
#endif
	return OGLERROR_NOERR;
}

Render3DError OpenGLES2Renderer::UpdateClearImage(const u16 *__restrict colorBuffer, const u16 *__restrict depthBuffer, const u8 clearStencil, const u8 xScroll, const u8 yScroll)
{
	static const size_t pixelsPerLine = 256;
	static const size_t lineCount = 192;
	static const size_t totalPixelCount = pixelsPerLine * lineCount;
	static const size_t bufferSize = totalPixelCount * sizeof(u16);
	
	static CACHE_ALIGN GLushort clearImageColorBuffer[totalPixelCount] = {0};
	static CACHE_ALIGN GLint clearImageDepthBuffer[totalPixelCount] = {0};
	static CACHE_ALIGN u16 lastColorBuffer[totalPixelCount] = {0};
	static CACHE_ALIGN u16 lastDepthBuffer[totalPixelCount] = {0};
	static u8 lastXScroll = 0;
	static u8 lastYScroll = 0;
	
	if (!this->isFBOSupported)
	{
		return OGLERROR_FEATURE_UNSUPPORTED;
	}
	
	if (lastXScroll != xScroll ||
		lastYScroll != yScroll ||
		memcmp(colorBuffer, lastColorBuffer, bufferSize) ||
		memcmp(depthBuffer, lastDepthBuffer, bufferSize) )
	{
		lastXScroll = xScroll;
		lastYScroll = yScroll;
		memcpy(lastColorBuffer, colorBuffer, bufferSize);
		memcpy(lastDepthBuffer, depthBuffer, bufferSize);
		
		unsigned int dd = totalPixelCount - pixelsPerLine;
		
		for(unsigned int iy = 0; iy < lineCount; iy++)
		{
			const unsigned int y = ((iy + yScroll) & 0xFF) << 8;
			
			for(unsigned int ix = 0; ix < pixelsPerLine; ix++)
			{
				const unsigned int x = (ix + xScroll) & 0xFF;
				const unsigned int adr = y + x;
				
				clearImageColorBuffer[dd] = colorBuffer[adr];
				clearImageDepthBuffer[dd] = dsDepthToD24S8_LUT[depthBuffer[adr] & 0x7FFF] | clearStencil;
				
				dd++;
			}
			
			dd -= pixelsPerLine * 2;
		}
		
		this->UploadClearImage(clearImageColorBuffer, clearImageDepthBuffer);
	}
	
	this->clearImageStencilValue = clearStencil;
	
	return OGLERROR_NOERR;
}

Render3DError OpenGLES2Renderer::UpdateToonTable(const u16 *toonTableBuffer)
{
	static CACHE_ALIGN u16 currentToonTable16[32] = {0};

	// Update the toon table if it changed.
	if (memcmp(currentToonTable16, toonTableBuffer, sizeof(currentToonTable16)))
	{
		memcpy(currentToonTable16, toonTableBuffer, sizeof(currentToonTable16));

		/*for(int i=0;i<32;i++)
			this->currentToonTable32[i] = RGB15TO32_NOALPHA(toonTableBuffer[i]);*/

		this->toonTableNeedsUpdate = true;
	}
	
	return OGLERROR_NOERR;
}

Render3DError OpenGLES2Renderer::ClearUsingImage() const
{
	static u8 lastClearStencil = 0;
	
	if (!this->isFBOSupported)
	{
		return OGLERROR_FEATURE_UNSUPPORTED;
	}
	
	OGLESRenderRef &OGLRef = *this->ref;
	
	//glBindFramebufferEXT(GL_READ_FRAMEBUFFER_EXT, OGLRef.fboClearImageID);
	//glBindFramebufferEXT(GL_DRAW_FRAMEBUFFER_EXT, OGLRef.selectedRenderingFBO);
	//glBlitFramebufferEXT(0, 0, 256, 192, 0, 0, 256, 192, GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT, GL_NEAREST);
	//glBindFramebufferEXT(GL_FRAMEBUFFER_EXT, OGLRef.selectedRenderingFBO);
	
	if (lastClearStencil == this->clearImageStencilValue)
	{
		lastClearStencil = this->clearImageStencilValue;
		glClearStencil(lastClearStencil);
	}
	
	glClear(GL_STENCIL_BUFFER_BIT);
	
	return OGLERROR_NOERR;
}

Render3DError OpenGLES2Renderer::ClearUsingValues(const u8 r, const u8 g, const u8 b, const u8 a, const u32 clearDepth, const u8 clearStencil) const
{
	static u8 last_r = 0;
	static u8 last_g = 0;
	static u8 last_b = 0;
	static u8 last_a = 0;
	static u32 lastClearDepth = 0;
	static u8 lastClearStencil = 0;
	
	if (r != last_r || g != last_g || b != last_b || a != last_a)
	{
		last_r = r;
		last_g = g;
		last_b = b;
		last_a = a;
		glClearColor(divide5bitBy31_LUT[r], divide5bitBy31_LUT[g], divide5bitBy31_LUT[b], divide5bitBy31_LUT[a]);
	}
	
	if (clearDepth != lastClearDepth)
	{
		lastClearDepth = clearDepth;
		glClearDepthf((GLfloat)clearDepth / (GLfloat)0x00FFFFFF);
	}
	
	if (clearStencil != lastClearStencil)
	{
		lastClearStencil = clearStencil;
		glClearStencil(clearStencil);
	}
	
	glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);
	
	return OGLERROR_NOERR;
}

Render3DError OpenGLES2Renderer::SetupPolygon(const POLY *thePoly)
{
	static unsigned int lastTexBlendMode = 0;
	static int lastStencilState = -1;

	//printf("SetupPolygon called\n");
	
	OGLESRenderRef &OGLRef = *this->ref;
	const PolygonAttributes attr = thePoly->getAttributes();
	
	// Set up polygon ID
	glUniform1i(OGLRef.uniformPolyID, attr.polygonID);
	
	// Set up alpha value
	const GLfloat thePolyAlpha = (!attr.isWireframe && attr.isTranslucent) ? divide5bitBy31_LUT[attr.alpha] : 1.0f;
	glUniform1f(OGLRef.uniformPolyAlpha, thePolyAlpha);
	
	// Set up depth test mode
	static const GLenum oglDepthFunc[2] = {GL_LESS, GL_EQUAL};
	glDepthFunc(oglDepthFunc[attr.enableDepthEqualTest]);
	
	// Set up culling mode
	static const GLenum oglCullingMode[4] = {GL_FRONT_AND_BACK, GL_FRONT, GL_BACK, 0};
	GLenum cullingMode = oglCullingMode[attr.surfaceCullingMode];
	
	if (cullingMode == 0)
	{
		glDisable(GL_CULL_FACE);
	}
	else
	{
		glEnable(GL_CULL_FACE);
		glCullFace(cullingMode);
	}
	
	// Set up depth write
	GLboolean enableDepthWrite = GL_TRUE;
	
	// Handle shadow polys. Do this after checking for depth write, since shadow polys
	// can change this too.
	if(attr.polygonMode == 3)
	{
		glEnable(GL_STENCIL_TEST);
		if(attr.polygonID == 0)
		{
			enableDepthWrite = GL_FALSE;
			if(lastStencilState != 0)
			{
				lastStencilState = 0;
				//when the polyID is zero, we are writing the shadow mask.
				//set stencilbuf = 1 where the shadow volume is obstructed by geometry.
				//do not write color or depth information.
				glStencilFunc(GL_ALWAYS, 65, 255);
				glStencilOp(GL_KEEP, GL_REPLACE, GL_KEEP);
				glColorMask(GL_FALSE, GL_FALSE, GL_FALSE, GL_FALSE);
			}
		}
		else
		{
			enableDepthWrite = GL_TRUE;
			if(lastStencilState != 1)
			{
				lastStencilState = 1;
				//when the polyid is nonzero, we are drawing the shadow poly.
				//only draw the shadow poly where the stencilbuf==1.
				//I am not sure whether to update the depth buffer here--so I chose not to.
				glStencilFunc(GL_EQUAL, 65, 255);
				glStencilOp(GL_KEEP, GL_KEEP, GL_KEEP);
				glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
			}
		}
	}
	else
	{
		glEnable(GL_STENCIL_TEST);
		if(attr.isTranslucent)
		{
			lastStencilState = 3;
			glStencilFunc(GL_NOTEQUAL, attr.polygonID, 255);
			glStencilOp(GL_KEEP, GL_KEEP, GL_REPLACE);
			glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
		}
		else if(lastStencilState != 2)
		{
			lastStencilState = 2;
			glStencilFunc(GL_ALWAYS, 64, 255);
			glStencilOp(GL_REPLACE, GL_REPLACE, GL_REPLACE);
			glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
		}
	}
	
	if(attr.isTranslucent && !attr.enableAlphaDepthWrite)
	{
		enableDepthWrite = GL_FALSE;
	}
	
	glDepthMask(enableDepthWrite);
	
	// Set up texture blending mode
	if(attr.polygonMode != lastTexBlendMode)
	{
		lastTexBlendMode = attr.polygonMode;
		
		glUniform1i(OGLRef.uniformPolygonMode, attr.polygonMode);
			
		// Update the toon table if necessary
		if (this->toonTableNeedsUpdate && attr.polygonMode == 2)
		{
			this->UploadToonTable(this->currentToonTable32);
			this->toonTableNeedsUpdate = false;
		}
	}
	
	return OGLERROR_NOERR;
}

Render3DError OpenGLES2Renderer::SetupTexture(const POLY *thePoly, bool enableTexturing)
{
	OGLESRenderRef &OGLRef = *this->ref;
	const PolygonTexParams params = thePoly->getTexParams();
	
	// Check if we need to use textures
	if (thePoly->texParam == 0 || params.texFormat == TEXMODE_NONE || !enableTexturing)
	{
		glUniform1i(OGLRef.uniformHasTexture, GL_FALSE);
		
		return OGLERROR_NOERR;
	}
	
	// Enable textures if they weren't already enabled
	glUniform1i(OGLRef.uniformHasTexture, GL_TRUE);
	
	//	texCacheUnit.TexCache_SetTexture<TexFormat_32bpp>(format, texpal);
	TexCacheItem *newTexture = TexCache_SetTexture(TexFormat_32bpp, thePoly->texParam, thePoly->texPalette);
	if(newTexture != this->currTexture)
	{
		this->currTexture = newTexture;

		//has the ogl renderer initialized the texture?
		if(!this->currTexture->GetDeleteCallback())
		{
			this->currTexture->SetDeleteCallback(texDeleteCallback, 0, 0);
			
			if(OGLRef.freeTextureIDs.empty())
			{
				this->ExpandFreeTextures();
			}
			
			this->currTexture->texid = (u64)OGLRef.freeTextureIDs.front();
			OGLRef.freeTextureIDs.pop();
			
			glBindTexture(GL_TEXTURE_2D, (GLuint)this->currTexture->texid);
			
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, (params.enableRepeatS ? (params.enableMirroredRepeatS ? OGLRef.stateTexMirroredRepeat : GL_REPEAT) : GL_CLAMP_TO_EDGE));
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, (params.enableRepeatT ? (params.enableMirroredRepeatT ? OGLRef.stateTexMirroredRepeat : GL_REPEAT) : GL_CLAMP_TO_EDGE));
			
			glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA,
						 this->currTexture->sizeX, this->currTexture->sizeY, 0,
						 GL_RGBA, GL_UNSIGNED_BYTE, this->currTexture->decoded);
		}
		else
		{
			//otherwise, just bind it
			glBindTexture(GL_TEXTURE_2D, (GLuint)this->currTexture->texid);
		}
		
		glUniform2f(OGLRef.uniformTexScale, this->currTexture->invSizeX, this->currTexture->invSizeY);
	} else {
		glBindTexture(GL_TEXTURE_2D, (GLuint)this->currTexture->texid);
	}
	
	return OGLERROR_NOERR;
}

Render3DError OpenGLES2Renderer::SetupViewport(const POLY *thePoly)
{
	VIEWPORT viewport;
	viewport.decode(thePoly->viewport);
	glViewport(viewport.x, viewport.y, viewport.width, viewport.height);
	
	return OGLERROR_NOERR;
}

Render3DError OpenGLES2Renderer::Reset()
{
	OGLESRenderRef &OGLRef = *this->ref;
	
	this->gpuScreen3DHasNewData[0] = false;
	this->gpuScreen3DHasNewData[1] = false;
	
	glFinish();
	
	for (unsigned int i = 0; i < 2; i++)
	{
		memset(this->GPU_screen3D[i], 0, sizeof(this->GPU_screen3D[i]));
	}
	
	memset(currentToonTable32, 0, sizeof(currentToonTable32));
	this->UpdateToonTable((u16*)currentToonTable32);
	this->toonTableNeedsUpdate = true;
	
	glUniform1f(OGLRef.uniformPolyAlpha, 1.0f);
	glUniform2f(OGLRef.uniformTexScale, 1.0f, 1.0f);
	glUniform1i(OGLRef.uniformPolyID, 0);
	glUniform1i(OGLRef.uniformHasTexture, GL_FALSE);
	glUniform1i(OGLRef.uniformPolygonMode, 0);
	glUniform1i(OGLRef.uniformToonShadingMode, 0);
	glUniform1i(OGLRef.uniformWBuffer, 0);
	glUniform1i(OGLRef.uniformEnableAlphaTest, GL_TRUE);
	glUniform1f(OGLRef.uniformAlphaTestRef, 0.0f);
	
	memset(OGLRef.vertIndexBuffer, 0, OGLRENDER_VERT_INDEX_BUFFER_COUNT * sizeof(GLushort));
	this->currTexture = NULL;
	this->doubleBufferIndex = 0;
	this->clearImageStencilValue = 0;
	
	return OGLERROR_NOERR;
}

Render3DError OpenGLES2Renderer::RenderFinish()
{
	/*const unsigned int i = this->doubleBufferIndex;
	
	if (!this->gpuScreen3DHasNewData[i])
	{
		return OGLERROR_NOERR;
	}
	
	OGLESRenderRef &OGLRef = *this->ref;
	u32 *__restrict workingBuffer = this->GPU_screen3D[i];
	glReadPixels(0, 0, 256, 192, GL_RGBA, GL_UNSIGNED_BYTE, workingBuffer);
	this->ConvertFramebuffer(workingBuffer, (u32 *)GPU->GetEngineMain()->Get3DFramebufferRGBA6665());
	this->gpuScreen3DHasNewData[i] = false;*/
	
	return OGLERROR_NOERR;
}

Render3DError OpenGLES2Renderer::Render(const GFX3D &engine)
{
	Render3DError error = RENDER3DERROR_NOERR;
	
	error = this->BeginRender(engine.renderState);
	if (error != RENDER3DERROR_NOERR)
	{
		return error;
	}
	
	this->RenderGeometry(&engine.renderState, engine.vertlist, engine.polylist, &engine.indexlist);
	this->EndRender(engine.render3DFrameCount);
	
	return error;
}

static Render3D* OGLInit()
{
	char result = 0;
	Render3DError error = OGLERROR_NOERR;	 


	if(!BEGINGL())
	{
		printf("OpenGLES2: Could not initialize -- BEGINGL() failed.\n");
		_OGLRenderer = NULL;
			
		ENDGL();
		return _OGLRenderer;
	}
	
	// Get OpenGLES2 info
	const char *oglVersionString = (const char *)glGetString(GL_VERSION);
	const char *oglVendorString = (const char *)glGetString(GL_VENDOR);
	const char *oglRendererString = (const char *)glGetString(GL_RENDERER);
	
	// Check the driver's OpenGLES2 version
	/*_OGLDriverVersion.major = 2;
	_OGLDriverVersion.minor = 0;
	if (!IsVersionSupported(OGLRENDER_MINIMUM_DRIVER_VERSION_REQUIRED_MAJOR, OGLRENDER_MINIMUM_DRIVER_VERSION_REQUIRED_MINOR))
	{
		INFO("OpenGLES2: Driver does not support OpenGLES2 v%u.%u or later. Disabling 3D renderer.\n[ Driver Info -\n    Version: %s\n    Vendor: %s\n    Renderer: %s ]\n",
			 OGLRENDER_MINIMUM_DRIVER_VERSION_REQUIRED_MAJOR, OGLRENDER_MINIMUM_DRIVER_VERSION_REQUIRED_MINOR,
			 oglVersionString, oglVendorString, oglRendererString);
		
		result = 0;
		return result;
	}*/
	
	// If the renderer doesn't initialize with OpenGLES2 v3.2 or higher, fall back
	// to one of the lower versions.
	if (_OGLRenderer == NULL)
	{
		OGLESLoadEntryPoints();
		
		//if (IsVersionSupported(2, 0))
		{
			_OGLRenderer = new OpenGLES2Renderer;
			//_OGLRenderer->SetVersion(2, 0);
		}
	}
	
	if (_OGLRenderer == NULL)
	{
		printf("OpenGLES2: Renderer did not initialize. Disabling 3D renderer.\n[ Driver Info -\n    Version: %s\n    Vendor: %s\n    Renderer: %s ]\n",
			 oglVersionString, oglVendorString, oglRendererString);
			_OGLRenderer = NULL;
			
			ENDGL();
			return _OGLRenderer;
	}
	
	// Initialize OpenGLES2 extensions
	error = _OGLRenderer->InitExtensions();
	if (error != OGLERROR_NOERR)
	{
		if ( (error == OGLERROR_SHADER_CREATE_ERROR ||
			 error == OGLERROR_VERTEX_SHADER_PROGRAM_LOAD_ERROR ||
			 error == OGLERROR_FRAGMENT_SHADER_PROGRAM_LOAD_ERROR) )
		{
			printf("OpenGLES2: Shaders are not working, even though they should be. Disabling 3D renderer.\n");
			_OGLRenderer = NULL;
			
			ENDGL();
			return _OGLRenderer;
		}
	}
	
	// Initialization finished -- reset the renderer
	_OGLRenderer->Reset();
	
	ENDGL();
	
	/*unsigned int major = 0;
	unsigned int minor = 0;
	_OGLRenderer->GetVersion(&major, &minor);*/
	
	printf("OpenGLES2: Renderer initialized successfully (v%u.%u).\n[ Driver Info -\n    Version: %s\n    Vendor: %s\n    Renderer: %s ]\n",
		 2, 0, oglVersionString, oglVendorString, oglRendererString);
	
	return _OGLRenderer;
}


static void OGLReset()
{
	if(!BEGINGL())
		return;
	
	_OGLRenderer->Reset();
	
	ENDGL();
}

static void OGLClose()
{
	if(!BEGINGL())
		return;
	
	delete _OGLRenderer;
	_OGLRenderer = NULL;
	
	ENDGL();
}

//forcibly use new profile
GPU3DInterface gpu3DglES = {
	"OpenGLES 2.0",
	OGLInit,
	OGLClose
};

