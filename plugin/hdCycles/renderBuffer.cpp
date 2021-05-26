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

#include "renderBuffer.h"
#include "renderDelegate.h"
#include "renderParam.h"
#include "renderPass.h"

#include <pxr/base/gf/vec2i.h>
#include <pxr/base/gf/vec3i.h>

PXR_NAMESPACE_OPEN_SCOPE

namespace {
template<typename T>
void
_ConvertPixel(HdFormat dstFormat, uint8_t* dst, HdFormat srcFormat, uint8_t const* src)
{
    HdFormat srcComponentFormat = HdGetComponentFormat(srcFormat);
    HdFormat dstComponentFormat = HdGetComponentFormat(dstFormat);
    size_t srcComponentCount = HdGetComponentCount(srcFormat);
    size_t dstComponentCount = HdGetComponentCount(dstFormat);

    for (size_t c = 0; c < dstComponentCount; ++c) {
        T readValue = 0;
        if (c < srcComponentCount) {
            if (srcComponentFormat == HdFormatInt32) {
                readValue = static_cast<T>(reinterpret_cast<const int32_t*>(src)[c]);
            } else if (srcComponentFormat == HdFormatFloat16) {
                GfHalf half;
                half.setBits(reinterpret_cast<const uint16_t*>(src)[c]);
                readValue = static_cast<T>(half);
            } else if (srcComponentFormat == HdFormatFloat32) {
                // We need to subtract one from here due to cycles prim defaulting to 0 but hydra to -1
                readValue = static_cast<T>(reinterpret_cast<const float*>(src)[c]);
            } else if (srcComponentFormat == HdFormatUNorm8) {
                readValue = static_cast<T>(reinterpret_cast<const uint8_t*>(src)[c] / 255.0f);
            } else if (srcComponentFormat == HdFormatSNorm8) {
                readValue = static_cast<T>(reinterpret_cast<const int8_t*>(src)[c] / 127.0f);
            }
        }

        if (dstComponentFormat == HdFormatInt32) {
            reinterpret_cast<int32_t*>(dst)[c] = static_cast<int32_t>(readValue);
        } else if (dstComponentFormat == HdFormatFloat16) {
            reinterpret_cast<uint16_t*>(dst)[c] = GfHalf(float(readValue)).bits();
        } else if (dstComponentFormat == HdFormatFloat32) {
            reinterpret_cast<float*>(dst)[c] = static_cast<float>(readValue);
        } else if (dstComponentFormat == HdFormatUNorm8) {
            reinterpret_cast<uint8_t*>(dst)[c] = static_cast<uint8_t>(static_cast<float>(readValue) * 255.0f);
        } else if (dstComponentFormat == HdFormatSNorm8) {
            reinterpret_cast<int8_t*>(dst)[c] = static_cast<int8_t>(static_cast<float>(readValue) * 127.0f);
        }
    }
}
}  // namespace

HdCyclesRenderBuffer::HdCyclesRenderBuffer(HdCyclesRenderDelegate* renderDelegate, const SdfPath& id)
    : HdRenderBuffer(id)
    , m_width(0)
    , m_height(0)
    , m_format(HdFormatInvalid)
    , m_pixelSize(0)
    , m_mappers(0)
    , m_converged(false)
    , m_renderDelegate(renderDelegate)
{
}

HdCyclesRenderBuffer::~HdCyclesRenderBuffer() {}

/*
    Do not call _Deallocate() from within this function. If you really have to
    try to make use of recursive locking.

    For some reasons if _Deallocate() is called before _Allocate, I get deadlocks
    when resizing the houdini viewport and the reason is still unclear
*/
bool
HdCyclesRenderBuffer::Allocate(const GfVec3i& dimensions, HdFormat format, bool multiSampled)
{
    TF_UNUSED(multiSampled);

    if (dimensions[2] != 1) {
        TF_WARN("Render buffer allocated with dims <%d, %d, %d> and format %s; depth must be 1!", dimensions[0],
                dimensions[1], dimensions[2], TfEnum::GetName(format).c_str());
        return false;
    }

    std::lock_guard<std::mutex> lock_guard { m_mutex };

    // Simulating shrink to fit
    std::vector<uint8_t> buffer_empty {};
    m_buffer.swap(buffer_empty);

    m_width = static_cast<unsigned int>(dimensions[0]);
    m_height = static_cast<unsigned int>(dimensions[1]);
    m_format = format;
    m_pixelSize = static_cast<unsigned int>(HdDataSizeOfFormat(format));
    m_buffer.resize(m_width * m_height * m_pixelSize, 0);

    return true;
}

