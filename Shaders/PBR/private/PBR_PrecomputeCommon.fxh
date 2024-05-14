#ifndef _PBR_PRECOMPUTE_COMMON_FXH_
#define _PBR_PRECOMPUTE_COMMON_FXH_

#include "PBR_Common.fxh"

#ifndef PI
#   define PI 3.1415926536
#endif

float2 Hammersley2D(uint i, uint N) 
{
    // Radical inverse based on http://holger.dammertz.org/stuff/notes_HammersleyOnHemisphere.html
    uint bits = (i << 16u) | (i >> 16u);
    bits = ((bits & 0x55555555u) << 1u) | ((bits & 0xAAAAAAAAu) >> 1u);
    bits = ((bits & 0x33333333u) << 2u) | ((bits & 0xCCCCCCCCu) >> 2u);
    bits = ((bits & 0x0F0F0F0Fu) << 4u) | ((bits & 0xF0F0F0F0u) >> 4u);
    bits = ((bits & 0x00FF00FFu) << 8u) | ((bits & 0xFF00FF00u) >> 8u);
    float rdi = float(bits) * 2.3283064365386963e-10;
    return float2(float(i) / float(N), rdi);
}

// Based on http://blog.selfshadow.com/publications/s2013-shading-course/karis/s2013_pbs_epic_slides.pdf
float3 ImportanceSampleGGX(float2 Xi, float PerceptualRoughness, float3 N)
{
    float AlphaRoughness = PerceptualRoughness * PerceptualRoughness;
    float a2             = AlphaRoughness * AlphaRoughness;
    
    float Phi      = 2.0 * PI * Xi.x;
    float CosTheta = sqrt( (1.0 - Xi.y) / ( 1.0 + (a2 - 1.0) * Xi.y ) );
    float SinTheta = sqrt( 1.0 - CosTheta * CosTheta );
    float3 H;
    H.x = SinTheta * cos( Phi );
    H.y = SinTheta * sin( Phi );
    H.z = CosTheta;
    float3 UpVector = abs(N.z) < 0.999 ? float3(0.0, 0.0, 1.0) : float3(1.0, 0.0, 0.0);
    float3 TangentX = normalize( cross( UpVector, N ) );
    float3 TangentY = cross( N, TangentX );
    // Tangent to world space
    return TangentX * H.x + TangentY * H.y + N * H.z;
}

float ComputeCubeMapPixelSolidAngle(float Width, float Height)
{
    return 4.0 * PI / (6.0 * Width * Height);
} 

float ComputeSphereMapPixelSolidAngle(float Width, float Height, float Theta, float Gamma)
{
    float dTheta = PI / Width;
    float dPhi = 2.0 * PI / Height;
    return dPhi * (cos(Theta - 0.5 * dTheta * Gamma) - cos(Theta + 0.5 * dTheta * Gamma));
}

#endif // _PBR_PRECOMPUTE_COMMON_FXH_
