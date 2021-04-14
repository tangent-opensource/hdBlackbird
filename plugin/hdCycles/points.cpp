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

#include "points.h"

#include "config.h"
#include "material.h"
#include "renderParam.h"
#include "utils.h"

#include <render/mesh.h>
#include <render/object.h>
#include <render/pointcloud.h>
#include <render/scene.h>
#include <render/shader.h>
#include <util/util_math_float3.h>
#include <util/util_string.h>

#include <pxr/base/gf/vec3f.h>
#include <pxr/base/gf/vec3i.h>
#include <pxr/imaging/hd/mesh.h>
#include <pxr/imaging/hd/sceneDelegate.h>

#ifdef USE_USD_CYCLES_SCHEMA
#    include <usdCycles/tokens.h>
#endif

PXR_NAMESPACE_OPEN_SCOPE

HdCyclesPoints::HdCyclesPoints(SdfPath const& id, SdfPath const& instancerId, HdCyclesRenderDelegate* a_renderDelegate)
    : HdPoints(id, instancerId)
    , m_renderDelegate(a_renderDelegate)
    , m_transform(ccl::transform_identity())
{
    static const HdCyclesConfig& config = HdCyclesConfig::GetInstance();
    config.enable_motion_blur.eval(m_useMotionBlur, true);

    config.default_point_style.eval(m_pointStyle, true);
    config.default_point_resolution.eval(m_pointResolution, true);

    if (m_useMotionBlur) {
        config.motion_steps.eval(m_motionSteps, true);
    }

    _InitializeNewCyclesPointCloud();
}

HdCyclesPoints::~HdCyclesPoints()
{
    if (m_cyclesPointCloud) {
        m_renderDelegate->GetCyclesRenderParam()->RemovePointCloud(
            m_cyclesPointCloud);
        delete m_cyclesPointCloud;
    }

    if (m_cyclesObject) {
        m_renderDelegate->GetCyclesRenderParam()->RemoveObject(m_cyclesObject);
        delete m_cyclesObject;
    }
}

void
HdCyclesPoints::_InitRepr(TfToken const& reprToken, HdDirtyBits* dirtyBits)
{
}

HdDirtyBits
HdCyclesPoints::_PropagateDirtyBits(HdDirtyBits bits) const
{
    return bits;
}

void
HdCyclesPoints::Finalize(HdRenderParam* renderParam)
{
}

void 
HdCyclesPoints::_InitializeNewCyclesPointCloud() {
    m_cyclesPointCloud = new ccl::PointCloud();
    assert(m_cyclesPointCloud);
    m_cyclesPointCloud->point_style = (ccl::PointCloudPointStyle)m_pointStyle;
    m_renderDelegate->GetCyclesRenderParam()->AddGeometry(m_cyclesPointCloud);

    m_cyclesObject             = new ccl::Object();
    assert(m_cyclesObject);
    m_cyclesObject->geometry   = m_cyclesPointCloud;
    m_cyclesObject->tfm        = ccl::transform_identity();
    m_cyclesObject->pass_id    = -1;
    m_cyclesObject->visibility = ccl::PATH_RAY_ALL_VISIBILITY;
    m_renderDelegate->GetCyclesRenderParam()->AddObject(m_cyclesObject);
}

void
HdCyclesPoints::_PopulatePoints(HdSceneDelegate* sceneDelegate, const SdfPath& id, bool& sizeHasChanged) {
    assert(m_cyclesPointCloud);
    sizeHasChanged = false;

    VtValue pointsValue = sceneDelegate->Get(id, HdTokens->points);

    if (pointsValue.IsEmpty()) {
        // Clearing the current point buffer to not display wrong data
        m_cyclesPointCloud->clear();
        TF_WARN("Empty point data for: %s", id.GetText());
        return;
    }

    if (!pointsValue.IsHolding<VtVec3fArray>()) {
        m_cyclesPointCloud->clear();
        TF_WARN("Invalid point data! Can not convert points for: %s", id.GetText());
        return;
    }

    VtVec3fArray points = pointsValue.UncheckedGet<VtVec3fArray>();

    if (points.size() != m_cyclesPointCloud->points.size()) {
        m_cyclesPointCloud->clear();
        m_cyclesPointCloud->resize(points.size());
        sizeHasChanged = true;

        // We set the size of the radius buffers to a default value
        for (size_t i = 0; i < m_cyclesPointCloud->radius.size(); ++i) {
            m_cyclesPointCloud->radius[i] = 1.f;
        }
    } 

    for (size_t i = 0; i < points.size(); ++i) {
        m_cyclesPointCloud->points[i] = vec3f_to_float3(points[i]);
    }
}


