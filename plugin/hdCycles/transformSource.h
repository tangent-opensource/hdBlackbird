//  Copyright 2021 Tangent Animation
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

#ifndef HDCYCLES_TRANSFORMSOURCE_H
#define HDCYCLES_TRANSFORMSOURCE_H

#include "objectSource.h"

#include <pxr/base/gf/matrix4d.h>
#include <pxr/imaging/hd/bufferSource.h>
#include <pxr/imaging/hd/timeSampleArray.h>
#include <pxr/imaging/hd/tokens.h>

#include <render/geometry.h>
#include <render/object.h>

#include <numeric>
#include <unordered_set>

PXR_NAMESPACE_OPEN_SCOPE

///
/// Max motion samples dictated by Cycles(Embree)
///
static constexpr ccl::uint HD_CYCLES_MAX_TRANSFORM_STEPS = ccl::Object::MAX_MOTION_STEPS;
static constexpr ccl::uint HD_CYCLES_MAX_GEOMETRY_STEPS  = ccl::Geometry::MAX_MOTION_STEPS;

///
/// Common aliases used in motion sampling
///
using HdCyclesValueTimeSampleArray         = HdTimeSampleArray<VtValue, HD_CYCLES_MAX_GEOMETRY_STEPS>;
using HdCyclesVec3fArrayTimeSampleArray    = HdTimeSampleArray<VtVec3fArray, HD_CYCLES_MAX_GEOMETRY_STEPS>;
using HdCyclesMatrix4dTimeSampleArray      = HdTimeSampleArray<GfMatrix4d, HD_CYCLES_MAX_TRANSFORM_STEPS>;
using HdCyclesMatrix4dArrayTimeSampleArray = HdTimeSampleArray<VtMatrix4dArray, HD_CYCLES_MAX_TRANSFORM_STEPS>;
using HdCyclesTransformTimeSampleArray     = HdTimeSampleArray<ccl::Transform, HD_CYCLES_MAX_TRANSFORM_STEPS>;

using HdCyclesTransformSmallVector = TfSmallVector<ccl::Transform, HD_CYCLES_MAX_TRANSFORM_STEPS>;

///
/// Transformation motion sample source
///
class HdCyclesTransformSource : public HdBufferSource {
public:
    HdCyclesTransformSource(ccl::Object* object, const HdCyclesMatrix4dTimeSampleArray& samples,
                            const GfMatrix4d& fallback, unsigned int new_num_samples = 0);

    bool Resolve() override;
    const TfToken& GetName() const override { return HdTokens->transform; }
    void GetBufferSpecs(HdBufferSpecVector* specs) const override {}
    const void* GetData() const override { return nullptr; }
    HdTupleType GetTupleType() const override { return {}; }
    size_t GetNumElements() const override { return 0; }

    const ccl::Object* GetObject() const { return m_object; }

    static HdCyclesTransformTimeSampleArray ResampleUniform(const HdCyclesMatrix4dTimeSampleArray& samples,
                                                            unsigned int new_num_samples);

private:
    bool _CheckValid() const override;

    ccl::Object* m_object;
    HdCyclesMatrix4dTimeSampleArray m_samples;
    GfMatrix4d m_fallback;
    unsigned int m_new_num_samples;
};

PXR_NAMESPACE_CLOSE_SCOPE

#endif  //HDCYCLES_TRANSFORMSOURCE_H
