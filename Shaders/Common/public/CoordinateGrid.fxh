#ifndef _COORDINATE_GRID_FXH_
#define _COORDINATE_GRID_FXH_

#include "CoordinateGridStructures.fxh"
#include "PostFX_Common.fxh"

#if GRID_AXES_OPTION_INVERTED_DEPTH
    #define DepthNearPlane     1.0
    #define DepthFarPlane      0.0
    #define DepthMin           max
    #define DepthCompare(x, y) ((x)>(y))
#else
    #define DepthNearPlane     0.0
    #define DepthFarPlane      1.0
    #define DepthMin           min
    #define DepthCompare(x, y) ((x)<(y))
#endif // GRID_AXES_OPTION_INVERTED_DEPTH

struct Ray
{
    float3 Origin;
    float3 Direction;
};

Ray CreateCameraRay(float2   NormalizedXY,
                    float4x4 CameraViewProjInv,
                    float3   CameraPosition)
{
    Ray CameraRay;
    float4 RayStart = mul(float4(NormalizedXY, DepthToNormalizedDeviceZ(DepthNearPlane), 1.0f), CameraViewProjInv);
    float4 RayEnd   = mul(float4(NormalizedXY, DepthToNormalizedDeviceZ(DepthFarPlane),  1.0f), CameraViewProjInv);

    RayStart.xyz /= RayStart.w;
    RayEnd.xyz   /= RayEnd.w;
    CameraRay.Direction = normalize(RayEnd.xyz - RayStart.xyz);
    CameraRay.Origin    = CameraPosition;
    return CameraRay;
}

float ComputeRayPlaneIntersection(Ray RayWS, float3 PlaneNormal, float3 PlaneOrigin)
{
    float NdotD = dot(RayWS.Direction, PlaneNormal);
    return dot(PlaneNormal, (PlaneOrigin - RayWS.Origin)) / NdotD;
}

float4 ComputeGrid(float2 PlanePos, float Scale, bool IsVisible) 
{
    float2 Coord = PlanePos * Scale; // use the scale variable to set the distance between the lines
    float2 Magnitude = fwidth(Coord);
    if (IsVisible)
    {
        float2 Grid = abs(frac(Coord - 0.5) - 0.5) / Magnitude;
        float Line = min(Grid.x, Grid.y);
        return float4(0.2, 0.2, 0.2, 1.0 - min(Line, 1.0));
    }
    else
    {
        return float4(0.0, 0.0, 0.0, 0.0);
    }
}

float2 ComputeAxisAlphaFromSinglePlane(float Axis, float Width, bool IsVisible)
{
    float Magnitude = Width * fwidth(Axis);
    Magnitude = max(Magnitude, 1e-5);
    float Line = abs(Axis) / Magnitude;
    return float2((1.0 - min(Line * Line, 1.0)) * (IsVisible ? 1.0 : 0.0), 1.0 / Magnitude);
}

float ComputeAxisAlphaFromTwoPlanes(float Axis0, bool IsVisible0, float DepthAlpha0,
                                    float Axis1, bool IsVisible1, float DepthAlpha1,
                                    float Width)
{
    float2 AxisAlpha0 = ComputeAxisAlphaFromSinglePlane(Axis0, Width, IsVisible0);
    float2 AxisAlpha1 = ComputeAxisAlphaFromSinglePlane(Axis1, Width, IsVisible1);
    return (AxisAlpha0.x * AxisAlpha0.y + AxisAlpha1.x * AxisAlpha1.y) / (AxisAlpha0.y + AxisAlpha1.y);
}

float ComputeNDCDepth(float3 Position, float4x4 CameraViewProj)
{
    float4 Position_NDC = mul(float4(Position, 1.0), CameraViewProj);
    return Position_NDC.z / Position_NDC.w;
}

void ComputePlaneIntersectionAttribs(in CameraAttribs Camera,
                                     in Ray           RayWS,
                                     in float3        Normal,
                                     in float         GeometryDepth,
                                     out float3       Position,
                                     out bool         IsVisible,
                                     out float        DepthAlpha)
{
    Position = RayWS.Origin + RayWS.Direction * ComputeRayPlaneIntersection(RayWS, Normal, float3(0.0, 0.0, 0.0)); 
    float  CameraZ  = mul(float4(Position, 1.0), Camera.mView).z;
    float  Depth    = CameraZToNormalizedDeviceZ(CameraZ, Camera.mProj);
    IsVisible  = DepthCompare(Depth, GeometryDepth);
    DepthAlpha = saturate(1.0 - CameraZ / Camera.fFarPlaneZ);
}