void
HdCyclesPoints::_PopulateWidths(HdSceneDelegate* sceneDelegate, const SdfPath& id, const HdInterpolation& interpolation, const VtValue& value_) {
    assert(m_cyclesPointCloud);
    assert(!value_.IsEmpty());

    if (m_cyclesPointCloud->points.empty()) {
        return;        
    }

    if (interpolation != HdInterpolationVertex && interpolation != HdInterpolationConstant) {
        TF_WARN("Point cloud %s has widths has no supported interpolation %s", id.GetText(), _HdInterpolationStr(interpolation));
        return;
    }

    if (!value_.IsHolding<VtFloatArray>()) {
        TF_WARN("Invalid point data! Can not convert widths for: %s", id.GetText());
        return;
    }

    VtFloatArray value = value_.UncheckedGet<VtFloatArray>();

    if (interpolation == HdInterpolationConstant) {
        assert(value.size() == 1);
        for (size_t i = 0; i < m_cyclesPointCloud->points.size(); ++i) {
            m_cyclesPointCloud->radius[i] = value[0] * 0.5f;
        }
    } else if (interpolation == HdInterpolationVertex) {
        assert(value.size() == m_cyclesPointCloud->points.size());
        for (size_t i = 0; i < m_cyclesPointCloud->points.size(); ++i) {
            m_cyclesPointCloud->radius[i] = value[i] * 0.5f;
        }
    }
}

void 
HdCyclesPoints::_PopulateColors(HdSceneDelegate* sceneDelegate, const SdfPath& id, const HdInterpolation& interpolation, const VtValue& value_) {
    assert(!value_.IsEmpty());

    if (interpolation != HdInterpolationVertex && interpolation != HdInterpolationConstant) {
        TF_WARN("Point cloud %s has colors with no supported interpolation %s", id.GetText(), _HdInterpolationStr(interpolation));
        return;
    }

    // If the points have been reset, the attributes are expected to also have been cleared
    ccl::AttributeSet* attributes = &m_cyclesPointCloud->attributes;
    ccl::ustring attrib_name("displayColor");
    ccl::Attribute* attr_C = attributes->find(attrib_name);
    bool reset_opacity = false;
    if (!attr_C) {
        attr_C = attributes->add(attrib_name, ccl::TypeDesc::TypeFloat4, ccl::ATTR_ELEMENT_VERTEX);
        reset_opacity = true;
    }

    auto value = value_.UncheckedGet<VtVec3fArray>();

    ccl::float4* C = attr_C->data_float4();
    if (interpolation == HdInterpolationConstant) {
        assert(value.size() == 1);
        const ccl::float3 v0 = vec3f_to_float3(value[0]);
        for (size_t i = 0; i < m_cyclesPointCloud->points.size(); ++i) {
            C[i].x = v0.x;
            C[i].y = v0.y;
            C[i].z = v0.z;
        }
    } else if (interpolation == HdInterpolationVertex) {
        assert(value.size() == m_cyclesPointCloud->points.size());
        for (size_t i = 0; i < m_cyclesPointCloud->points.size(); ++i) {
            C[i].x = value[i][0];
            C[i].y = value[i][1];
            C[i].z = value[i][2];
        }
    } else {
        assert(false);
    }

    if (reset_opacity) {
        for (size_t i = 0; i < m_cyclesPointCloud->points.size(); ++i) {
            C[i].w = 1.f;
        }
    }
}

