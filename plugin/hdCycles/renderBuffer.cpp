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

#include "renderBuffer.h"

#include <pxr/base/gf/vec2i.h>
#include <pxr/base/gf/vec3i.h>

PXR_NAMESPACE_OPEN_SCOPE

namespace {
template<typename T>
void
_ConvertPixel(HdFormat dstFormat, uint8_t* dst, HdFormat srcFormat,
              uint8_t const* src)
{
    HdFormat srcComponentFormat = HdGetComponentFormat(srcFormat);
    HdFormat dstComponentFormat = HdGetComponentFormat(dstFormat);
    size_t srcComponentCount    = HdGetComponentCount(srcFormat);
    size_t dstComponentCount    = HdGetComponentCount(dstFormat);

    for (size_t c = 0; c < dstComponentCount; ++c) {
        T readValue = 0;
        if (c < srcComponentCount) {
            if (srcComponentFormat == HdFormatInt32) {
                readValue = ((int32_t*)src)[c];
            } else if (srcComponentFormat == HdFormatFloat16) {
                GfHalf half;
                half.setBits(((uint16_t*)src)[c]);
                readValue = static_cast<float>(half);
            } else if (srcComponentFormat == HdFormatFloat32) {
                readValue = ((float*)src)[c];
            } else if (srcComponentFormat == HdFormatUNorm8) {
                readValue = ((uint8_t*)src)[c] / 255.0f;
            } else if (srcComponentFormat == HdFormatSNorm8) {
                readValue = ((int8_t*)src)[c] / 127.0f;
            }
        }

        if (dstComponentFormat == HdFormatInt32) {
            ((int32_t*)dst)[c] = readValue;
        } else if (dstComponentFormat == HdFormatFloat16) {
            ((uint16_t*)dst)[c] = GfHalf(float(readValue)).bits();
        } else if (dstComponentFormat == HdFormatFloat32) {
            ((float*)dst)[c] = readValue;
        } else if (dstComponentFormat == HdFormatUNorm8) {
            ((uint8_t*)dst)[c] = (readValue * 255.0f);
        } else if (dstComponentFormat == HdFormatSNorm8) {
            ((int8_t*)dst)[c] = (readValue * 127.0f);
        }
    }
}
}  // namespace

HdCyclesRenderBuffer::HdCyclesRenderBuffer(const SdfPath& id)
    : HdRenderBuffer(id)
    , m_width(0)
    , m_height(0)
    , m_format(HdFormatInvalid)
    , m_pixelSize(0)
    , m_mappers(0)
    , m_converged(false)
{
}

bool
HdCyclesRenderBuffer::Allocate(const GfVec3i& dimensions, HdFormat format,
                               bool multiSampled)
{
    _Deallocate();

    if (dimensions[2] != 1) {
        TF_WARN(
            "Render buffer allocated with dims <%d, %d, %d> and format %s; depth must be 1!",
            dimensions[0], dimensions[1], dimensions[2],
            TfEnum::GetName(format).c_str());
        return false;
    }

    m_width     = dimensions[0];
    m_height    = dimensions[1];
    m_format    = format;
    m_pixelSize = HdDataSizeOfFormat(format);
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
    m_mappers++;
    return m_buffer.data();
}

void
HdCyclesRenderBuffer::Unmap()
{
    m_mappers--;
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
HdCyclesRenderBuffer::Blit(HdFormat format, int width, int height, int offset,
                           int stride, uint8_t const* data)
{
    if (m_format == format) {
        if (static_cast<unsigned int>(width) == m_width
            && static_cast<unsigned int>(height) == m_height) {
            // Blit line by line.
            for (unsigned int j = 0; j < m_height; ++j) {
                memcpy(&m_buffer[(j * m_width) * m_pixelSize],
                       &data[(j * stride + offset) * m_pixelSize],
                       m_width * m_pixelSize);
            }
        } else {
            // Blit pixel by pixel, with nearest point sampling.
            float scalei = width / float(m_width);
            float scalej = height / float(m_height);
            for (unsigned int j = 0; j < m_height; ++j) {
                for (unsigned int i = 0; i < m_width; ++i) {
                    unsigned int ii = scalei * i;
                    unsigned int jj = scalej * j;
                    memcpy(&m_buffer[(j * m_width + i) * m_pixelSize],
                           &data[(jj * stride + offset + ii) * m_pixelSize],
                           m_pixelSize);
                }
            }
        }
    } else {
        // Convert pixel by pixel, with nearest point sampling.
        // If src and dst are both int-based, don't round trip to float.
        size_t pixelSize  = HdDataSizeOfFormat(format);
        bool convertAsInt = (HdGetComponentFormat(format) == HdFormatInt32)
                            && (HdGetComponentFormat(m_format)
                                == HdFormatInt32);

        float scalei = width / float(m_width);
        float scalej = height / float(m_height);
        for (unsigned int j = 0; j < m_height; ++j) {
            for (unsigned int i = 0; i < m_width; ++i) {
                unsigned int ii = scalei * i;
                unsigned int jj = scalej * j;
                if (convertAsInt) {
                    _ConvertPixel<int32_t>(
                        m_format,
                        static_cast<uint8_t*>(
                            &m_buffer[(j * m_width + i) * m_pixelSize]),
                        format, &data[(jj * stride + offset + ii) * pixelSize]);
                } else {
                    _ConvertPixel<float>(
                        m_format,
                        static_cast<uint8_t*>(
                            &m_buffer[(j * m_width + i) * m_pixelSize]),
                        format, &data[(jj * stride + offset + ii) * pixelSize]);
                }
            }
        }
    }
}

void
HdCyclesRenderBuffer::_Deallocate()
{
    m_width  = 0;
    m_height = 0;
    m_format = HdFormatInvalid;
    m_buffer.resize(0);
    m_mappers.store(0);
    m_converged.store(false);
}

PXR_NAMESPACE_CLOSE_SCOPE