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

#include "transformSource.h"
#include "utils.h"

#include <util/util_transform.h>

#include <unordered_set>

namespace {

///
/// Hashable helper class for motion sample overlap elimination
///
struct HdCyclesIndexedTimeSample {
    using index_type = ccl::uint;
    using time_type  = float;

    static constexpr time_type epsilon     = static_cast<time_type>(1e-5);
    static constexpr index_type resolution = static_cast<index_type>(static_cast<time_type>(1.0) / epsilon);

    HdCyclesIndexedTimeSample(index_type _index, time_type _time)
        : index { _index }
        , time { _time }
    {
        assert(time >= static_cast<time_type>(-1.0) && time <= static_cast<time_type>(1.0));
    }

    std::size_t Hash() const { return static_cast<size_t>(resolution + time * resolution); }

    index_type index;
    time_type time;
};

bool
operator<(const HdCyclesIndexedTimeSample& lhs, const HdCyclesIndexedTimeSample& rhs)
{
    return lhs.time < rhs.time;
}

bool
operator==(const HdCyclesIndexedTimeSample& lhs, const HdCyclesIndexedTimeSample& rhs)
{
    return std::abs(lhs.time - rhs.time) <= HdCyclesIndexedTimeSample::epsilon;
}

}  // namespace

namespace std {

template<> struct hash<HdCyclesIndexedTimeSample> {
    using This = HdCyclesIndexedTimeSample;
    std::size_t operator()(const This& s) const noexcept { return s.Hash(); }
};

}  // namespace std

PXR_NAMESPACE_USING_DIRECTIVE

namespace {

template<typename TYPE, unsigned int CAPACITY>
HdTimeSampleArray<TYPE, CAPACITY>
HdCyclesTimeSamplesRemoveOverlaps(const HdTimeSampleArray<TYPE, CAPACITY>& samples)
{
    if (samples.count == 1) {
        return samples;
    }

    // 2x number of buckets to cover negative and positive time samples
    std::unordered_set<HdCyclesIndexedTimeSample> unique { 2 * HdCyclesIndexedTimeSample::resolution };
    for (unsigned int i = 0; i < samples.count; ++i) {
        using index_type = HdCyclesIndexedTimeSample::index_type;
        unique.insert(HdCyclesIndexedTimeSample { static_cast<index_type>(i), samples.times[i] });
    }

    TfSmallVector<HdCyclesIndexedTimeSample, CAPACITY> sorted { unique.begin(), unique.end() };
    std::sort(sorted.begin(), sorted.end());

    HdTimeSampleArray<TYPE, CAPACITY> result;
    result.Resize(static_cast<unsigned int>(unique.size()));

    using size_type = typename decltype(HdTimeSampleArray<TYPE, CAPACITY>::times)::size_type;
    for (size_type i = 0; i < sorted.size(); ++i) {
        result.times[i]  = sorted[i].time;
        result.values[i] = samples.values[sorted[i].index];
    }

    return result;
}

template<typename TYPE, unsigned int CAPACITY>
bool
HdCyclesAreTimeSamplesUniformlyDistributed(const HdTimeSampleArray<TYPE, CAPACITY>& array)
{
    if (array.count < 3) {
        return true;
    }

    using size_type  = typename decltype(HdTimeSampleArray<TYPE, CAPACITY>::times)::size_type;
    using value_type = typename decltype(HdTimeSampleArray<TYPE, CAPACITY>::times)::value_type;

    // reference segment - samples must be sorted in ascending order
    const value_type ref_segment = array.times[1] - array.times[0];
    for (size_type i = 2; i < array.count; ++i) {
        auto l = array.times[i] - array.times[i - 1];
        if (std::abs(l - ref_segment) > HdCyclesIndexedTimeSample::epsilon) {
            return false;
        }
    }

    return true;
}

}  // namespace

HdCyclesTransformSource::HdCyclesTransformSource(ccl::Object* object, const HdCyclesMatrix4dTimeSampleArray& samples,
                                                 const GfMatrix4d& fallback, unsigned int new_num_samples)
    : m_object { object }
    , m_samples { samples }
    , m_fallback { fallback }
    , m_new_num_samples { new_num_samples }
{
}

bool
HdCyclesTransformSource::_CheckValid() const
{
    if (!m_object) {
        return false;
    }

    return m_samples.count < HD_CYCLES_MAX_TRANSFORM_STEPS;
}