/*
    Opacities in USD are separated from colors. If there is a vertex color
    attribute, we associate the alpha with that, otherwise we only set 
    the alpha channel of the color.

    This is because opacities can be read before colors and viceversa,
    once the syncing architecture is more deferred this problem
    won't exist anymore.
*/
void
HdCyclesPoints::_PopulateOpacities(HdSceneDelegate* sceneDelegate, const SdfPath& id, const HdInterpolation& interpolation, const VtValue& value_) {
    assert(m_cyclesPointCloud);
    assert(!value_.IsEmpty());

    if (interpolation != HdInterpolationVertex && interpolation != HdInterpolationConstant) {
        TF_WARN("Point cloud %s has opacities has no supported interpolation %s", id.GetText(), _HdInterpolationStr(interpolation));
        return;
    }

    if (!value_.IsHolding<VtFloatArray>()) {
        TF_WARN("Invalid point data! Can not convert opacities for: %s", id.GetText());
        return;
    }

    VtFloatArray value = value_.UncheckedGet<VtFloatArray>();

    ccl::AttributeSet* attributes = &m_cyclesPointCloud->attributes;
    ccl::ustring attrib_name("displayColor");
    ccl::Attribute* attr_C = attributes->find(attrib_name);
    if (!attr_C) {
        attr_C = attributes->add(attrib_name, ccl::TypeDesc::TypeFloat4, ccl::ATTR_ELEMENT_VERTEX);
    } 

    ccl::float4* C = attr_C->data_float4();
    if (interpolation == HdInterpolationConstant) {
        assert(value.size() == 1);
        for (size_t i = 0; i < m_cyclesPointCloud->points.size(); ++i) {
            C[i].w = value[0]; 
        }
    } else if (interpolation == HdInterpolationVertex) {
        assert(value.size() == m_cyclesPointCloud->points.size());
        for (size_t i = 0; i < m_cyclesPointCloud->points.size(); ++i) {
            C[i].w = value[i];
        }
    } else {
        assert(false);
    }
}

/*
    Setting normals even if the type is not disc oriented in case they need to
    be picked up by some shader.
*/
void
HdCyclesPoints::_PopulateNormals(HdSceneDelegate* sceneDelegate, const SdfPath& id, const HdInterpolation& interpolation, const VtValue& value_) {
    assert(m_cyclesPointCloud);
    assert(!value_.IsEmpty());

    if (interpolation != HdInterpolationVertex && interpolation != HdInterpolationConstant) {
        TF_WARN("Point cloud %s has normals with no supported interpolation %s", id.GetText(), _HdInterpolationStr(interpolation));
        return;
    }

    if (!value_.IsHolding<VtVec3fArray>()) {
        TF_WARN("Invalid normal type for point cloud %s", id.GetText());
        return;
    }
    auto value = value_.UncheckedGet<VtVec3fArray>();

    ccl::Attribute* N_attr = m_cyclesPointCloud->attributes.find(ccl::ATTR_STD_VERTEX_NORMAL);
    if (!N_attr) {
        N_attr = m_cyclesPointCloud->attributes.add(ccl::ATTR_STD_VERTEX_NORMAL);
    }
    ccl::float3* N = N_attr->data_float3();

    if (interpolation == HdInterpolationConstant) {
        assert(value.size() == 1);
        const ccl::float3 N0 = vec3f_to_float3(value[0]);
        for (size_t i = 0; i < m_cyclesPointCloud->points.size(); ++i) {
            N[i] = N0;
        }
    } else if (interpolation == HdInterpolationVertex) {
        assert(value.size() == m_cyclesPointCloud->points.size());
        for (size_t i = 0; i < m_cyclesPointCloud->points.size(); ++i) {
            N[i] = vec3f_to_float3(value[i]);
        }
    } else {
        assert(false);
    }
}


void
HdCyclesPoints::_PopulateVelocities(HdSceneDelegate* sceneDelegate, const SdfPath& id, const HdInterpolation& interpolation, const VtValue& value_) {
    assert(m_cyclesPointCloud);

    // Is motion blur enabled?
    // because of the structure of the rendering code.
    if (!m_useMotionBlur) {
        return;
    }

    if (interpolation != HdInterpolationVertex && interpolation != HdInterpolationConstant) {
        TF_WARN("Point cloud %s has velocities with not supported interpolation %s", id.GetText(), _HdInterpolationStr(interpolation));
        return;
    }

    if (!value_.IsHolding<VtVec3fArray>()) {
        TF_WARN("Invalid normal type for point cloud %s", id.GetText());
        return;
    }
    auto value = value_.UncheckedGet<VtVec3fArray>();

    // Skipping velocities if positions already exist
    // This is safe to check here as the points are a special primvar
    ccl::AttributeSet* attributes = &m_cyclesPointCloud->attributes;
    ccl::Attribute* attr_mP = attributes->find(ccl::ATTR_STD_MOTION_VERTEX_POSITION);
    if (attr_mP) {
        TF_WARN("Velocities will be ignored since motion positions already exist");
        return;
    }

    ccl::Attribute* attr_V = attributes->find(ccl::ATTR_STD_VERTEX_VELOCITY);
    if (!attr_V) {
        attr_V = attributes->add(ccl::ATTR_STD_VERTEX_VELOCITY);
    }

    ccl::float3* V = attr_V->data_float3();
    if (interpolation == HdInterpolationConstant) {
        assert(value.size() == 1);
        const ccl::float3 V0 = vec3f_to_float3(value[0]);
        for (int i = 0; i < m_cyclesPointCloud->points.size(); ++i) {
            V[i] = V0;
        }
    } else if (interpolation == HdInterpolationVertex) {
        assert(value.size() == m_cyclesPointCloud->points.size());
        for (int i = 0; i < m_cyclesPointCloud->points.size(); ++i) {
            V[i] = vec3f_to_float3(value[i]);
        }
    } else {
        assert(false);
    }

    // Enabling motion blur on the geometry
    m_cyclesPointCloud->use_motion_blur = true;
    m_cyclesPointCloud->motion_steps    = HD_CYCLES_MOTION_STEPS;
}

