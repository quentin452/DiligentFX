"#include \"ScreenSpaceReflectionStructures.fxh\"\n"
"#include \"SSR_Common.fxh\"\n"
"\n"
"cbuffer cbScreenSpaceReflectionAttribs\n"
"{\n"
"    ScreenSpaceReflectionAttribs g_SSRAttribs;\n"
"}\n"
"\n"
"Texture2D<uint> g_SobolBuffer;\n"
"Texture2D<uint> g_ScramblingTileBuffer;\n"
"\n"
"// Blue Noise Sampler by Eric Heitz. Returns a value in the range [0, 1].\n"
"float SampleRandomNumber(uint2 PixelCoord, uint SampleIndex, uint SampleDimension)\n"
"{\n"
"    // Wrap arguments\n"
"    PixelCoord = PixelCoord & 127u;\n"
"    SampleIndex = SampleIndex & 255u;\n"
"    SampleDimension = SampleDimension & 255u;\n"
"\n"
"    // xor index based on optimized ranking\n"
"    const uint RankedSampleIndex = SampleIndex;\n"
"\n"
"    // Fetch value in sequence\n"
"    uint Value = g_SobolBuffer.Load(uint3(SampleDimension, RankedSampleIndex * 256u, 0));\n"
"\n"
"    // If the dimension is optimized, xor sequence value based on optimized scrambling\n"
"    uint OriginalIndex = (SampleDimension % 8u) + (PixelCoord.x + PixelCoord.y * 128u) * 8u;\n"
"    Value = Value ^ g_ScramblingTileBuffer.Load(uint3(OriginalIndex % 512u, OriginalIndex / 512u, 0));\n"
"\n"
"    return (Value + 0.5f) / 256.0f;\n"
"}\n"
"\n"
"float2 SampleRandomVector2D(uint2 PixelCoord)\n"
"{\n"
"    return float2(\n"
"        fmod(SampleRandomNumber(PixelCoord, 0, 0u) + (g_SSRAttribs.FrameIndex & 0xFFu) * M_GOLDEN_RATIO, 1.0),\n"
"        fmod(SampleRandomNumber(PixelCoord, 0, 1u) + (g_SSRAttribs.FrameIndex & 0xFFu) * M_GOLDEN_RATIO, 1.0));\n"
"}\n"
"\n"
"float2 ComputeBlueNoiseTexturePS(in float4 Position : SV_Position) : SV_Target\n"
"{\n"
"    return SampleRandomVector2D(uint2(Position.xy));\n"
"}\n"
