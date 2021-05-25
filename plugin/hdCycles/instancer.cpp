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

#include "instancer.h"

#include <pxr/base/gf/matrix4f.h>
#include <pxr/base/gf/quatd.h>
#include <pxr/base/gf/quath.h>
#include <pxr/base/gf/rotation.h>
#include <pxr/imaging/hd/sceneDelegate.h>

PXR_NAMESPACE_OPEN_SCOPE

// TODO: Use HdInstancerTokens when Houdini updates USD to 20.02

// clang-format off
TF_DEFINE_PRIVATE_TOKENS(_tokens,
    (instanceTransform)
    (rotate)
    (scale)
    (translate)
);
// clang-format on

void
HdCyclesInstancer::Sync()
{
    HD_TRACE_FUNCTION();
    HF_MALLOC_TAG_FUNCTION();

    const SdfPath& instancerId = GetId();
    auto& changeTracker = GetDelegate()->GetRenderIndex().GetChangeTracker();

    // Use the double-checked locking pattern to check if this instancer's
    // primvars are dirty.
    int dirtyBits = changeTracker.GetInstancerDirtyBits(instancerId);
    if (!HdChangeTracker::IsAnyPrimvarDirty(dirtyBits, instancerId)) {
        return;
    }

    std::lock_guard<std::mutex> lock(m_syncMutex);
    dirtyBits = changeTracker.GetInstancerDirtyBits(instancerId);
    if (!HdChangeTracker::IsAnyPrimvarDirty(dirtyBits, instancerId)) {
        return;
    }

    auto primvarDescs = GetDelegate()->GetPrimvarDescriptors(instancerId, HdInterpolationInstance);
    for (auto& desc : primvarDescs) {
        if (!HdChangeTracker::IsPrimvarDirty(dirtyBits, instancerId, desc.name)) {
            continue;
        }

        VtValue value = GetDelegate()->Get(instancerId, desc.name);
        if (value.IsEmpty()) {
            continue;
        }

        if (desc.name == _tokens->translate) {
            if (value.IsHolding<VtVec3fArray>()) {
                m_translate = value.UncheckedGet<VtVec3fArray>();
            }
        } else if (desc.name == _tokens->rotate) {
            if (value.IsHolding<VtVec4fArray>()) {
                m_rotate = value.UncheckedGet<VtVec4fArray>();
            }
        } else if (desc.name == _tokens->scale) {
            if (value.IsHolding<VtVec3fArray>()) {
                m_scale = value.UncheckedGet<VtVec3fArray>();
            }
        } else if (desc.name == _tokens->instanceTransform) {
            if (value.IsHolding<VtMatrix4dArray>()) {
                m_transform = value.UncheckedGet<VtMatrix4dArray>();
            }
        }
    }

    // Mark the instancer as clean
    changeTracker.MarkInstancerClean(instancerId);
}

VtMatrix4dArray
HdCyclesInstancer::ComputeTransforms(SdfPath const& prototypeId)
{
    Sync();

    GfMatrix4d instancerTransform = GetDelegate()->GetInstancerTransform(GetId());
    VtIntArray instanceIndices = GetDelegate()->GetInstanceIndices(GetId(), prototypeId);

    VtMatrix4dArray transforms;
    transforms.reserve(instanceIndices.size());
    for (int idx : instanceIndices) {
        GfMatrix4d translateMat(1);
        GfMatrix4d rotateMat(1);
        GfMatrix4d scaleMat(1);
        GfMatrix4d transform(1);

        if (!m_translate.empty()) {
            translateMat.SetTranslate(GfVec3d(m_translate.cdata()[idx]));
        }

        if (!m_rotate.empty()) {
            auto& v = m_rotate.cdata()[idx];
            rotateMat.SetRotate(GfQuatd(v[0], GfVec3d(v[1], v[2], v[3])));
        }

        if (!m_scale.empty()) {
            scaleMat.SetScale(GfVec3d(m_scale.cdata()[idx]));
        }

        if (!m_transform.empty()) {
            transform = m_transform.cdata()[idx];
        }

        transforms.push_back(transform * scaleMat * rotateMat * translateMat * instancerTransform);
    }

    auto parentInstancer = static_cast<HdCyclesInstancer*>(GetDelegate()->GetRenderIndex().GetInstancer(GetParentId()));
    if (!parentInstancer) {
        return transforms;
    }

    VtMatrix4dArray wordTransform;
    for (const GfMatrix4d& parentTransform : parentInstancer->ComputeTransforms(GetId())) {
        for (const GfMatrix4d& localTransform : transforms) {
            wordTransform.push_back(parentTransform * localTransform);
        }
    }

    return wordTransform;
}

