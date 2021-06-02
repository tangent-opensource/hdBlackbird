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

#ifndef HD_CYCLES_RENDER_BUFFER_H
#define HD_CYCLES_RENDER_BUFFER_H

#include "api.h"

#include <pxr/imaging/hd/renderBuffer.h>
#include <pxr/pxr.h>

#include <mutex>

PXR_NAMESPACE_OPEN_SCOPE

class HdCyclesRenderDelegate;

/**
 * @brief Utility class for handling HdCycles Render Buffers
 * This handles 2d images for render output.
 * 
 */
class HdCyclesRenderBuffer : public HdRenderBuffer {
public:
    /**
     * @brief Construct a new HdCycles Render Buffer object
     * 
     * @param id Path to the Render Buffer Primitive
     */
    HdCyclesRenderBuffer(HdCyclesRenderDelegate* renderDelegate, const SdfPath& id);

    /**
     * @brief Destroy the HdCycles Render Buffer object
     * 
     */
    ~HdCyclesRenderBuffer() override;

    /**
     * @brief Allocates the memory used by the render buffer
     * 
     * @param dimensions 3 Dimension Vector describing the dimensions of the
     * render buffer
     * @param format HdFormat specifying the format of the Render Buffer 
     * @param multiSampled Bool to indicate if the Render Buffer is multisampled
     * @return Returns true if allocation was successful
     */
    bool Allocate(const GfVec3i& dimensions, HdFormat format, bool multiSampled) override;

    /**
     * @return Returns the width of the render buffer
     */
    unsigned int GetWidth() const override;

    /**
     * @return Returns the height of the render buffer
     */
    unsigned int GetHeight() const override;

    /**
     * @return Returns the depth of the render buffer
     */
    unsigned int GetDepth() const override;

    /**
     * @return Returns the format of the render buffer
     */
    HdFormat GetFormat() const override;

    /**
     * @return Returns if the render buffer is multi-sampled
     */
    bool IsMultiSampled() const override;

    /**
     * @brief Maps the render buffer to the system memory.
     * This returns the Cycles representation of data stored in _buffer
     * 
     * @return Pointer to the render buffer mapped to system memory
     */
    void* Map() override;

    /**
     * @brief Unmaps the render buffer by decrementing ref count.
     * TODO: Should this free memory?
     * 
     * @return Unmap 
     */
    void Unmap() override;

    /**
     * @return Returns true if the render buffer is mapped to system memory
     */
    bool IsMapped() const override;

    /**
     * @brief Resolve the buffer so that reads reflect the latest writes
     * This does nothing.
     */
    void Resolve() override;

    /** 
     * @return Returns true if the buffer is converged.
     */
    bool IsConverged() const override;
    /**
     * @brief Set whether or not the buffer is Converged
     * 
     * @param cv Is Converged
     */
    void SetConverged(bool cv);

    void Clear();

    void Finalize(HdRenderParam* renderParam) override;

    /**
     * @brief Helper to blit the render buffer data
     * 
     * @param format Input format
     * @param width Width of buffer
     * @param height Height of buffer
     * @param offset Offset between pixels
     * @param stride Stride of pixel
     * @param data Pointer to data
     */
    void BlitTile(HdFormat format, unsigned int x, unsigned int y, unsigned int width, unsigned int height,
                  float width_data, float height_data, int offset, int stride, uint8_t const* data);

protected:
    /**
     * @brief Deallocate memory allocated by the render buffer
     * TODO: Implement this
     */
    void _Deallocate() override;

private:
    unsigned int m_width;
    unsigned int m_height;
    HdFormat m_format;
    unsigned int m_pixelSize;

    std::vector<uint8_t> m_buffer;
    std::atomic<int> m_mappers;
    std::atomic<bool> m_converged;

    // Synchronizes resize with writes from cycles and
    // reads from hydra. Maybe it could be made more lightweight
    // with some assumptions on execution?
    std::mutex m_mutex;

    HdCyclesRenderDelegate* m_renderDelegate;
};

PXR_NAMESPACE_CLOSE_SCOPE

#endif  // HD_CYCLES_RENDER_BUFFER_H
