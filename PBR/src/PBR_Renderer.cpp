/*
 *  Copyright 2019-2023 Diligent Graphics LLC
 *
 *  Licensed under the Apache License, Version 2.0 (the "License");
 *  you may not use this file except in compliance with the License.
 *  You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 *  Unless required by applicable law or agreed to in writing, software
 *  distributed under the License is distributed on an "AS IS" BASIS,
 *  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *  See the License for the specific language governing permissions and
 *  limitations under the License.
 *
 *  In no event and under no legal theory, whether in tort (including negligence),
 *  contract, or otherwise, unless required by applicable law (such as deliberate
 *  and grossly negligent acts) or agreed to in writing, shall any Contributor be
 *  liable for any damages, including any direct, indirect, special, incidental,
 *  or consequential damages of any character arising as a result of this License or
 *  out of the use or inability to use the software (including but not limited to damages
 *  for loss of goodwill, work stoppage, computer failure or malfunction, or any and
 *  all other commercial damages or losses), even if such Contributor has been advised
 *  of the possibility of such damages.
 */

#include "PBR_Renderer.hpp"

#include <array>
#include <vector>

#include "RenderStateCache.hpp"
#include "ShaderMacroHelper.hpp"
#include "GraphicsUtilities.h"
#include "CommonlyUsedStates.h"
#include "BasicMath.hpp"
#include "MapHelper.hpp"
#include "../../../Utilities/include/DiligentFXShaderSourceStreamFactory.hpp"