unsigned int
HdCyclesRenderBuffer::GetWidth() const
{
    return m_width;
}

unsigned int
HdCyclesRenderBuffer::GetHeight() const
{
    return m_height;
}

unsigned int
HdCyclesRenderBuffer::GetDepth() const
{
    return 1;
}

HdFormat
HdCyclesRenderBuffer::GetFormat() const
{
    return m_format;
}

bool
HdCyclesRenderBuffer::IsMultiSampled() const
{
    return false;
}

void*
HdCyclesRenderBuffer::Map()
{
    m_mutex.lock();
    if (m_buffer.empty()) {
        m_mutex.unlock();
        return nullptr;
    }

    m_mappers++;
    return m_buffer.data();
}

void
HdCyclesRenderBuffer::Unmap()
{
    if (!m_buffer.empty()) {
        m_mappers--;
        m_mutex.unlock();
    }
}

bool
HdCyclesRenderBuffer::IsMapped() const
{
    return m_mappers.load() != 0;
}

void
HdCyclesRenderBuffer::Resolve()
{
}

bool
HdCyclesRenderBuffer::IsConverged() const
{
    return m_converged.load();
}

void
HdCyclesRenderBuffer::SetConverged(bool cv)
{
    m_converged.store(cv);
}

void
HdCyclesRenderBuffer::Clear()
{
    if (m_format == HdFormatInvalid)
        return;

    std::lock_guard<std::mutex> lock { m_mutex };

    size_t pixelSize = HdDataSizeOfFormat(m_format);
    memset(&m_buffer[0], 0, m_buffer.size());
}

void
HdCyclesRenderBuffer::Finalize(HdRenderParam* renderParam)
{
    auto param = dynamic_cast<HdCyclesRenderParam*>(renderParam);
    param->RemoveAovBinding(this);
}

void
HdCyclesRenderBuffer::BlitTile(HdFormat format, unsigned int x, unsigned int y, int unsigned width, unsigned int height,
                               int offset, int stride, uint8_t const* data)
{
    // TODO: BlitTile shouldnt be called but it is...
    if (m_width <= 0) {
        return;
    }
    if (m_height <= 0) {
        return;
    }
    if (m_buffer.size() <= 0) {
        return;
    }

    size_t pixelSize = HdDataSizeOfFormat(format);

    if (m_format == format) {
        for (unsigned int j = 0; j < height; ++j) {
            if ((x + width) <= m_width) {
                if ((y + height) <= m_height) {
                    int mem_start = static_cast<int>((((y + j) * m_width) * pixelSize) + (x * pixelSize));


                    unsigned int tile_mem_start = (j * width) * static_cast<unsigned int>(pixelSize);

                    memcpy(&m_buffer[mem_start], &data[tile_mem_start], width * pixelSize);
                }
            }
        }
    } else {
        // Convert pixel by pixel, with nearest point sampling.
        // If src and dst are both int-based, don't round trip to float.
        bool convertAsInt = (HdGetComponentFormat(format) == HdFormatInt32)
                            && (HdGetComponentFormat(m_format) == HdFormatInt32);

        for (unsigned int j = 0; j < height; ++j) {
            for (unsigned int i = 0; i < width; ++i) {
                size_t mem_start = (((y + j) * m_width) * m_pixelSize) + ((x + i) * m_pixelSize);

                int tile_mem_start = static_cast<int>(((j * width) * pixelSize) + (i * pixelSize));

                if (convertAsInt) {
                    _ConvertPixel<int32_t>(m_format, &m_buffer[mem_start], format, &data[tile_mem_start]);
                } else {
                    if (mem_start >= m_buffer.size()) {
                        // TODO: This is triggered more times than it should be
                    } else {
                        _ConvertPixel<float>(m_format, &m_buffer[mem_start], format, &data[tile_mem_start]);
                    }
                }
            }
        }
    }
}

void
HdCyclesRenderBuffer::_Deallocate()
{
    std::lock_guard<std::mutex> lock { m_mutex };

    m_width = 0;
    m_height = 0;
    m_format = HdFormatInvalid;

    std::vector<uint8_t> buffer_empty {};
    m_buffer.swap(buffer_empty);
    m_mappers.store(0);
    m_converged.store(false);
}

PXR_NAMESPACE_CLOSE_SCOPE