void
HdCyclesPoints::_PopulateAccelerations(HdSceneDelegate* sceneDelegate, const SdfPath& id, const HdInterpolation& interpolation, const VtValue& value_) {
    assert(m_cyclesPointCloud);

    if (!m_useMotionBlur) {
        return;
    }

    if (interpolation != HdInterpolationVertex && interpolation != HdInterpolationConstant) {
        TF_WARN("Point cloud %s has accelerations with not supported interpolation %s", id.GetText(), _HdInterpolationStr(interpolation));
    }

    if (!value_.IsHolding<VtVec3fArray>()) {
        TF_WARN("Invalid normal type for point cloud %s", id.GetText());
        return;
    }
    auto value = value_.UncheckedGet<VtVec3fArray>();

    // Skipping accelerations if positions already exist
    // This is safe to check here as the points are a special primvar
    ccl::AttributeSet* attributes = &m_cyclesPointCloud->attributes;
    ccl::Attribute* attr_mP = attributes->find(ccl::ATTR_STD_MOTION_VERTEX_POSITION);
    if (attr_mP) {
        TF_WARN("Acclerations will be ignored since motion positions already exist");
        return;
    }

    ccl::Attribute* attr_A = attributes->find(ccl::ATTR_STD_VERTEX_ACCELERATION);
    if (!attr_A) {
        attr_A = attributes->add(ccl::ATTR_STD_VERTEX_ACCELERATION);
    }

    ccl::float3* A = attr_A->data_float3();
    if (interpolation == HdInterpolationConstant) {
        assert(value.size() == 1);
        const ccl::float3 A0 = vec3f_to_float3(value[0]);
        for (int i = 0; i < m_cyclesPointCloud->points.size(); ++i) {
            A[i] = A0;
        }
    } else if (interpolation == HdInterpolationVertex) {
        assert(value.size() == m_cyclesPointCloud->points.size());
        for (int i = 0; i < m_cyclesPointCloud->points.size(); ++i) {
            A[i] = vec3f_to_float3(value[i]);
        }
    } else {
        assert(false);
    }

    // Enabling motion blur on the geometry
    m_cyclesPointCloud->use_motion_blur = true;
    m_cyclesPointCloud->motion_steps    = HD_CYCLES_MOTION_STEPS;
}

void
HdCyclesPoints::_UpdateObject(ccl::Scene* scene, HdCyclesRenderParam* param, HdDirtyBits* dirtyBits, bool rebuildBVH) {
    // todo: fix this
    //m_cyclesObject->visibility = _sharedData.visible ? m_visibilityFlags : 0;
    m_cyclesPointCloud->tag_update(scene, rebuildBVH);
    m_cyclesObject->tag_update(scene);

    // todo: check the mesh code that marks the visibility

    // Following the behavior of the mesh converter
    if (!_sharedData.visible) {
        *dirtyBits &= ~HdChangeTracker::DirtyVisibility;
    }

    param->Interrupt();
}