namespace Diligent
{

const SamplerDesc PBR_Renderer::CreateInfo::DefaultSampler = Sam_LinearWrap;

namespace
{

namespace HLSL
{

#include "Shaders/PBR/public/PBR_Structures.fxh"

}

} // namespace

PBR_Renderer::PBR_Renderer(IRenderDevice*     pDevice,
                           IRenderStateCache* pStateCache,
                           IDeviceContext*    pCtx,
                           const CreateInfo&  CI) :
    m_Settings{CI}
{
    if (m_Settings.UseIBL)
    {
        PrecomputeBRDF(pDevice, pStateCache, pCtx, m_Settings.NumBRDFSamples);

        TextureDesc TexDesc;
        TexDesc.Name      = "Irradiance cube map for PBR renderer";
        TexDesc.Type      = RESOURCE_DIM_TEX_CUBE;
        TexDesc.Usage     = USAGE_DEFAULT;
        TexDesc.BindFlags = BIND_SHADER_RESOURCE | BIND_RENDER_TARGET;
        TexDesc.Width     = IrradianceCubeDim;
        TexDesc.Height    = IrradianceCubeDim;
        TexDesc.Format    = IrradianceCubeFmt;
        TexDesc.ArraySize = 6;
        TexDesc.MipLevels = 0;

        RefCntAutoPtr<ITexture> IrradainceCubeTex;
        pDevice->CreateTexture(TexDesc, nullptr, &IrradainceCubeTex);
        m_pIrradianceCubeSRV = IrradainceCubeTex->GetDefaultView(TEXTURE_VIEW_SHADER_RESOURCE);

        TexDesc.Name   = "Prefiltered environment map for PBR renderer";
        TexDesc.Width  = PrefilteredEnvMapDim;
        TexDesc.Height = PrefilteredEnvMapDim;
        TexDesc.Format = PrefilteredEnvMapFmt;
        RefCntAutoPtr<ITexture> PrefilteredEnvMapTex;
        pDevice->CreateTexture(TexDesc, nullptr, &PrefilteredEnvMapTex);
        m_pPrefilteredEnvMapSRV = PrefilteredEnvMapTex->GetDefaultView(TEXTURE_VIEW_SHADER_RESOURCE);
    }

    {
        static constexpr Uint32 TexDim = 8;

        TextureDesc TexDesc;
        TexDesc.Name      = "White texture for PBR renderer";
        TexDesc.Type      = RESOURCE_DIM_TEX_2D_ARRAY;
        TexDesc.Usage     = USAGE_IMMUTABLE;
        TexDesc.BindFlags = BIND_SHADER_RESOURCE;
        TexDesc.Width     = TexDim;
        TexDesc.Height    = TexDim;
        TexDesc.Format    = TEX_FORMAT_RGBA8_UNORM;
        TexDesc.MipLevels = 1;
        std::vector<Uint32>     Data(TexDim * TexDim, 0xFFFFFFFF);
        TextureSubResData       Level0Data{Data.data(), TexDim * 4};
        TextureData             InitData{&Level0Data, 1};
        RefCntAutoPtr<ITexture> pWhiteTex;
        pDevice->CreateTexture(TexDesc, &InitData, &pWhiteTex);
        m_pWhiteTexSRV = pWhiteTex->GetDefaultView(TEXTURE_VIEW_SHADER_RESOURCE);

        TexDesc.Name = "Black texture for PBR renderer";
        for (auto& c : Data) c = 0;
        RefCntAutoPtr<ITexture> pBlackTex;
        pDevice->CreateTexture(TexDesc, &InitData, &pBlackTex);
        m_pBlackTexSRV = pBlackTex->GetDefaultView(TEXTURE_VIEW_SHADER_RESOURCE);

        TexDesc.Name = "Default normal map for PBR renderer";
        for (auto& c : Data) c = 0x00FF7F7F;
        RefCntAutoPtr<ITexture> pDefaultNormalMap;
        pDevice->CreateTexture(TexDesc, &InitData, &pDefaultNormalMap);
        m_pDefaultNormalMapSRV = pDefaultNormalMap->GetDefaultView(TEXTURE_VIEW_SHADER_RESOURCE);

        TexDesc.Name = "Default physical description map for PBR renderer";
        for (auto& c : Data) c = 0x0000FF00;
        RefCntAutoPtr<ITexture> pDefaultPhysDesc;
        pDevice->CreateTexture(TexDesc, &InitData, &pDefaultPhysDesc);
        m_pDefaultPhysDescSRV = pDefaultPhysDesc->GetDefaultView(TEXTURE_VIEW_SHADER_RESOURCE);

        // clang-format off
        StateTransitionDesc Barriers[] = 
        {
            {pWhiteTex,         RESOURCE_STATE_UNKNOWN, RESOURCE_STATE_SHADER_RESOURCE, STATE_TRANSITION_FLAG_UPDATE_STATE},
            {pBlackTex,         RESOURCE_STATE_UNKNOWN, RESOURCE_STATE_SHADER_RESOURCE, STATE_TRANSITION_FLAG_UPDATE_STATE},
            {pDefaultNormalMap, RESOURCE_STATE_UNKNOWN, RESOURCE_STATE_SHADER_RESOURCE, STATE_TRANSITION_FLAG_UPDATE_STATE},
            {pDefaultPhysDesc,  RESOURCE_STATE_UNKNOWN, RESOURCE_STATE_SHADER_RESOURCE, STATE_TRANSITION_FLAG_UPDATE_STATE}
        };
        // clang-format on
        pCtx->TransitionResourceStates(_countof(Barriers), Barriers);

        RefCntAutoPtr<ISampler> pDefaultSampler;
        pDevice->CreateSampler(Sam_LinearClamp, &pDefaultSampler);
        m_pWhiteTexSRV->SetSampler(pDefaultSampler);
        m_pBlackTexSRV->SetSampler(pDefaultSampler);
        m_pDefaultNormalMapSRV->SetSampler(pDefaultSampler);
    }

    if (CI.RTVFmt != TEX_FORMAT_UNKNOWN || CI.DSVFmt != TEX_FORMAT_UNKNOWN)
    {
        CreateUniformBuffer(pDevice, sizeof(HLSL::PBRShaderAttribs), "PBR attribs CB", &m_PBRAttribsCB);
        CreateUniformBuffer(pDevice, static_cast<Uint32>(sizeof(float4x4) * m_Settings.MaxJointCount), "PBR joint transforms", &m_JointsBuffer);

        // clang-format off
        StateTransitionDesc Barriers[] = 
        {
            {m_PBRAttribsCB, RESOURCE_STATE_UNKNOWN, RESOURCE_STATE_CONSTANT_BUFFER, STATE_TRANSITION_FLAG_UPDATE_STATE},
            {m_JointsBuffer,  RESOURCE_STATE_UNKNOWN, RESOURCE_STATE_CONSTANT_BUFFER, STATE_TRANSITION_FLAG_UPDATE_STATE}
        };
        // clang-format on
        pCtx->TransitionResourceStates(_countof(Barriers), Barriers);

        CreatePSO(pDevice, pStateCache);
    }
}

PBR_Renderer::~PBR_Renderer()
{
}

void PBR_Renderer::PrecomputeBRDF(IRenderDevice*     pDevice,
                                  IRenderStateCache* pStateCache,
                                  IDeviceContext*    pCtx,
                                  Uint32             NumBRDFSamples)
{
    RenderDeviceWithCache<false> Device{pDevice, pStateCache};

    TextureDesc TexDesc;
    TexDesc.Name      = "BRDF Look-up texture";
    TexDesc.Type      = RESOURCE_DIM_TEX_2D;
    TexDesc.Usage     = USAGE_DEFAULT;
    TexDesc.BindFlags = BIND_SHADER_RESOURCE | BIND_RENDER_TARGET;
    TexDesc.Width     = BRDF_LUT_Dim;
    TexDesc.Height    = BRDF_LUT_Dim;
    TexDesc.Format    = TEX_FORMAT_RG16_FLOAT;
    TexDesc.MipLevels = 1;
    auto pBRDF_LUT    = Device.CreateTexture(TexDesc);
    m_pBRDF_LUT_SRV   = pBRDF_LUT->GetDefaultView(TEXTURE_VIEW_SHADER_RESOURCE);

    RefCntAutoPtr<IPipelineState> PrecomputeBRDF_PSO;
    {
        GraphicsPipelineStateCreateInfo PSOCreateInfo;
        PipelineStateDesc&              PSODesc          = PSOCreateInfo.PSODesc;
        GraphicsPipelineDesc&           GraphicsPipeline = PSOCreateInfo.GraphicsPipeline;

        PSODesc.Name         = "Precompute BRDF LUT PSO";
        PSODesc.PipelineType = PIPELINE_TYPE_GRAPHICS;

        GraphicsPipeline.NumRenderTargets             = 1;
        GraphicsPipeline.RTVFormats[0]                = TexDesc.Format;
        GraphicsPipeline.PrimitiveTopology            = PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
        GraphicsPipeline.RasterizerDesc.CullMode      = CULL_MODE_NONE;
        GraphicsPipeline.DepthStencilDesc.DepthEnable = False;

        ShaderCreateInfo ShaderCI;
        ShaderCI.SourceLanguage             = SHADER_SOURCE_LANGUAGE_HLSL;
        ShaderCI.pShaderSourceStreamFactory = &DiligentFXShaderSourceStreamFactory::GetInstance();

        ShaderMacroHelper Macros;
        Macros.AddShaderMacro("NUM_SAMPLES", NumBRDFSamples);
        ShaderCI.Macros = Macros;

        RefCntAutoPtr<IShader> pVS;
        {
            ShaderCI.Desc       = {"Full screen triangle VS", SHADER_TYPE_VERTEX, true};
            ShaderCI.EntryPoint = "FullScreenTriangleVS";
            ShaderCI.FilePath   = "FullScreenTriangleVS.fx";

            pVS = Device.CreateShader(ShaderCI);
        }

        // Create pixel shader
        RefCntAutoPtr<IShader> pPS;
        {
            ShaderCI.Desc       = {"Precompute BRDF PS", SHADER_TYPE_PIXEL, true};
            ShaderCI.EntryPoint = "PrecomputeBRDF_PS";
            ShaderCI.FilePath   = "PrecomputeBRDF.psh";

            pPS = Device.CreateShader(ShaderCI);
        }

        // Finally, create the pipeline state
        PSOCreateInfo.pVS  = pVS;
        PSOCreateInfo.pPS  = pPS;
        PrecomputeBRDF_PSO = Device.CreateGraphicsPipelineState(PSOCreateInfo);
    }
    pCtx->SetPipelineState(PrecomputeBRDF_PSO);

    ITextureView* pRTVs[] = {pBRDF_LUT->GetDefaultView(TEXTURE_VIEW_RENDER_TARGET)};
    pCtx->SetRenderTargets(1, pRTVs, nullptr, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
    DrawAttribs attrs(3, DRAW_FLAG_VERIFY_ALL);
    pCtx->Draw(attrs);

    // clang-format off
    StateTransitionDesc Barriers[] =
    {
        {pBRDF_LUT, RESOURCE_STATE_UNKNOWN, RESOURCE_STATE_SHADER_RESOURCE, STATE_TRANSITION_FLAG_UPDATE_STATE}
    };
    // clang-format on
    pCtx->TransitionResourceStates(_countof(Barriers), Barriers);
}

void PBR_Renderer::PrecomputeCubemaps(IRenderDevice*     pDevice,
                                      IRenderStateCache* pStateCache,
                                      IDeviceContext*    pCtx,
                                      ITextureView*      pEnvironmentMap,
                                      Uint32             NumPhiSamples,
                                      Uint32             NumThetaSamples,
                                      bool               OptimizeSamples)
{
    if (!m_Settings.UseIBL)
    {
        LOG_WARNING_MESSAGE("IBL is disabled, so precomputing cube maps will have no effect");
        return;
    }

    RenderDeviceWithCache<false> Device{pDevice, pStateCache};

    struct PrecomputeEnvMapAttribs
    {
        float4x4 Rotation;

        float Roughness;
        float EnvMapDim;
        uint  NumSamples;
        float Dummy;
    };

    if (!m_PrecomputeEnvMapAttribsCB)
    {
        CreateUniformBuffer(pDevice, sizeof(PrecomputeEnvMapAttribs), "Precompute env map attribs CB", &m_PrecomputeEnvMapAttribsCB);
    }

    if (!m_pPrecomputeIrradianceCubePSO)
    {
        ShaderCreateInfo ShaderCI;
        ShaderCI.SourceLanguage             = SHADER_SOURCE_LANGUAGE_HLSL;
        ShaderCI.pShaderSourceStreamFactory = &DiligentFXShaderSourceStreamFactory::GetInstance();

        ShaderMacroHelper Macros;
        Macros.AddShaderMacro("NUM_PHI_SAMPLES", static_cast<int>(NumPhiSamples));
        Macros.AddShaderMacro("NUM_THETA_SAMPLES", static_cast<int>(NumThetaSamples));
        ShaderCI.Macros = Macros;
        RefCntAutoPtr<IShader> pVS;
        {
            ShaderCI.Desc       = {"Cubemap face VS", SHADER_TYPE_VERTEX, true};
            ShaderCI.EntryPoint = "main";
            ShaderCI.FilePath   = "CubemapFace.vsh";

            pVS = Device.CreateShader(ShaderCI);
        }

        // Create pixel shader
        RefCntAutoPtr<IShader> pPS;
        {
            ShaderCI.Desc       = {"Precompute irradiance cube map PS", SHADER_TYPE_PIXEL, true};
            ShaderCI.EntryPoint = "main";
            ShaderCI.FilePath   = "ComputeIrradianceMap.psh";

            pPS = Device.CreateShader(ShaderCI);
        }

        GraphicsPipelineStateCreateInfo PSOCreateInfo;
        PipelineStateDesc&              PSODesc          = PSOCreateInfo.PSODesc;
        GraphicsPipelineDesc&           GraphicsPipeline = PSOCreateInfo.GraphicsPipeline;

        PSODesc.Name         = "Precompute irradiance cube PSO";
        PSODesc.PipelineType = PIPELINE_TYPE_GRAPHICS;

        GraphicsPipeline.NumRenderTargets             = 1;
        GraphicsPipeline.RTVFormats[0]                = IrradianceCubeFmt;
        GraphicsPipeline.PrimitiveTopology            = PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP;
        GraphicsPipeline.RasterizerDesc.CullMode      = CULL_MODE_NONE;
        GraphicsPipeline.DepthStencilDesc.DepthEnable = False;

        PSOCreateInfo.pVS = pVS;
        PSOCreateInfo.pPS = pPS;

        PSODesc.ResourceLayout.DefaultVariableType = SHADER_RESOURCE_VARIABLE_TYPE_STATIC;
        // clang-format off
        ShaderResourceVariableDesc Vars[] = 
        {
            {SHADER_TYPE_PIXEL, "g_EnvironmentMap", SHADER_RESOURCE_VARIABLE_TYPE_DYNAMIC}
        };
        // clang-format on
        PSODesc.ResourceLayout.NumVariables = _countof(Vars);
        PSODesc.ResourceLayout.Variables    = Vars;

        // clang-format off
        ImmutableSamplerDesc ImtblSamplers[] =
        {
            {SHADER_TYPE_PIXEL, "g_EnvironmentMap", Sam_LinearClamp}
        };
        // clang-format on
        PSODesc.ResourceLayout.NumImmutableSamplers = _countof(ImtblSamplers);
        PSODesc.ResourceLayout.ImmutableSamplers    = ImtblSamplers;

        m_pPrecomputeIrradianceCubePSO = Device.CreateGraphicsPipelineState(PSOCreateInfo);
        m_pPrecomputeIrradianceCubePSO->GetStaticVariableByName(SHADER_TYPE_VERTEX, "cbTransform")->Set(m_PrecomputeEnvMapAttribsCB);
        m_pPrecomputeIrradianceCubePSO->CreateShaderResourceBinding(&m_pPrecomputeIrradianceCubeSRB, true);
    }

    if (!m_pPrefilterEnvMapPSO)
    {
        ShaderCreateInfo ShaderCI;
        ShaderCI.SourceLanguage             = SHADER_SOURCE_LANGUAGE_HLSL;
        ShaderCI.pShaderSourceStreamFactory = &DiligentFXShaderSourceStreamFactory::GetInstance();

        ShaderMacroHelper Macros;
        Macros.AddShaderMacro("OPTIMIZE_SAMPLES", OptimizeSamples ? 1 : 0);
        ShaderCI.Macros = Macros;

        RefCntAutoPtr<IShader> pVS;
        {
            ShaderCI.Desc       = {"Cubemap face VS", SHADER_TYPE_VERTEX, true};
            ShaderCI.EntryPoint = "main";
            ShaderCI.FilePath   = "CubemapFace.vsh";

            pVS = Device.CreateShader(ShaderCI);
        }

        // Create pixel shader
        RefCntAutoPtr<IShader> pPS;
        {
            ShaderCI.Desc       = {"Prefilter environment map PS", SHADER_TYPE_PIXEL, true};
            ShaderCI.EntryPoint = "main";
            ShaderCI.FilePath   = "PrefilterEnvMap.psh";

            pPS = Device.CreateShader(ShaderCI);
        }

        GraphicsPipelineStateCreateInfo PSOCreateInfo;
        PipelineStateDesc&              PSODesc          = PSOCreateInfo.PSODesc;
        GraphicsPipelineDesc&           GraphicsPipeline = PSOCreateInfo.GraphicsPipeline;

        PSODesc.Name         = "Prefilter environment map PSO";
        PSODesc.PipelineType = PIPELINE_TYPE_GRAPHICS;

        GraphicsPipeline.NumRenderTargets             = 1;
        GraphicsPipeline.RTVFormats[0]                = PrefilteredEnvMapFmt;
        GraphicsPipeline.PrimitiveTopology            = PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP;
        GraphicsPipeline.RasterizerDesc.CullMode      = CULL_MODE_NONE;
        GraphicsPipeline.DepthStencilDesc.DepthEnable = False;

        PSOCreateInfo.pVS = pVS;
        PSOCreateInfo.pPS = pPS;

        PSODesc.ResourceLayout.DefaultVariableType = SHADER_RESOURCE_VARIABLE_TYPE_STATIC;
        // clang-format off
        ShaderResourceVariableDesc Vars[] = 
        {
            {SHADER_TYPE_PIXEL, "g_EnvironmentMap", SHADER_RESOURCE_VARIABLE_TYPE_DYNAMIC}
        };
        // clang-format on
        PSODesc.ResourceLayout.NumVariables = _countof(Vars);
        PSODesc.ResourceLayout.Variables    = Vars;

        // clang-format off
        ImmutableSamplerDesc ImtblSamplers[] =
        {
            {SHADER_TYPE_PIXEL, "g_EnvironmentMap", Sam_LinearClamp}
        };
        // clang-format on
        PSODesc.ResourceLayout.NumImmutableSamplers = _countof(ImtblSamplers);
        PSODesc.ResourceLayout.ImmutableSamplers    = ImtblSamplers;

        m_pPrefilterEnvMapPSO = Device.CreateGraphicsPipelineState(PSOCreateInfo);
        m_pPrefilterEnvMapPSO->GetStaticVariableByName(SHADER_TYPE_VERTEX, "cbTransform")->Set(m_PrecomputeEnvMapAttribsCB);
        m_pPrefilterEnvMapPSO->GetStaticVariableByName(SHADER_TYPE_PIXEL, "FilterAttribs")->Set(m_PrecomputeEnvMapAttribsCB);
        m_pPrefilterEnvMapPSO->CreateShaderResourceBinding(&m_pPrefilterEnvMapSRB, true);
    }


    // clang-format off
    const std::array<float4x4, 6> Matrices =
    {
/* +X */ float4x4::RotationY(+PI_F / 2.f),
/* -X */ float4x4::RotationY(-PI_F / 2.f),
/* +Y */ float4x4::RotationX(-PI_F / 2.f),
/* -Y */ float4x4::RotationX(+PI_F / 2.f),
/* +Z */ float4x4::Identity(),
/* -Z */ float4x4::RotationY(PI_F)
    };
    // clang-format on

    pCtx->SetPipelineState(m_pPrecomputeIrradianceCubePSO);
    m_pPrecomputeIrradianceCubeSRB->GetVariableByName(SHADER_TYPE_PIXEL, "g_EnvironmentMap")->Set(pEnvironmentMap);
    pCtx->CommitShaderResources(m_pPrecomputeIrradianceCubeSRB, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
    auto*       pIrradianceCube    = m_pIrradianceCubeSRV->GetTexture();
    const auto& IrradianceCubeDesc = pIrradianceCube->GetDesc();
    for (Uint32 mip = 0; mip < IrradianceCubeDesc.MipLevels; ++mip)
    {
        for (Uint32 face = 0; face < 6; ++face)
        {
            TextureViewDesc RTVDesc{"RTV for irradiance cube texture", TEXTURE_VIEW_RENDER_TARGET, RESOURCE_DIM_TEX_2D_ARRAY};
            RTVDesc.MostDetailedMip = mip;
            RTVDesc.FirstArraySlice = face;
            RTVDesc.NumArraySlices  = 1;
            RefCntAutoPtr<ITextureView> pRTV;
            pIrradianceCube->CreateView(RTVDesc, &pRTV);
            ITextureView* ppRTVs[] = {pRTV};
            pCtx->SetRenderTargets(_countof(ppRTVs), ppRTVs, nullptr, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
            {
                MapHelper<PrecomputeEnvMapAttribs> Attribs{pCtx, m_PrecomputeEnvMapAttribsCB, MAP_WRITE, MAP_FLAG_DISCARD};
                Attribs->Rotation = Matrices[face];
            }
            DrawAttribs drawAttrs(4, DRAW_FLAG_VERIFY_ALL);
            pCtx->Draw(drawAttrs);
        }
    }

    pCtx->SetPipelineState(m_pPrefilterEnvMapPSO);
    m_pPrefilterEnvMapSRB->GetVariableByName(SHADER_TYPE_PIXEL, "g_EnvironmentMap")->Set(pEnvironmentMap);
    pCtx->CommitShaderResources(m_pPrefilterEnvMapSRB, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
    auto*       pPrefilteredEnvMap    = m_pPrefilteredEnvMapSRV->GetTexture();
    const auto& PrefilteredEnvMapDesc = pPrefilteredEnvMap->GetDesc();
    for (Uint32 mip = 0; mip < PrefilteredEnvMapDesc.MipLevels; ++mip)
    {
        for (Uint32 face = 0; face < 6; ++face)
        {
            TextureViewDesc RTVDesc{"RTV for prefiltered env map cube texture", TEXTURE_VIEW_RENDER_TARGET, RESOURCE_DIM_TEX_2D_ARRAY};
            RTVDesc.MostDetailedMip = mip;
            RTVDesc.FirstArraySlice = face;
            RTVDesc.NumArraySlices  = 1;
            RefCntAutoPtr<ITextureView> pRTV;
            pPrefilteredEnvMap->CreateView(RTVDesc, &pRTV);
            ITextureView* ppRTVs[] = {pRTV};
            pCtx->SetRenderTargets(_countof(ppRTVs), ppRTVs, nullptr, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);

            {
                MapHelper<PrecomputeEnvMapAttribs> Attribs{pCtx, m_PrecomputeEnvMapAttribsCB, MAP_WRITE, MAP_FLAG_DISCARD};
                Attribs->Rotation   = Matrices[face];
                Attribs->Roughness  = static_cast<float>(mip) / static_cast<float>(PrefilteredEnvMapDesc.MipLevels);
                Attribs->EnvMapDim  = static_cast<float>(PrefilteredEnvMapDesc.Width);
                Attribs->NumSamples = 256;
            }

            DrawAttribs drawAttrs(4, DRAW_FLAG_VERIFY_ALL);
            pCtx->Draw(drawAttrs);
        }
    }

    // clang-format off
    StateTransitionDesc Barriers[] = 
    {
        {m_pPrefilteredEnvMapSRV->GetTexture(), RESOURCE_STATE_UNKNOWN, RESOURCE_STATE_SHADER_RESOURCE, STATE_TRANSITION_FLAG_UPDATE_STATE},
        {m_pIrradianceCubeSRV->GetTexture(),    RESOURCE_STATE_UNKNOWN, RESOURCE_STATE_SHADER_RESOURCE, STATE_TRANSITION_FLAG_UPDATE_STATE}
    };
    // clang-format on
    pCtx->TransitionResourceStates(_countof(Barriers), Barriers);

    // To avoid crashes on some low-end Android devices
    pCtx->Flush();
}


void PBR_Renderer::InitCommonSRBVars(IShaderResourceBinding* pSRB,
                                     IBuffer*                pCameraAttribs,
                                     IBuffer*                pLightAttribs)
{
    VERIFY_EXPR(pSRB != nullptr);

    if (pCameraAttribs != nullptr)
    {
        if (auto* pCameraAttribsVSVar = pSRB->GetVariableByName(SHADER_TYPE_VERTEX, "cbCameraAttribs"))
            pCameraAttribsVSVar->Set(pCameraAttribs);

        if (auto* pCameraAttribsPSVar = pSRB->GetVariableByName(SHADER_TYPE_PIXEL, "cbCameraAttribs"))
            pCameraAttribsPSVar->Set(pCameraAttribs);
    }

    if (pLightAttribs != nullptr)
    {
        if (auto* pLightAttribsPSVar = pSRB->GetVariableByName(SHADER_TYPE_PIXEL, "cbLightAttribs"))
            pLightAttribsPSVar->Set(pLightAttribs);
    }

    if (m_Settings.UseIBL)
    {
        if (auto* pIrradianceMapPSVar = pSRB->GetVariableByName(SHADER_TYPE_PIXEL, "g_IrradianceMap"))
            pIrradianceMapPSVar->Set(m_pIrradianceCubeSRV);

        if (auto* pPrefilteredEnvMap = pSRB->GetVariableByName(SHADER_TYPE_PIXEL, "g_PrefilteredEnvMap"))
            pPrefilteredEnvMap->Set(m_pPrefilteredEnvMapSRV);
    }
}


void PBR_Renderer::CreatePSO(IRenderDevice* pDevice, IRenderStateCache* pStateCache)
{
    RenderDeviceWithCache<false> Device{pDevice, pStateCache};

    GraphicsPipelineStateCreateInfo PSOCreateInfo;
    PipelineStateDesc&              PSODesc          = PSOCreateInfo.PSODesc;
    GraphicsPipelineDesc&           GraphicsPipeline = PSOCreateInfo.GraphicsPipeline;

    PSODesc.Name         = "Render PBR PSO";
    PSODesc.PipelineType = PIPELINE_TYPE_GRAPHICS;

    GraphicsPipeline.NumRenderTargets                     = 1;
    GraphicsPipeline.RTVFormats[0]                        = m_Settings.RTVFmt;
    GraphicsPipeline.DSVFormat                            = m_Settings.DSVFmt;
    GraphicsPipeline.PrimitiveTopology                    = PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    GraphicsPipeline.RasterizerDesc.CullMode              = CULL_MODE_BACK;
    GraphicsPipeline.RasterizerDesc.FrontCounterClockwise = m_Settings.FrontCCW;

    ShaderCreateInfo ShaderCI;
    ShaderCI.SourceLanguage             = SHADER_SOURCE_LANGUAGE_HLSL;
    ShaderCI.pShaderSourceStreamFactory = &DiligentFXShaderSourceStreamFactory::GetInstance();

    ShaderMacroHelper Macros;
    Macros.AddShaderMacro("MAX_JOINT_COUNT", m_Settings.MaxJointCount);
    Macros.AddShaderMacro("ALLOW_DEBUG_VIEW", m_Settings.AllowDebugView);
    Macros.AddShaderMacro("TONE_MAPPING_MODE", "TONE_MAPPING_MODE_UNCHARTED2");
    Macros.AddShaderMacro("PBR_USE_IBL", m_Settings.UseIBL);
    Macros.AddShaderMacro("PBR_USE_AO", m_Settings.UseAO);
    Macros.AddShaderMacro("PBR_USE_EMISSIVE", m_Settings.UseEmissive);
    Macros.AddShaderMacro("USE_TEXTURE_ATLAS", m_Settings.UseTextureAtlas);
    Macros.AddShaderMacro("PBR_WORKFLOW_METALLIC_ROUGHNESS", PBR_WORKFLOW_METALL_ROUGH);
    Macros.AddShaderMacro("PBR_WORKFLOW_SPECULAR_GLOSINESS", PBR_WORKFLOW_SPEC_GLOSS);
    Macros.AddShaderMacro("PBR_ALPHA_MODE_OPAQUE", ALPHA_MODE_OPAQUE);
    Macros.AddShaderMacro("PBR_ALPHA_MODE_MASK", ALPHA_MODE_MASK);
    Macros.AddShaderMacro("PBR_ALPHA_MODE_BLEND", ALPHA_MODE_BLEND);
    Macros.AddShaderMacro("USE_IBL_ENV_MAP_LOD", true);
    Macros.AddShaderMacro("USE_HDR_IBL_CUBEMAPS", true);
    ShaderCI.Macros = Macros;
    RefCntAutoPtr<IShader> pVS;
    {
        ShaderCI.Desc       = {"PBR VS", SHADER_TYPE_VERTEX, true};
        ShaderCI.EntryPoint = "main";
        ShaderCI.FilePath   = "RenderPBR.vsh";

        pVS = Device.CreateShader(ShaderCI);
    }

    // Create pixel shader
    RefCntAutoPtr<IShader> pPS;
    {
        ShaderCI.Desc       = {"PBR PS", SHADER_TYPE_PIXEL, true};
        ShaderCI.EntryPoint = "main";
        ShaderCI.FilePath   = "RenderPBR.psh";

        pPS = Device.CreateShader(ShaderCI);
    }

    // clang-format off
    LayoutElement Inputs[] =
    {
        {0, 0, 3, VT_FLOAT32},   //float3 Pos     : ATTRIB0;
        {1, 0, 3, VT_FLOAT32},   //float3 Normal  : ATTRIB1;
        {2, 0, 2, VT_FLOAT32},   //float2 UV0     : ATTRIB2;
        {3, 0, 2, VT_FLOAT32},   //float2 UV1     : ATTRIB3;
        {4, 1, 4, VT_FLOAT32},   //float4 Joint0  : ATTRIB4;
        {5, 1, 4, VT_FLOAT32}    //float4 Weight0 : ATTRIB5;
    };
    // clang-format on
    PSOCreateInfo.GraphicsPipeline.InputLayout.LayoutElements = Inputs;
    PSOCreateInfo.GraphicsPipeline.InputLayout.NumElements    = _countof(Inputs);

    PSODesc.ResourceLayout.DefaultVariableType = SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE;
    // clang-format off
    std::vector<ShaderResourceVariableDesc> Vars = 
    {
        {SHADER_TYPE_VS_PS,  "cbPBRAttribs",      SHADER_RESOURCE_VARIABLE_TYPE_STATIC},
        {SHADER_TYPE_VERTEX, "cbJointTransforms", SHADER_RESOURCE_VARIABLE_TYPE_STATIC}
    };
    // clang-format on

    std::vector<ImmutableSamplerDesc> ImtblSamplers;
    // clang-format off
    if (m_Settings.UseImmutableSamplers)
    {
        ImtblSamplers.emplace_back(SHADER_TYPE_PIXEL, "g_ColorMap",              m_Settings.ColorMapImmutableSampler);
        ImtblSamplers.emplace_back(SHADER_TYPE_PIXEL, "g_PhysicalDescriptorMap", m_Settings.PhysDescMapImmutableSampler);
        ImtblSamplers.emplace_back(SHADER_TYPE_PIXEL, "g_NormalMap",             m_Settings.NormalMapImmutableSampler);
    }
    // clang-format on

    if (m_Settings.UseAO)
    {
        ImtblSamplers.emplace_back(SHADER_TYPE_PIXEL, "g_AOMap", m_Settings.AOMapImmutableSampler);
    }

    if (m_Settings.UseEmissive)
    {
        ImtblSamplers.emplace_back(SHADER_TYPE_PIXEL, "g_EmissiveMap", m_Settings.EmissiveMapImmutableSampler);
    }

    if (m_Settings.UseIBL)
    {
        Vars.emplace_back(SHADER_TYPE_PIXEL, "g_BRDF_LUT", SHADER_RESOURCE_VARIABLE_TYPE_STATIC);

        // clang-format off
        ImtblSamplers.emplace_back(SHADER_TYPE_PIXEL, "g_BRDF_LUT",          Sam_LinearClamp);
        ImtblSamplers.emplace_back(SHADER_TYPE_PIXEL, "g_IrradianceMap",     Sam_LinearClamp);
        ImtblSamplers.emplace_back(SHADER_TYPE_PIXEL, "g_PrefilteredEnvMap", Sam_LinearClamp);
        // clang-format on
    }

    PSODesc.ResourceLayout.NumVariables         = static_cast<Uint32>(Vars.size());
    PSODesc.ResourceLayout.Variables            = Vars.data();
    PSODesc.ResourceLayout.NumImmutableSamplers = static_cast<Uint32>(ImtblSamplers.size());
    PSODesc.ResourceLayout.ImmutableSamplers    = !ImtblSamplers.empty() ? ImtblSamplers.data() : nullptr;

    PSOCreateInfo.pVS = pVS;
    PSOCreateInfo.pPS = pPS;

    {
        PSOKey Key{ALPHA_MODE_OPAQUE, false};

        auto pSingleSidedOpaquePSO = Device.CreateGraphicsPipelineState(PSOCreateInfo);
        AddPSO(Key, std::move(pSingleSidedOpaquePSO));

        PSOCreateInfo.GraphicsPipeline.RasterizerDesc.CullMode = CULL_MODE_NONE;

        Key.DoubleSided = true;

        auto pDobleSidedOpaquePSO = Device.CreateGraphicsPipelineState(PSOCreateInfo);
        AddPSO(Key, std::move(pDobleSidedOpaquePSO));
    }

    PSOCreateInfo.GraphicsPipeline.RasterizerDesc.CullMode = CULL_MODE_BACK;

    auto& RT0          = PSOCreateInfo.GraphicsPipeline.BlendDesc.RenderTargets[0];
    RT0.BlendEnable    = true;
    RT0.SrcBlend       = BLEND_FACTOR_SRC_ALPHA;
    RT0.DestBlend      = BLEND_FACTOR_INV_SRC_ALPHA;
    RT0.BlendOp        = BLEND_OPERATION_ADD;
    RT0.SrcBlendAlpha  = BLEND_FACTOR_INV_SRC_ALPHA;
    RT0.DestBlendAlpha = BLEND_FACTOR_ZERO;
    RT0.BlendOpAlpha   = BLEND_OPERATION_ADD;

    {
        PSOKey Key{ALPHA_MODE_BLEND, false};

        auto pSingleSidedBlendPSO = Device.CreateGraphicsPipelineState(PSOCreateInfo);
        AddPSO(Key, std::move(pSingleSidedBlendPSO));

        PSOCreateInfo.GraphicsPipeline.RasterizerDesc.CullMode = CULL_MODE_NONE;

        Key.DoubleSided = true;

        auto pDoubleSidedBlendPSO = Device.CreateGraphicsPipelineState(PSOCreateInfo);
        AddPSO(Key, std::move(pDoubleSidedBlendPSO));
    }

    for (auto& PSO : m_PSOCache)
    {
        if (m_Settings.UseIBL)
        {
            PSO->GetStaticVariableByName(SHADER_TYPE_PIXEL, "g_BRDF_LUT")->Set(m_pBRDF_LUT_SRV);
        }
        // clang-format off
        PSO->GetStaticVariableByName(SHADER_TYPE_PIXEL, "cbPBRAttribs")->Set(m_PBRAttribsCB);
        PSO->GetStaticVariableByName(SHADER_TYPE_VERTEX, "cbJointTransforms")->Set(m_JointsBuffer);
        // clang-format on
    }
}

void PBR_Renderer::AddPSO(const PSOKey& Key, RefCntAutoPtr<IPipelineState> pPSO)
{
    auto Idx = GetPSOIdx(Key);
    if (Idx >= m_PSOCache.size())
        m_PSOCache.resize(Idx + 1);
    VERIFY_EXPR(!m_PSOCache[Idx]);
    m_PSOCache[Idx] = std::move(pPSO);
}

} // namespace Diligent