namespace {
// Helper to accumulate sample times from the largest set of
// samples seen, up to maxNumSamples.
template<typename T1, typename T2, unsigned int C>
void
AccumulateSampleTimes(HdTimeSampleArray<T1, C> const& in, HdTimeSampleArray<T2, C>* out)
{
    if (in.count > out->count) {
        out->Resize(static_cast<unsigned int>(in.count));
        out->times = in.times;
    }
}

// Apply transforms referenced by instanceIndices
template<typename Op, typename T>
void
ApplyTransform(VtValue const& allTransformsValue, VtIntArray const& instanceIndices, GfMatrix4d* transforms)
{
    auto& allTransforms = allTransformsValue.Get<VtArray<T>>();
    if (allTransforms.empty()) {
        TF_RUNTIME_ERROR("No transforms");
        return;
    }

    for (size_t i = 0; i < instanceIndices.size(); ++i) {
        transforms[i] = Op {}(allTransforms[instanceIndices[i]]) * transforms[i];
    }
}

// Apply interpolated transforms referenced by instanceIndices
template<typename Op, typename T>
void
ApplyTransform(float alpha, VtValue const& allTransformsValue0, VtValue const& allTransformsValue1,
               VtIntArray const& instanceIndices, GfMatrix4d* transforms)
{
    auto& allTransforms0 = allTransformsValue0.Get<VtArray<T>>();
    auto& allTransforms1 = allTransformsValue1.Get<VtArray<T>>();
    if (allTransforms0.empty() || allTransforms1.empty()) {
        TF_RUNTIME_ERROR("No transforms");
        return;
    }

    for (size_t i = 0; i < instanceIndices.size(); ++i) {
        auto transform = HdResampleNeighbors(alpha, allTransforms0[instanceIndices[i]],
                                             allTransforms1[instanceIndices[i]]);
        transforms[i] = Op {}(transform)*transforms[i];
    }
}

template<typename Op, typename T>
void
ApplyTransform(HdTimeSampleArray<VtValue, HD_BLACKBIRD_MOTION_STEPS> const& samples, VtIntArray const& instanceIndices,
               float time, GfMatrix4d* transforms)
{
    using size_type = typename decltype(samples.values)::size_type;

    size_type i = 0;
    for (; i < samples.count; ++i) {
        if (samples.times[i] == time) {
            // Exact time match
            return ApplyTransform<Op, T>(samples.values[i], instanceIndices, transforms);
        }
        if (samples.times[i] > time) {
            break;
        }
    }

    if (i == 0) {
        // time is before the first sample.
        return ApplyTransform<Op, T>(samples.values[0], instanceIndices, transforms);
    } else if (i == samples.count) {
        // time is after the last sample.
        return ApplyTransform<Op, T>(samples.values[static_cast<size_type>(samples.count) - 1], instanceIndices,
                                     transforms);
    } else if (samples.times[i] == samples.times[i - 1]) {
        // Neighboring samples have identical parameter.
        // Arbitrarily choose a sample.
        TF_WARN("overlapping samples at %f; using first sample", samples.times[i]);
        return ApplyTransform<Op, T>(samples.values[i - 1], instanceIndices, transforms);
    } else {
        // Linear blend of neighboring samples.
        float alpha = (samples.times[i] - time) / (samples.times[i] - samples.times[i - 1]);
        return ApplyTransform<Op, T>(alpha, samples.values[i - 1], samples.values[i], instanceIndices, transforms);
    }
}

struct TranslateOp {
    template<typename T> GfMatrix4d operator()(T const& translate)
    {
        return GfMatrix4d(1).SetTranslate(GfVec3d(translate));
    }
};

struct RotateOp {
    template<typename T> GfMatrix4d operator()(T const& rotate)
    {
        return GfMatrix4d(1).SetRotate(GfRotation(GfQuatd(rotate)));
    }
};

struct ScaleOp {
    template<typename T> GfMatrix4d operator()(T const& scale) { return GfMatrix4d(1).SetScale(GfVec3d(scale)); }
};

struct TransformOp {
    GfMatrix4d const& operator()(GfMatrix4d const& transform) { return transform; }