void
HdCyclesPoints::Sync(HdSceneDelegate* sceneDelegate, HdRenderParam* renderParam, HdDirtyBits* dirtyBits,
                     TfToken const& reprSelector)
{
    auto param = dynamic_cast<HdCyclesRenderParam*>(renderParam);

    m_point_display_color_shader = param->default_vcol_display_color_surface;
    assert(m_point_display_color_shader);

    ccl::Scene* scene = param->GetCyclesScene();
    const SdfPath& id = GetId();

    std::lock_guard<std::mutex>(scene->mutex);

    // Triggers only a bvh refit
    bool needsUpdate = false; 
    bool needsRebuildBVH = false;

    // -------------------------------------
    // -- Resolve Drawstyles
#ifdef USE_USD_CYCLES_SCHEMA
    if (HdChangeTracker::IsPrimvarDirty(
            *dirtyBits, id, usdCyclesTokens->cyclesObjectPoint_style)) {

        needsRebuildBVH = true;
        needsUpdate = true;

        HdTimeSampleArray<VtValue, 1> xf;
        sceneDelegate->SamplePrimvar(id, usdCyclesTokens->cyclesObjectPoint_style, &xf);
        if (xf.count > 0) {
            const TfToken& styles = xf.values[0].Get<TfToken>();
            m_pointStyle          = ccl::POINT_CLOUD_POINT_DISC;
            if (styles == usdCyclesTokens->sphere) {
                m_pointStyle = ccl::POINT_CLOUD_POINT_SPHERE;
            }
        }
    }

    // todo: what do we do with PointDPI exactly? check other render delegates
    if (HdChangeTracker::IsPrimvarDirty(
            *dirtyBits, id, usdCyclesTokens->cyclesObjectPoint_resolution)) {

        needsRebuildBVH = true;
        needsUpdate = true;

        HdTimeSampleArray<VtValue, 1> xf;
        sceneDelegate->SamplePrimvar(id, usdCyclesTokens->cyclesObjectPoint_resolution, &xf);
        if (xf.count > 0) {
            const int& resolutions = xf.values[0].Get<int>();
            m_pointResolution      = std::max(resolutions, 10);
        }
    }
#endif

    // Update object flags and exit if visibility is null
    if (*dirtyBits & HdChangeTracker::DirtyVisibility) {
        _sharedData.visible = sceneDelegate->GetVisible(id);
        _UpdateObject(scene, param, dirtyBits, false);
        if (!_sharedData.visible) {
            return;
        }
    }

    if (*dirtyBits & HdChangeTracker::DirtyPrimID) {
        m_cyclesObject->pass_id = this->GetPrimId() + 1;
    }

    if (*dirtyBits & HdChangeTracker::DirtyDoubleSided) {
        TF_WARN("DoubleSided state has changed, but PointCloud is ignoring it.");
    }


#if 0
    if (*dirtyBits & HdChangeTracker::DirtyTransform) {
        ccl::Transform newTransform
                = HdCyclesExtractTransform(sceneDelegate, id);
        m_cyclesObject->tfm = newTransform;
        m_transform         = newTransform;

        needs_update = true;
    }

#endif

    // Checking points separately as they dictate the size of other attribute buffers
    if (*dirtyBits & HdChangeTracker::DirtyPoints) {
        bool sizeHasChanged;
        _PopulatePoints(sceneDelegate, id, sizeHasChanged);
        needsUpdate = true;
        needsRebuildBVH = needsRebuildBVH || sizeHasChanged;
    }

    // Loop through all the other primvars
    std::array<std::pair<HdInterpolation, HdPrimvarDescriptorVector>, 5> primvars_desc {
        std::make_pair(HdInterpolationConstant, HdPrimvarDescriptorVector {}),
        std::make_pair(HdInterpolationUniform, HdPrimvarDescriptorVector {}),
        std::make_pair(HdInterpolationVertex, HdPrimvarDescriptorVector {}),
        std::make_pair(HdInterpolationVarying, HdPrimvarDescriptorVector {}),
        std::make_pair(HdInterpolationFaceVarying, HdPrimvarDescriptorVector {}),
    };

    for (auto& info : primvars_desc) {
        info.second = sceneDelegate->GetPrimvarDescriptors(id, info.first);
    }

    for (auto& interpolation_description : primvars_desc) {
        for (const HdPrimvarDescriptor& description : interpolation_description.second) {
            if (description.name == HdTokens->points) {
                continue;
            }

            if (!HdChangeTracker::IsPrimvarDirty(*dirtyBits, id, description.name)) {
                continue;
            }

            std::cout << "Primvar " << description.name << std::endl;

            auto interpolation = interpolation_description.first;
            auto value         = GetPrimvar(sceneDelegate, description.name);

            if (value.IsEmpty()) {
                TF_WARN("Primvar %s is empty with interpolation %s", description.name, _HdInterpolationStr(interpolation));
                continue;
            }

            if (description.name == HdTokens->widths) {
                _PopulateWidths(sceneDelegate, id, interpolation, value);
            } else if (description.name == HdTokens->normals) {
                _PopulateNormals(sceneDelegate, id, interpolation, value);
            } else if (description.name == HdTokens->displayColor || description.role == HdPrimvarRoleTokens->color) {
                _PopulateColors(sceneDelegate, id, interpolation, value);
            } else if (description.name == HdTokens->displayOpacity) {
                _PopulateOpacities(sceneDelegate, id, interpolation, value);
            } else if (description.name == HdTokens->velocities) {
                _PopulateVelocities(sceneDelegate, id, interpolation, value);
            } else if (description.name == HdTokens->accelerations) {
                _PopulateAccelerations(sceneDelegate, id, interpolation, value);
            }
        }
    }

#if 1
    auto N_attr = m_cyclesPointCloud->attributes.add(ccl::ATTR_STD_VERTEX_NORMAL);
    ccl::float3* N = N_attr->data_float3();

    srand(125125125);
    for (size_t i = 0; i < m_cyclesPointCloud->points.size(); ++i) {
        N[i].x = (float)rand() / RAND_MAX;
        N[i].y = (float)rand() / RAND_MAX;
        N[i].z = (float)rand() / RAND_MAX;
        N[i] = ccl::normalize(N[i]);
    }

    m_cyclesPointCloud->point_style = ccl::POINT_CLOUD_POINT_DISC_ORIENTED;

#endif

    //m_cyclesPointCloud->point_style = ccl::POINT_CLOUD_POINT_DISC;

    _CheckIntegrity(param);


     if (needsUpdate) {
        _UpdateObject(scene, param, dirtyBits);
    }   
    *dirtyBits = HdChangeTracker::Clean;
}

