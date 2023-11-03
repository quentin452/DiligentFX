/*
 *  Copyright 2023 Diligent Graphics LLC
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

#pragma once

#include "HnTask.hpp"
#include "HnTypes.hpp"

#include "../../../Common/interface/BasicMath.hpp"

#include "pxr/imaging/hd/renderPass.h"

namespace Diligent
{

namespace USD
{

struct HnRenderRprimsTaskParams
{
    HN_RENDER_MODE RenderMode = HN_RENDER_MODE_SOLID;

    int   DebugView         = 0;
    float OcclusionStrength = 1;
    float EmissionScale     = 1;
    float IBLScale          = 1;

    float4 WireframeColor = float4(1, 1, 1, 1);
    float4 PointColor     = float4(1, 1, 1, 1);

    float4x4 Transform = float4x4::Identity();

    /// Selected prim id.
    /// Selected rprim is rendered with negative index.
    pxr::SdfPath SelectedPrimId;

    constexpr bool operator==(const HnRenderRprimsTaskParams& rhs) const
    {
        // clang-format off
        return RenderMode        == rhs.RenderMode &&
               DebugView         == rhs.DebugView &&
               OcclusionStrength == rhs.OcclusionStrength &&
               EmissionScale     == rhs.EmissionScale &&
               IBLScale          == rhs.IBLScale &&
               WireframeColor    == rhs.WireframeColor &&
               Transform         == rhs.Transform &&
               SelectedPrimId    == rhs.SelectedPrimId;
        // clang-format on
    }

    constexpr bool operator!=(const HnRenderRprimsTaskParams& rhs) const
    {
        return !(*this == rhs);
    }
};

/// Renders the Rprims by executing the render pass.
/// The task should be executed after the HnSetupRenderingTask that prepares
/// the render targets and sets the required task context and render pass state
/// parameters.
class HnRenderRprimsTask final : public HnTask
{
public:
    HnRenderRprimsTask(pxr::HdSceneDelegate* ParamsDelegate, const pxr::SdfPath& Id);
    ~HnRenderRprimsTask();

    virtual void Sync(pxr::HdSceneDelegate* Delegate,
                      pxr::HdTaskContext*   TaskCtx,
                      pxr::HdDirtyBits*     DirtyBits) override final;

    virtual void Prepare(pxr::HdTaskContext* TaskCtx,
                         pxr::HdRenderIndex* RenderIndex) override final;


    virtual void Execute(pxr::HdTaskContext* TaskCtx) override final;

    virtual const pxr::TfTokenVector& GetRenderTags() const override final
    {
        return m_RenderTags;
    }

private:
    void UpdateRenderPassParams(const HnRenderRprimsTaskParams& Params);

private:
    pxr::TfTokenVector         m_RenderTags;
    pxr::HdRenderPassSharedPtr m_RenderPass;
};

} // namespace USD

} // namespace Diligent