float4 ComputeCoordinateGrid(in float2                f2NormalizedXY,
                             in CameraAttribs         Camera,
                             in float                 GeometryDepth,
                             in CoordinateGridAttribs GridAttribs)
{
    Ray RayWS = CreateCameraRay(f2NormalizedXY, Camera.mViewProjInv, Camera.f4Position.xyz);

    float3 Positions[3];
    bool   IsVisible[3];
    float  DepthAlpha[3];
    
    ComputePlaneIntersectionAttribs(Camera, RayWS, float3(1.0, 0.0, 0.0), GeometryDepth, Positions[0], IsVisible[0], DepthAlpha[0]);
    ComputePlaneIntersectionAttribs(Camera, RayWS, float3(0.0, 1.0, 0.0), GeometryDepth, Positions[1], IsVisible[1], DepthAlpha[1]);
    ComputePlaneIntersectionAttribs(Camera, RayWS, float3(0.0, 0.0, 1.0), GeometryDepth, Positions[2], IsVisible[2], DepthAlpha[2]);

    float4 GridResult = float4(0.0, 0.0, 0.0, 0.0);
    float4 AxisResult = float4(0.0, 0.0, 0.0, 0.0);

#if GRID_AXES_OPTION_AXIS_X
    {
       AxisResult += float4(GridAttribs.XAxisColor.xyz, 1.0) * ComputeAxisAlphaFromTwoPlanes(
                Positions[1].x, IsVisible[1], DepthAlpha[1],
                Positions[0].y, IsVisible[0], DepthAlpha[0],
                GridAttribs.XAxisWidth);
    }
#endif

#if GRID_AXES_OPTION_AXIS_Y
    {
        AxisResult += float4(GridAttribs.YAxisColor.xyz, 1.0) * ComputeAxisAlphaFromTwoPlanes(
                Positions[0].z, IsVisible[0], DepthAlpha[0],
                Positions[2].x, IsVisible[2], DepthAlpha[2],
                GridAttribs.YAxisWidth);
    }
#endif

#if GRID_AXES_OPTION_AXIS_Z
    {
        AxisResult += float4(GridAttribs.ZAxisColor.xyz, 1.0) * ComputeAxisAlphaFromTwoPlanes(
                Positions[1].z, IsVisible[1], DepthAlpha[1],
                Positions[2].y, IsVisible[2], DepthAlpha[2],
                GridAttribs.ZAxisWidth);
    }
#endif

#if GRID_AXES_OPTION_PLANE_YZ
    {
        GridResult += (0.2 * ComputeGrid(Positions[0].yz, GridAttribs.GridSubdivision.x * GridAttribs.GridScale.x, IsVisible[0]) +
                       0.8 * ComputeGrid(Positions[0].yz, GridAttribs.GridScale.x, IsVisible[0])) * DepthAlpha[0];
    }
#endif 

#if GRID_AXES_OPTION_PLANE_XZ
    {
        GridResult += (0.2 * ComputeGrid(Positions[1].xz, GridAttribs.GridSubdivision.y * GridAttribs.GridScale.y, IsVisible[1]) +
                       0.8 * ComputeGrid(Positions[1].xz, GridAttribs.GridScale.y, IsVisible[1])) * DepthAlpha[1]; 
    }
#endif

#if GRID_AXES_OPTION_PLANE_XY
    {
        GridResult += (0.2 * ComputeGrid(Positions[2].xy, GridAttribs.GridSubdivision.z * GridAttribs.GridScale.z, IsVisible[2]) +
                       0.8 * ComputeGrid(Positions[2].xy, GridAttribs.GridScale.z, IsVisible[2])) * DepthAlpha[2]; 
    }
#endif

    float4 Result;
    Result.rgb = GridResult.rgb * exp(-10.0 * AxisResult.a * AxisResult.a) + AxisResult.rgb;
    Result.a   = GridResult.a * (1.0 - AxisResult.a) + AxisResult.a;

    return Result;
}

#endif //_COORDINATE_GRID_FXH_