    GfMatrix4d operator()(GfMatrix4f const& transform) { return GfMatrix4d(transform); }
};

}  // namespace

HdTimeSampleArray<VtMatrix4dArray, HD_BLACKBIRD_MOTION_STEPS>
HdCyclesInstancer::SampleInstanceTransforms(SdfPath const& prototypeId)
{
    HdSceneDelegate* delegate = GetDelegate();
    const SdfPath& instancerId = GetId();

    VtIntArray instanceIndices = delegate->GetInstanceIndices(instancerId, prototypeId);

    HdTimeSampleArray<GfMatrix4d, HD_BLACKBIRD_MOTION_STEPS> instancerXform;
    HdTimeSampleArray<VtValue, HD_BLACKBIRD_MOTION_STEPS> instanceXforms;
    HdTimeSampleArray<VtValue, HD_BLACKBIRD_MOTION_STEPS> translates;
    HdTimeSampleArray<VtValue, HD_BLACKBIRD_MOTION_STEPS> rotates;
    HdTimeSampleArray<VtValue, HD_BLACKBIRD_MOTION_STEPS> scales;
    delegate->SampleInstancerTransform(instancerId, &instancerXform);
    delegate->SamplePrimvar(instancerId, _tokens->instanceTransform, &instanceXforms);
    delegate->SamplePrimvar(instancerId, _tokens->translate, &translates);
    delegate->SamplePrimvar(instancerId, _tokens->scale, &scales);
    delegate->SamplePrimvar(instancerId, _tokens->rotate, &rotates);

    using size_type = typename decltype(instancerXform.values)::size_type;

    // Hydra might give us falsely varying instancerXform, i.e. more than one time sample with the sample matrix
    // This will lead to huge over computation in case it's the only array with a few time samples
    if (instancerXform.count > 1) {
        size_type iSample = 1;
        for (; iSample < instancerXform.values.size(); ++iSample) {
            if (!GfIsClose(instancerXform.values[iSample - 1], instancerXform.values[iSample], 1e-6)) {
                break;
            }
        }
        // All samples the same
        if (iSample == instancerXform.values.size()) {
            instancerXform.Resize(1);
        }
    }

    // As a simple resampling strategy, find the input with the max #
    // of samples and use its sample placement.  In practice we expect
    // them to all be the same, i.e. to not require resampling.
    HdTimeSampleArray<VtMatrix4dArray, HD_BLACKBIRD_MOTION_STEPS> sa;
    sa.Resize(0);
    AccumulateSampleTimes(instancerXform, &sa);
    AccumulateSampleTimes(instanceXforms, &sa);
    AccumulateSampleTimes(translates, &sa);
    AccumulateSampleTimes(scales, &sa);
    AccumulateSampleTimes(rotates, &sa);

    for (size_type i = 0; i < sa.count; ++i) {
        const float t = sa.times[i];

        GfMatrix4d xf(1);
        if (instancerXform.count > 0) {
            xf = instancerXform.Resample(t);
        }

        auto& transforms = sa.values[i];
        transforms = VtMatrix4dArray(instanceIndices.size(), xf);

        if (translates.count > 0 && translates.values[0].IsArrayValued()) {
            auto& type = translates.values[0].GetElementTypeid();
            if (type == typeid(GfVec3f)) {
                ApplyTransform<TranslateOp, GfVec3f>(translates, instanceIndices, t, transforms.data());
            } else if (type == typeid(GfVec3d)) {
                ApplyTransform<TranslateOp, GfVec3d>(translates, instanceIndices, t, transforms.data());
            } else if (type == typeid(GfVec3h)) {
                ApplyTransform<TranslateOp, GfVec3h>(translates, instanceIndices, t, transforms.data());
            }
        }

        if (rotates.count > 0 && rotates.values[0].IsArrayValued()) {
            auto& type = rotates.values[0].GetElementTypeid();
            if (type == typeid(GfQuath)) {
                ApplyTransform<RotateOp, GfQuath>(rotates, instanceIndices, t, transforms.data());
            } else if (type == typeid(GfQuatf)) {
                ApplyTransform<RotateOp, GfQuatf>(rotates, instanceIndices, t, transforms.data());
            } else if (type == typeid(GfQuatd)) {
                ApplyTransform<RotateOp, GfQuatd>(rotates, instanceIndices, t, transforms.data());
            }
        }

        if (scales.count > 0 && scales.values[0].IsArrayValued()) {
            auto& type = scales.values[0].GetElementTypeid();
            if (type == typeid(GfVec3f)) {
                ApplyTransform<ScaleOp, GfVec3f>(scales, instanceIndices, t, transforms.data());
            } else if (type == typeid(GfVec3d)) {
                ApplyTransform<ScaleOp, GfVec3d>(scales, instanceIndices, t, transforms.data());
            } else if (type == typeid(GfVec3h)) {
                ApplyTransform<ScaleOp, GfVec3h>(scales, instanceIndices, t, transforms.data());
            }
        }

        if (instanceXforms.count > 0 && instanceXforms.values[0].IsArrayValued()) {
            auto& type = instanceXforms.values[0].GetElementTypeid();
            if (type == typeid(GfMatrix4d)) {
                ApplyTransform<TransformOp, GfMatrix4d>(instanceXforms, instanceIndices, t, transforms.data());
            } else if (type == typeid(GfMatrix4f)) {
                ApplyTransform<TransformOp, GfMatrix4f>(instanceXforms, instanceIndices, t, transforms.data());
            }
        }
    }

    // If there is a parent instancer, continue to unroll
    // the child instances across the parent; otherwise we're done.
    if (GetParentId().IsEmpty()) {
        return sa;
    }

    HdInstancer* parentInstancer = GetDelegate()->GetRenderIndex().GetInstancer(GetParentId());
    if (!TF_VERIFY(parentInstancer)) {
        return sa;
    }
    auto cyclesParentInstancer = static_cast<HdCyclesInstancer*>(parentInstancer);

    // Multiply the instance samples against the parent instancer samples.
    auto parentXf = cyclesParentInstancer->SampleInstanceTransforms(GetId());
    if (parentXf.count == 0 || parentXf.values[0].empty()) {
        // No samples for parent instancer.
        return sa;
    }
    // Move aside previously computed child xform samples to childXf.
    HdTimeSampleArray<VtMatrix4dArray, HD_BLACKBIRD_MOTION_STEPS> childXf(sa);
    // Merge sample times, taking the densest sampling.
    AccumulateSampleTimes(parentXf, &sa);
    // Apply parent xforms to the children.
    for (size_type i = 0; i < sa.count; ++i) {
        const float t = sa.times[i];
        // Resample transforms at the same time.
        VtMatrix4dArray curParentXf = parentXf.Resample(t);
        VtMatrix4dArray curChildXf = childXf.Resample(t);
        // Multiply out each combination.
        VtMatrix4dArray& result = sa.values[i];
        result.resize(curParentXf.size() * curChildXf.size());
        for (size_t j = 0; j < curParentXf.size(); ++j) {
            for (size_t k = 0; k < curChildXf.size(); ++k) {
                result[j * curChildXf.size() + k] = curChildXf[k] * curParentXf[j];
            }
        }
    }

    return sa;
}

PXR_NAMESPACE_CLOSE_SCOPE