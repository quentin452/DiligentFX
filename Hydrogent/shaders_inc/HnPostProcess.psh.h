"#include \"ToneMapping.fxh\"\n"
"#include \"HnPostProcessStructures.fxh\"\n"
"#include \"HnClosestSelectedLocation.fxh\"\n"
"#include \"FullScreenTriangleVSOutput.fxh\"\n"
"\n"
"cbuffer cbPostProcessAttribs\n"
"{\n"
"    PostProcessAttribs g_Attribs;\n"
"}\n"
"\n"
"Texture2D g_ColorBuffer;\n"
"Texture2D g_SelectionDepth;\n"
"Texture2D g_Depth;\n"
"Texture2D g_ClosestSelectedLocation;\n"
"Texture2D g_SSR;\n"
"Texture2D g_SpecularIBL;\n"
"\n"
"void main(in  FullScreenTriangleVSOutput VSOut,\n"
"          out float4                     Color : SV_Target0)\n"
"{\n"
"    float4 Pos = VSOut.f4PixelPos;\n"
"\n"
"    Color = g_ColorBuffer.Load(int3(Pos.xy, 0));\n"
"\n"
"    float4 SpecularIBL = g_SpecularIBL.Load(int3(Pos.xy, 0));\n"
"    float4 SSRRadiance = g_SSR.Load(int3(Pos.xy, 0));\n"
"\n"
"    Color.rgb += (SSRRadiance.rgb - SpecularIBL.rgb) * SSRRadiance.w * Color.a;\n"
"\n"
"#if TONE_MAPPING_MODE > TONE_MAPPING_MODE_NONE\n"
"    Color.rgb = ToneMap(Color.rgb, g_Attribs.ToneMapping, g_Attribs.AverageLogLum);\n"
"#endif\n"
"\n"
"    float SelectionDepth = g_SelectionDepth.Load(int3(Pos.xy, 0)).r;\n"
"    float Depth          = g_Depth.Load(int3(Pos.xy, 0)).r;\n"
"    bool  IsSelected     = Depth != g_Attribs.ClearDepth && SelectionDepth == Depth;\n"
"\n"
"    float  Outline = 0.0;\n"
"    float2 ClosestSelectedLocation;\n"
"    if (DecodeClosestSelectedLocation(g_ClosestSelectedLocation.Load(int3(Pos.xy, 0)).xy, ClosestSelectedLocation))\n"
"    {\n"
"        float Width, Height;\n"
"        g_ColorBuffer.GetDimensions(Width, Height);\n"
"        ClosestSelectedLocation *= float2(Width, Height);\n"
"        // Get distance in pixels from the current pixel to the closest selected pixel\n"
"        float Dist = length(ClosestSelectedLocation - Pos.xy);\n"
"        // Make outline fade out with distance from the closest selected pixel\n"
"        Outline = saturate(1.0 - Dist / g_Attribs.SelectionOutlineWidth);\n"
"        // Do not highlight the selected object itself\n"
"        Outline *= SelectionDepth != g_Attribs.ClearDepth ? 0.0 : 1.0;\n"
"    }\n"
"\n"
"    // Desaturate all unselected pixels\n"
"    float DesatFactor = IsSelected ? 0.0 : g_Attribs.NonselectionDesaturationFactor;\n"
"    float Luminance = dot(Color.rgb, float3(0.2126, 0.7152, 0.0722));\n"
"    Color.rgb = lerp(Color.rgb, float3(Luminance, Luminance, Luminance), DesatFactor);\n"
"\n"
"    Color.rgb = lerp(Color.rgb, g_Attribs.SelectionOutlineColor.rgb, Outline);\n"
"\n"
"#if CONVERT_OUTPUT_TO_SRGB\n"
"    Color.rgb = pow(Color.rgb, float3(1.0/2.2, 1.0/2.2, 1.0/2.2));\n"
"#endif\n"
"\n"
"    Color.a = 1.0;\n"
"}\n"