HdDirtyBits
HdCyclesPoints::GetInitialDirtyBitsMask() const
{
    return HdChangeTracker::DirtyPoints | HdChangeTracker::DirtyTransform | HdChangeTracker::DirtyVisibility
           | HdChangeTracker::DirtyPrimvar | HdChangeTracker::DirtyWidths | HdChangeTracker::DirtyMaterialId
           | HdChangeTracker::DirtyInstanceIndex | HdChangeTracker::DirtyNormals;
}

bool
HdCyclesPoints::IsValid() const
{
    return true;
}

void
HdCyclesPoints::_CheckIntegrity(HdCyclesRenderParam* param) {
    assert(m_cyclesPointCloud);
    assert(m_cyclesPointCloud->points.size() == m_cyclesPointCloud->radius.size());

    // Oriented point style requires normals
    if (m_cyclesPointCloud->point_style == ccl::POINT_CLOUD_POINT_DISC_ORIENTED) {
        ccl::Attribute* attr = m_cyclesPointCloud->attributes.find(ccl::ATTR_STD_VERTEX_NORMAL);
        if (!attr) {
            TF_WARN("Point cloud has style DISC_ORIENTED but no normals are present. Reverting to DISC");
            m_cyclesPointCloud->point_style = ccl::POINT_CLOUD_POINT_DISC;
        }
    }

    // Assigning a default material to the point cloud if none is present
    if (m_cyclesPointCloud->used_shaders.empty()) {
        m_cyclesPointCloud->used_shaders.push_back(param->default_vcol_display_color_surface);

        // If no colors are present we also set a beautiful magenta
        ccl::ustring attr_color_name("displayColor");
        ccl::Attribute* attr_colors = m_cyclesPointCloud->attributes.find(attr_color_name);
        if (!attr_colors) {
            attr_colors = m_cyclesPointCloud->attributes.add(attr_color_name, ccl::TypeDesc::TypeFloat4, ccl::ATTR_ELEMENT_VERTEX);
            ccl::float4* colors = attr_colors->data_float4();

            for (size_t i = 0; i < m_cyclesPointCloud->points.size(); ++i) {
                colors[i] = ccl::make_float4(1.f, 0.f, 1.f, 1.f);
            }
        }
    }

    {
        ccl::Attribute* attr_mP = m_cyclesPointCloud->attributes.find(ccl::ATTR_STD_MOTION_VERTEX_POSITION);
        ccl::Attribute* attr_V = m_cyclesPointCloud->attributes.find(ccl::ATTR_STD_VERTEX_VELOCITY);
        ccl::Attribute* attr_A = m_cyclesPointCloud->attributes.find(ccl::ATTR_STD_VERTEX_ACCELERATION);
        if (attr_mP) {
            assert(!attr_V && !attr_A);
        }
    }
}

PXR_NAMESPACE_CLOSE_SCOPE
