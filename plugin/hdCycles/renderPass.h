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
//  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
//  See the License for the specific language governing permissions and
//  limitations under the License.

#ifndef HD_CYCLES_RENDER_PASS_H
#define HD_CYCLES_RENDER_PASS_H

#include "api.h"

#include "renderDelegate.h"

#include <render/buffers.h>

#include <pxr/base/gf/matrix4d.h>
#include <pxr/imaging/hd/renderPass.h>
#include <pxr/pxr.h>

PXR_NAMESPACE_OPEN_SCOPE

typedef struct {
    unsigned char red;
    unsigned char green;
    unsigned char blue;
    unsigned char alpha;
} CyRGBA8;

typedef struct {
    ccl::half red;
    ccl::half green;
    ccl::half blue;
    ccl::half alpha;
} CyRGBA16;

class HdCyclesRenderDelegate;

/**
 * @brief Represents a single render iteration. rendering a view of the scene
 * (HdRprimCollection) for a specific viewer (camera/viewport params in 
 * HdRenderPassState) to the current draw target.
 * 
 */
class HdCyclesRenderPass final : public HdRenderPass {
public:
    /**
     * @brief Construct a new HdCycles Render Pass object
     * 
     * @param delegate 
     * @param index The render index containing scene data to render
     * @param collection Initial rprim collection for this render pass
     */
    HdCyclesRenderPass(HdCyclesRenderDelegate* delegate, HdRenderIndex* index,
                       HdRprimCollection const& collection);

    /**
     * @brief Destroy the HdCycles Render Pass object
     * 
     */
    virtual ~HdCyclesRenderPass();

protected:
    /**
     * @brief Draw the scene with the bound renderpass state
     * 
     * @param renderPassState Input parameters
     * @param renderTags  Which render tags should be drawn this pass
     */
    void _Execute(HdRenderPassStateSharedPtr const& renderPassState,
                  TfTokenVector const& renderTags) override;

    bool IsConverged() const override { return m_isConverged; }

protected:
    HdCyclesRenderDelegate* m_delegate;

    GfMatrix4d m_projMtx;
    GfMatrix4d m_viewMtx;

    std::vector<unsigned char> m_colorBuffer;

public:
    int m_width  = 0;
    int m_height = 0;

    bool m_isConverged = false;
};

PXR_NAMESPACE_CLOSE_SCOPE

#endif  // HD_CYCLES_RENDER_PASS_H
