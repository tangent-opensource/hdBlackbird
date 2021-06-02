//  Copyright 2020 Tangent Animation
//
//  Licensed under the Apache License, Version 2.0 (the "License");
//  you may not use this file except in compliance with the License.
//  You may obtain a copy of the License at
//
//      http://www.apache.org/licenses/LICENSE-2.0
//
//  Unless required by applicable law or agreed to in writing, software
//  distributed under the License is distributed on an "AS IS" BASIS,
//  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied,
//  including without limitation, as related to merchantability and fitness
//  for a particular purpose.
//
//  In no event shall any copyright holder be liable for any damages of any kind
//  arising from the use of this software, whether in contract, tort or otherwise.
//  See the License for the specific language governing permissions and
//  limitations under the License.

#include "renderPass.h"

#include "camera.h"
#include "renderBuffer.h"
#include "renderParam.h"
#include "utils.h"

#include <render/camera.h>
#include <render/scene.h>
#include <render/session.h>
#include <util/util_types.h>

#include <pxr/base/tf/staticTokens.h>
#include <pxr/imaging/hd/camera.h>
#include <pxr/imaging/hd/renderPassState.h>

PXR_NAMESPACE_OPEN_SCOPE

// clang-format off
#ifdef __GNUC__
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-variable"
#endif
TF_DEFINE_PRIVATE_TOKENS(_tokens, 
    (color)
    (depth)
);
#ifdef __GNUC__
#pragma GCC diagnostic pop
#endif
// clang-format on

HdCyclesRenderPass::HdCyclesRenderPass(HdCyclesRenderDelegate* delegate, HdRenderIndex* index,
                                       HdRprimCollection const& collection)
    : HdRenderPass(index, collection)
    , m_delegate(delegate)
{
}

HdCyclesRenderPass::~HdCyclesRenderPass() {}

void
HdCyclesRenderPass::_Execute(HdRenderPassStateSharedPtr const& renderPassState, TfTokenVector const& renderTags)
{
    auto* renderParam = reinterpret_cast<HdCyclesRenderParam*>(m_delegate->GetRenderParam());

    // Update convergence status. Cycles will stop blitting once rendering has finished,
    // but this is needed to let Hydra and the viewport know.
    m_isConverged = renderParam->IsConverged();

    // Update the Cycles render passes with the new aov bindings if they have changed
    // Do not reset the session yet
    HdRenderPassAovBindingVector aovBindings = renderPassState->GetAovBindings();
    const bool aovBindingsHaveChanged = renderParam->GetAovBindings() != aovBindings;
    if (aovBindingsHaveChanged) {
        renderParam->SetAovBindings(aovBindings);
        }

    // TODO: Revisit this code and move it to HdCyclesRenderPassState
    bool shouldUpdate = false;
    auto hdCam = const_cast<HdCyclesCamera*>(dynamic_cast<HdCyclesCamera const*>(renderPassState->GetCamera()));
    if (hdCam) {
    GfMatrix4d projMtx = renderPassState->GetProjectionMatrix();
    GfMatrix4d viewMtx = renderPassState->GetWorldToViewMatrix();

    ccl::Camera* active_camera = renderParam->GetCyclesSession()->scene->camera;

    if (projMtx != m_projMtx || viewMtx != m_viewMtx) {
        m_projMtx = projMtx;
        m_viewMtx = viewMtx;

        const float fov_rad = atanf(1.0f / static_cast<float>(m_projMtx[1][1])) * 2.0f;
        hdCam->SetFOV(fov_rad);

        shouldUpdate = true;
    }

        if (!shouldUpdate) {
        shouldUpdate = hdCam->IsDirty();
        }

    if (shouldUpdate) {
        hdCam->ApplyCameraSettings(active_camera);

        // Needed for now, as houdini looks through a generated camera
        // and doesn't copy the projection type (as of 18.0.532)
        bool is_ortho = round(m_projMtx[3][3]) == 1.0;

        if (is_ortho) {
            active_camera->set_camera_type(ccl::CameraType::CAMERA_ORTHOGRAPHIC);
        } else
            active_camera->set_camera_type(ccl::CameraType::CAMERA_PERSPECTIVE);

        // DirectReset here instead of Interrupt for faster IPR camera orbits
        renderParam->DirectReset();
    }
    }

    // Resetting the Cycles session if the viewport size or AOV bindings changed
    const GfVec4f& viewport = renderPassState->GetViewport();
    const auto width = static_cast<int>(viewport[2]);
    const auto height = static_cast<int>(viewport[3]);

    if (width != m_width || height != m_height) {
        m_width  = width;
        m_height = height;

        // TODO: Due to the startup flow of Cycles, this gets called after a tiled render
        // has already started. Sometimes causing the original tiled render to complete
        // before actually rendering at the appropriate size. This seems to be a Cycles
        // issue, however the startup flow of HdCycles has LOTS of room for improvement...
        renderParam->SetViewport(m_width, m_height);

        // TODO: This is very hacky... But stops the tiled render double render issue...
        if (renderParam->IsTiledRender()) {
            renderParam->StartRender();
        }

        renderParam->Interrupt();
    } else if (aovBindingsHaveChanged) {
        renderParam->DirectReset();
        renderParam->Interrupt();
    }
}

PXR_NAMESPACE_CLOSE_SCOPE
