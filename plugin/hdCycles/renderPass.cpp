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
TF_DEFINE_PRIVATE_TOKENS(_tokens, 
    (color)
    (depth)
);
// clang-format on

HdCyclesRenderPass::HdCyclesRenderPass(HdCyclesRenderDelegate* delegate,
                                       HdRenderIndex* index,
                                       HdRprimCollection const& collection)
    : HdRenderPass(index, collection)
    , m_delegate(delegate)
{
}

HdCyclesRenderPass::~HdCyclesRenderPass() {}

void
HdCyclesRenderPass::_Execute(HdRenderPassStateSharedPtr const& renderPassState,
                             TfTokenVector const& renderTags)
{
    auto* renderParam = reinterpret_cast<HdCyclesRenderParam*>(
        m_delegate->GetRenderParam());

    HdRenderPassAovBindingVector aovBindings = renderPassState->GetAovBindings();

    if (renderParam->GetAovBindings() != aovBindings)
        renderParam->SetAovBindings(aovBindings);

    const auto vp = renderPassState->GetViewport();

    GfMatrix4d projMtx = renderPassState->GetProjectionMatrix();
    GfMatrix4d viewMtx = renderPassState->GetWorldToViewMatrix();

    m_isConverged = renderParam->IsConverged();

    // XXX: Need to cast away constness to process updated camera params since
    // the Hydra camera doesn't update the Cycles camera directly.
    HdCyclesCamera* hdCam = const_cast<HdCyclesCamera*>(
        dynamic_cast<HdCyclesCamera const*>(renderPassState->GetCamera()));

    ccl::Camera* active_camera = renderParam->GetCyclesSession()->scene->camera;

    bool shouldUpdate = false;

    if (projMtx != m_projMtx || viewMtx != m_viewMtx) {
        m_projMtx = projMtx;
        m_viewMtx = viewMtx;

        const float fov_rad = atan(1.0f / m_projMtx[1][1]) * 2.0f;
        const float fov_deg = fov_rad / M_PI * 180.0f;
        hdCam->SetFOV(fov_rad);

        shouldUpdate = true;
    }

    if(!shouldUpdate)
        shouldUpdate = hdCam->IsDirty();

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

    const auto width     = static_cast<int>(vp[2]);
    const auto height    = static_cast<int>(vp[3]);
    const auto numPixels = static_cast<size_t>(width * height);

    bool resized = false;

    if (width != m_width || height != m_height) {
        const auto oldNumPixels = static_cast<size_t>(m_width * m_height);
        m_width                 = width;
        m_height                = height;

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

        if (numPixels != oldNumPixels) {
            resized = true;
        }
    }

    // Tiled renders early out because we do the blitting on render tile callback
    if (renderParam->IsTiledRender())
        return;

    if (!renderParam->GetCyclesSession())
        return;

    if (!renderParam->GetCyclesScene())
        return;

    ccl::DisplayBuffer* display = renderParam->GetCyclesSession()->display;

    if (!display)
        return;

    HdFormat colorFormat = display->half_float ? HdFormatFloat16Vec4
                                               : HdFormatUNorm8Vec4;

    unsigned char* hpixels
        = (display->half_float)
              ? (unsigned char*)display->rgba_half.host_pointer
              : (unsigned char*)display->rgba_byte.host_pointer;

    if (!hpixels)
        return;

    int w = display->draw_width;
    int h = display->draw_height;

    if (w == 0 || h == 0)
        return;

    // Blit
    if (!aovBindings.empty()) {
        // Blit from the framebuffer to currently selected aovs...
        for (auto& aov : aovBindings) {
            if (!TF_VERIFY(aov.renderBuffer != nullptr)) {
                continue;
            }

            auto* rb = static_cast<HdCyclesRenderBuffer*>(aov.renderBuffer);
            rb->SetConverged(m_isConverged);

            // Needed as a stopgap, because Houdini dellocates renderBuffers
            // when changing render settings. This causes the current blit to
            // fail (Probably can be fixed with proper render thread management)
            if (!rb->WasUpdated()) {
                if (aov.aovName == HdAovTokens->color) {
                    rb->Blit(colorFormat, w, h, 0, w,
                             reinterpret_cast<uint8_t*>(hpixels));
                }
            } else {
                rb->SetWasUpdated(false);
            }
        }
    }
}

PXR_NAMESPACE_CLOSE_SCOPE
