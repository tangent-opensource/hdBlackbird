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

HdCyclesRenderPass::~HdCyclesRenderPass() { m_colorBuffer.clear(); }

void
HdCyclesRenderPass::_Execute(HdRenderPassStateSharedPtr const& renderPassState,
                             TfTokenVector const& renderTags)
{
    auto* renderParam = reinterpret_cast<HdCyclesRenderParam*>(
        m_delegate->GetRenderParam());

    const auto vp = renderPassState->GetViewport();

    GfMatrix4d projMtx = renderPassState->GetProjectionMatrix();
    GfMatrix4d viewMtx = renderPassState->GetWorldToViewMatrix();

    // XXX: Need to cast away constness to process updated camera params since
    // the Hydra camera doesn't update the Cycles camera directly.
    HdCyclesCamera* hdCam = const_cast<HdCyclesCamera*>(
        dynamic_cast<HdCyclesCamera const*>(renderPassState->GetCamera()));

    if (projMtx != m_projMtx || viewMtx != m_viewMtx) {
        m_projMtx = projMtx;
        m_viewMtx = viewMtx;

        const float fov_rad = atan(1.0f / m_projMtx[1][1]) * 2.0f;
        const float fov_deg = fov_rad / M_PI * 180.0f;
        hdCam->SetFOV(fov_rad);

        //hdCam->SetTransform(m_projMtx);

        bool shouldUpdate = hdCam->ApplyCameraSettings(
            renderParam->GetCyclesSession()->scene->camera);

        if (shouldUpdate)
            renderParam->Interrupt();
        else
            renderParam->DirectReset();
    }

    ccl::DisplayBuffer* display = renderParam->GetCyclesSession()->display;

    int w = display->draw_width;
    int h = display->draw_height;

    const auto width     = static_cast<int>(vp[2]);
    const auto height    = static_cast<int>(vp[3]);
    const auto numPixels = static_cast<size_t>(width * height);

    unsigned int pixelSize = (display->half_float) ? sizeof(CyRGBA16)
                                                   : sizeof(CyRGBA8);

    HdFormat colorFormat = display->half_float ? HdFormatFloat16Vec4
                                               : HdFormatUNorm8Vec4;

    unsigned char* hpixels
        = (display->half_float)
              ? (unsigned char*)display->rgba_half.host_pointer
              : (unsigned char*)display->rgba_byte.host_pointer;

    if (width != m_width || height != m_height) {
        renderParam->Interrupt();
        const auto oldNumPixels = static_cast<size_t>(m_width * m_height);
        m_width                 = width;
        m_height                = height;
        renderParam->CyclesReset(m_width, m_height);

        if (numPixels != oldNumPixels) {
            m_colorBuffer.resize(numPixels * pixelSize);
            memset(m_colorBuffer.data(), 0, numPixels * pixelSize);

            m_cryptoVec.resize(numPixels);
            m_cryptoInt.resize(numPixels);
        }
    }

    m_isConverged = renderParam->GetProgress() >= 1.0f;

    HdRenderPassAovBindingVector aovBindings = renderPassState->GetAovBindings();

    if (w != 0) {
        m_colorBuffer.resize(w * h * pixelSize);
        memcpy(m_colorBuffer.data(), hpixels, w * h * pixelSize);
    }

    // Blit
    if (!aovBindings.empty()) {
        // Blit from the framebuffer to currently selected aovs...
        for (auto& aov : aovBindings) {
            if (!TF_VERIFY(aov.renderBuffer != nullptr)) {
                continue;
            }

            auto* rb = static_cast<HdCyclesRenderBuffer*>(aov.renderBuffer);
            rb->SetConverged(m_isConverged);

            if (aov.aovName == HdAovTokens->color) {
                rb->Blit(colorFormat, w, h, 0, w,
                         reinterpret_cast<uint8_t*>(m_colorBuffer.data()));
            } else {
                // Check to see if this aov corresponds to a cryptomatte token
                std::pair<TfToken, const char*> cryptoMatteTypes[] = {
                    { HdAovTokens->primId, "asset" },
                    { HdAovTokens->instanceId, "object" },

                    { HdAovTokens->elementId, "material" },
                };

                for (const auto& cryptomatteType : cryptoMatteTypes) {
                    const auto& token = cryptomatteType.first;
                    if (aov.aovName != token) {
                        continue;
                    }

                    renderParam->GetCyclesSession()->buffers->copy_from_device();
                    if (renderParam->GetCyclesSession()->buffers->get_pass_rect(
                            cryptomatteType.second, 1, 1, 4,
                            &(*m_cryptoVec.data())[0])) {
                        auto iter = m_cryptoInt.begin();
                        for (const auto& vec : m_cryptoVec) {
                            if (vec[0] == 0) {
                                *iter = -1;
                            } else {
                                if (token == HdAovTokens->primId) {
                                    *iter = renderParam->CryptoAssetToId(
                                        vec[0]);
                                } else if (token == HdAovTokens->instanceId) {
                                    *iter = renderParam->CryptoObjectToId(
                                        vec[0]);
                                } else if (token == HdAovTokens->elementId) {
                                    *iter = renderParam->CryptoMaterialToId(
                                        vec[0]);
                                }
                            }

                            ++iter;
                        }
                        rb->Blit(HdFormatInt32, w, h, 0, w,
                                 (const uint8_t*)m_cryptoInt.data());
                        break;
                    }
                }
            }
        }
    }
}

PXR_NAMESPACE_CLOSE_SCOPE