HdCyclesTransformTimeSampleArray
HdCyclesTransformSource::ResampleUniform(const HdCyclesMatrix4dTimeSampleArray& samples, unsigned int new_num_samples)
{
    if (new_num_samples % 2 == 0)
        new_num_samples += 1;

    HdCyclesTransformTimeSampleArray resampled;
    resampled.Resize(new_num_samples);

    const auto num_samples = static_cast<unsigned int>(samples.count);

    // sample - point in time, segment - width between two samples
    // 3 samples = 2 segments => num_segments = num_samples - 1
    const float shutter_time            = samples.times[num_samples - 1] - samples.times[0];
    const unsigned int new_num_segments = new_num_samples > 1 ? new_num_samples - 1 : 1;
    const float new_segment_width       = shutter_time / static_cast<float>(new_num_segments);

    //
    unsigned int sample = 1;
    for (unsigned int i = 0; i < new_num_samples; ++i) {
        const float time = samples.times.front() + static_cast<float>(i) * new_segment_width;

        // Search for segment: [sample - 1, sample]
        for (; sample < samples.count;) {
            if (time >= samples.times[sample - 1] && time <= samples.times[sample]) {
                break;
            }
            ++sample;
        }

        resampled.times[i] = time;

        const unsigned int iXfPrev = sample - 1;
        const unsigned int iXfNext = sample;

        // boundary conditions and any other overlapping sample
        if (std::abs(time - samples.times[iXfPrev]) <= HdCyclesIndexedTimeSample::epsilon) {
            resampled.values[i] = mat4d_to_transform(samples.values[iXfPrev]);
            continue;
        }

        if (std::abs(time - samples.times[iXfNext]) <= HdCyclesIndexedTimeSample::epsilon) {
            resampled.values[i] = mat4d_to_transform(samples.values[iXfNext]);
            continue;
        }

        // in-between samples compute u coordinate
        ccl::Transform xfPrev = mat4d_to_transform(samples.values[iXfPrev]);
        ccl::Transform xfNext = mat4d_to_transform(samples.values[iXfNext]);

        ccl::DecomposedTransform dxf[2];
        transform_motion_decompose(dxf + 0, &xfPrev, 1);
        transform_motion_decompose(dxf + 1, &xfNext, 1);

        // Preferring the smaller rotation difference
        if (ccl::len_squared(dxf[0].x - dxf[1].x) > ccl::len_squared(dxf[0].x + dxf[1].x)) {
            dxf[1].x = -dxf[1].x;
        }

        // Weighting by distance to sample
        const float timeDiff = samples.times[iXfNext] - samples.times[iXfPrev];
        const float t        = (resampled.times[i] - samples.times[iXfPrev]) / timeDiff;
        assert(t >= 0.0f && t <= 1.0f);

        transform_motion_array_interpolate(&resampled.values[i], dxf, 2, t);
    }

    return resampled;
}

bool
HdCyclesTransformSource::Resolve()
{
    if (!_TryLock()) {
        return false;
    }

    ccl::Object* object = m_object;

    // Hd outputs duplicated time samples, remove all duplicates and keep time samples in ascending order
    m_samples = HdCyclesTimeSamplesRemoveOverlaps(m_samples);

    // No motion samples, no motion blur use fallback value
    if (m_samples.count == 0) {
        object->motion.resize(0);
        object->tfm = mat4d_to_transform(m_fallback);

        // Marked as finished
        _SetResolved();
        return true;
    }

    // Only one motion sample - no motion blur
    if (m_samples.count == 1) {
        object->motion.resize(0);
        object->tfm = mat4d_to_transform(m_samples.values[0]);

        // Marked as finished
        _SetResolved();
        return true;
    }

    // Frame centered motion blur only, with fallback to default value
    const float shutter_open  = m_samples.times[0];
    const float shutter_close = m_samples.times[static_cast<unsigned int>(m_samples.count) - 1];
    if (std::abs(std::abs(shutter_close) - std::abs(shutter_open)) > HdCyclesIndexedTimeSample::epsilon) {
        object->motion.resize(0);
        object->tfm = mat4d_to_transform(m_fallback);

        // Marked as failed and continue
        _SetResolveError();
        return true;
    }

    // Resample motion samples if necessary:
    // * resample if number of samples is even
    // * resample if samples are not distributed evenly
    // * otherwise copy as they are
    //
    auto num_inp_samples = static_cast<unsigned int>(m_samples.count);
    auto num_req_samples = m_new_num_samples > 0 ? m_new_num_samples : num_inp_samples;

    // Check if requested samples are odd samples
    num_req_samples = num_req_samples % 2 == 1 ? num_req_samples : num_req_samples + 1;

    // Check if resampling is required
    bool requires_resampling = false;
    if (num_inp_samples != num_req_samples || !HdCyclesAreTimeSamplesUniformlyDistributed(m_samples)) {
        requires_resampling = true;
    }

    // Resampling
    HdCyclesTransformTimeSampleArray motion_transforms;
    if (requires_resampling) {
        motion_transforms = ResampleUniform(m_samples, num_req_samples);
    } else {
        motion_transforms.Resize(num_req_samples);
        for (unsigned int i = 0; i < num_req_samples; ++i) {
            motion_transforms.values[i] = mat4d_to_transform(m_samples.values[i]);
        }
    }

    // Commit samples
    object->tfm = ccl::transform_identity();
    object->motion.resize(motion_transforms.count);
    for (unsigned int i {}; i < motion_transforms.count; ++i) {
        object->motion[i] = motion_transforms.values[i];
    }

    // Marked as finished
    _SetResolved();
    return true;
}