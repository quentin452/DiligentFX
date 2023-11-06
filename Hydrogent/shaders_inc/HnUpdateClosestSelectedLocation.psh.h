"#include \"HnClosestSelectedLocation.fxh\"\n"
"\n"
"struct PSInput\n"
"{\n"
"    float4 Pos : SV_POSITION;\n"
"};\n"
"\n"
"cbuffer cbConstants\n"
"{\n"
"    ClosestSelectedLocationConstants g_Constants;\n"
"}\n"
"\n"
"Texture2D g_SrcClosestLocation;\n"
"\n"
"void UpdateClosestLocation(in    float2 CurrentLocation,\n"
"                           in    float2 Offset,\n"
"                           inout float2 ClosestLocation,\n"
"                           inout bool   IsLocationValid,\n"
"                           inout float  ClosestDistance)\n"
"{\n"
"    float2 TestSelectedLocation;\n"
"    if (!DecodeClosestSelectedLocation(g_SrcClosestLocation.Load(int3(CurrentLocation + Offset * g_Constants.SampleRange, 0)).xy, TestSelectedLocation))\n"
"        return;\n"
"\n"
"    float Width;\n"
"    float Height;\n"
"    g_SrcClosestLocation.GetDimensions(Width, Height);\n"
"    float2 Dir = TestSelectedLocation * float2(Width, Height) - CurrentLocation;\n"
"    float DistSqr = dot(Dir, Dir);\n"
"    if (DistSqr < ClosestDistance)\n"
"    {\n"
"        ClosestDistance = DistSqr;\n"
"        ClosestLocation = TestSelectedLocation;\n"
"        IsLocationValid = true;\n"
"    }\n"
"}\n"
"\n"
"void main(in  PSInput PSIn,\n"
"          out float2  Location : SV_Target0)\n"
"{\n"
"    float2 ClosestLocation = float2(0.0, 0.0);\n"
"    bool   IsLocationValid = false;\n"
"    float  ClosestDistance = 1e+10;\n"
"\n"
"    UpdateClosestLocation(PSIn.Pos.xy, float2(-1.0, -1.0), ClosestLocation, IsLocationValid, ClosestDistance);\n"
"    UpdateClosestLocation(PSIn.Pos.xy, float2( 0.0, -1.0), ClosestLocation, IsLocationValid, ClosestDistance);\n"
"    UpdateClosestLocation(PSIn.Pos.xy, float2(+1.0, -1.0), ClosestLocation, IsLocationValid, ClosestDistance);\n"
"\n"
"    UpdateClosestLocation(PSIn.Pos.xy, float2(-1.0,  0.0), ClosestLocation, IsLocationValid, ClosestDistance);\n"
"    UpdateClosestLocation(PSIn.Pos.xy, float2( 0.0,  0.0), ClosestLocation, IsLocationValid, ClosestDistance);\n"
"    UpdateClosestLocation(PSIn.Pos.xy, float2(+1.0,  0.0), ClosestLocation, IsLocationValid, ClosestDistance);\n"
"\n"
"    UpdateClosestLocation(PSIn.Pos.xy, float2(-1.0, +1.0), ClosestLocation, IsLocationValid, ClosestDistance);\n"
"    UpdateClosestLocation(PSIn.Pos.xy, float2( 0.0, +1.0), ClosestLocation, IsLocationValid, ClosestDistance);\n"
"    UpdateClosestLocation(PSIn.Pos.xy, float2(+1.0, +1.0), ClosestLocation, IsLocationValid, ClosestDistance);\n"
"\n"
"    Location = EncodeClosestSelectedLocation(ClosestLocation, IsLocationValid);\n"
"}\n"
