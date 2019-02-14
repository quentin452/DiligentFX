"\n"
"\n"
"void ComputeUnshadowedInscattering(float2 f2SampleLocation, \n"
"                                   float fCamSpaceZ,\n"
"                                   const float fNumSteps,\n"
"                                   out float3 f3Inscattering,\n"
"                                   out float3 f3Extinction)\n"
"{\n"
"    f3Inscattering = float3(0.0, 0.0, 0.0);\n"
"    f3Extinction = float3(1.0, 1.0, 1.0);\n"
"    float3 f3RayTermination = ProjSpaceXYZToWorldSpace( float3(f2SampleLocation, fCamSpaceZ), g_CameraAttribs.mProj, g_CameraAttribs.mViewProjInv );\n"
"    float3 f3CameraPos = g_CameraAttribs.f4CameraPos.xyz;\n"
"    float3 f3ViewDir = f3RayTermination - f3CameraPos;\n"
"    float fRayLength = length(f3ViewDir);\n"
"    f3ViewDir /= fRayLength;\n"
"\n"
"    float3 f3EarthCentre =  - float3(0.0, 1.0, 0.0) * EARTH_RADIUS;\n"
"    float2 f2RayAtmTopIsecs;\n"
"    GetRaySphereIntersection( f3CameraPos, f3ViewDir, f3EarthCentre, \n"
"                              ATM_TOP_RADIUS, \n"
"                              f2RayAtmTopIsecs);\n"
"    if( f2RayAtmTopIsecs.y <= 0.0 )\n"
"        return;\n"
"\n"
"    float3 f3RayStart = f3CameraPos + f3ViewDir * max(0.0, f2RayAtmTopIsecs.x);\n"
"    if( fCamSpaceZ > g_CameraAttribs.fFarPlaneZ ) // fFarPlaneZ is pre-multiplied with 0.999999f\n"
"        fRayLength = +FLT_MAX;\n"
"    float3 f3RayEnd = f3CameraPos + f3ViewDir * min(fRayLength, f2RayAtmTopIsecs.y);\n"
"\n"
"#if SINGLE_SCATTERING_MODE == SINGLE_SCTR_MODE_INTEGRATION\n"
"    IntegrateUnshadowedInscattering(f3RayStart, \n"
"                                    f3RayEnd,\n"
"                                    f3ViewDir,\n"
"                                    f3EarthCentre,\n"
"                                    -g_LightAttribs.f4Direction.xyz,\n"
"                                    fNumSteps,\n"
"                                    f3Inscattering,\n"
"                                    f3Extinction);\n"
"#endif\n"
"\n"
"#if SINGLE_SCATTERING_MODE == SINGLE_SCTR_MODE_LUT || MULTIPLE_SCATTERING_MODE > MULTIPLE_SCTR_MODE_NONE\n"
"\n"
"#if MULTIPLE_SCATTERING_MODE > MULTIPLE_SCTR_MODE_NONE\n"
"    #if SINGLE_SCATTERING_MODE == SINGLE_SCTR_MODE_LUT\n"
"        #define tex3DSctrLUT         g_tex3DMultipleSctrLUT\n"
"        #define tex3DSctrLUT_sampler g_tex3DMultipleSctrLUT_sampler\n"
"    #elif SINGLE_SCATTERING_MODE == SINGLE_SCTR_MODE_NONE || SINGLE_SCATTERING_MODE == SINGLE_SCTR_MODE_INTEGRATION\n"
"        #define tex3DSctrLUT         g_tex3DHighOrderSctrLUT\n"
"        #define tex3DSctrLUT_sampler g_tex3DHighOrderSctrLUT_sampler\n"
"    #endif\n"
"#else\n"
"    #define tex3DSctrLUT         g_tex3DSingleSctrLUT\n"
"    #define tex3DSctrLUT_sampler g_tex3DSingleSctrLUT_sampler\n"
"#endif\n"
"\n"
"    f3Extinction = GetExtinctionUnverified(f3RayStart, f3RayEnd, f3ViewDir, f3EarthCentre);\n"
"\n"
"    // To avoid artifacts, we must be consistent when performing look-ups into the scattering texture, i.e.\n"
"    // we must assure that if the first look-up is above (below) horizon, then the second look-up\n"
"    // is also above (below) horizon. \n"
"    float4 f4UVWQ = float4(-1.0, -1.0, -1.0, -1.0);\n"
"    f3Inscattering +=                LookUpPrecomputedScattering(f3RayStart, f3ViewDir, f3EarthCentre, -g_LightAttribs.f4Direction.xyz, tex3DSctrLUT, tex3DSctrLUT_sampler, f4UVWQ); \n"
"    // Provide previous look-up coordinates to the function to assure that look-ups are consistent\n"
"    f3Inscattering -= f3Extinction * LookUpPrecomputedScattering(f3RayEnd,   f3ViewDir, f3EarthCentre, -g_LightAttribs.f4Direction.xyz, tex3DSctrLUT, tex3DSctrLUT_sampler, f4UVWQ);\n"
"    #undef tex3DSctrLUT\n"
"    #undef tex3DSctrLUT_sampler\n"
"#endif\n"
"\n"
"}\n"
