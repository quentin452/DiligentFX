#include "BasicStructures.fxh"
#include "FullScreenTriangleVSOutput.fxh"
#include "PostFX_Common.fxh"
#include "TemporalAntiAliasingStructures.fxh"
#include "ScreenSpaceAmbientOcclusionStructures.fxh"
#include "SSAO_Common.fxh"

#pragma warning(disable : 3078)

cbuffer cbCameraAttribs
{
    CameraAttribs g_Camera;
}

Texture2D<float>  g_TextureOcclusion;
Texture2D<float>  g_TextureDepth;
Texture2D<float>  g_TextureHistory;
Texture2D<float3> g_TextureNormal;

SamplerState g_TextureDepth_sampler;
SamplerState g_TextureOcclusion_sampler;

float SampleOcclusion(int2 PixelCoord, int MipLevel)
{
    return g_TextureOcclusion.Load(int3(PixelCoord, MipLevel));
}

float SampleDepth(int2 PixelCoord, int MipLevel)
{
    return g_TextureDepth.Load(int3(PixelCoord, MipLevel));
}

float2 GetMipResolution(float2 ScreenDimensions, int MipLevel)
{
    return ScreenDimensions * rcp(float(1 << MipLevel));
}

float SampleDepthLinear(float2 Texcoord, int MipLevel)
{
    return g_TextureDepth.SampleLevel(g_TextureDepth_sampler, Texcoord, MipLevel);
}

float SampleOcclusionPoint(float2 Texcoord, int MipLevel)
{
    return g_TextureOcclusion.SampleLevel(g_TextureOcclusion_sampler, Texcoord, MipLevel);
}

float SampleHistory(int2 PixelCoord)
{
    return g_TextureHistory.Load(int3(PixelCoord, 0));
}

float3 SampleNormalWS(int2 PixelCoord)
{
    return g_TextureNormal.Load(int3(PixelCoord, 0));
}

float ComputeResampledHistoryPS(in FullScreenTriangleVSOutput VSOut) : SV_Target0
{
    float4 Position = VSOut.f4PixelPos;
    float Depth = SampleDepth(int2(Position.xy), 0);
    float History = SampleHistory(int2(Position.xy));
    float AccumulationFactor = (History - 1.0) / float(SSAO_OCCLUSION_HISTORY_MAX_FRAMES_WITH_HISTORY_FIX);
    
    if (IsBackground(Depth) || AccumulationFactor >= 1.0)
        return SampleOcclusion(int2(Position.xy), 0);
        
    int MipLevel = int(float(SSAO_DEPTH_HISTORY_CONVOLUTED_MAX_MIP) * (1.0 - saturate(AccumulationFactor)));
    float3 PositionSS = float3(Position.xy * g_Camera.f4ViewportSize.zw, Depth);
    float3 PositionVS = FastReconstructPosition(PositionSS, g_Camera.mProj);
    float3 NormalVS = mul(float4(SampleNormalWS(int2(Position.xy)), 0.0), g_Camera.mView).xyz;
    float PlaneNormalFactor = 10.0 / (1.0 + DepthToCameraZ(Depth, g_Camera.mProj));
    
    float OcclusionSum = 0.0;
    float WeightSum = 0.0;
    
    while (MipLevel >= 0 && WeightSum < 0.995) {

        float2 MipResolution = GetMipResolution(g_Camera.f4ViewportSize.xy, MipLevel);
        float2 MipLocation = Position.xy * rcp(float(1 << MipLevel));

        int2 MipLocationi = int2(MipLocation - 0.5);
        float x = frac(MipLocation.x + 0.5);
        float y = frac(MipLocation.y + 0.5);

        float4 Weight;
        Weight.x = (1.0 - x) * (1.0 - y);
        Weight.y = x * (1.0 - y);
        Weight.z = (1.0 - x) * y;
        Weight.w = x * y;
        
        OcclusionSum = 0.0;
        WeightSum = 0.0;
        
        for (int SampleIdx = 0; SampleIdx < 4; SampleIdx++)
        {
            int2 Location = MipLocationi + int2(SampleIdx & 0x01, SampleIdx >> 1);
            float2 Texcoord = (float2(Location) + 0.5) * rcp(MipResolution);
           
            float SampledDepth = SampleDepthLinear(Texcoord, MipLevel);
            float SampledOcclusion = SampleOcclusionPoint(Texcoord, MipLevel);

            float3 SamplePositionSS = float3(Texcoord, SampledDepth);
            float3 SamplePositionVS = FastReconstructPosition(SamplePositionSS, g_Camera.mProj);
            
            float WeightS = Weight[SampleIdx];
            float WeightZ = ComputeGeometryWeight(PositionVS, SamplePositionVS, NormalVS, PlaneNormalFactor);
            
            OcclusionSum += SampledOcclusion * WeightS * WeightZ;
            WeightSum += WeightS * WeightZ;
        }
        
        MipLevel--;
    }
    
    return OcclusionSum / WeightSum;
}
