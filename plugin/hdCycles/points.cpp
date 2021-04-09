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

// todo: remove
#include <random>

PXR_NAMESPACE_OPEN_SCOPE

HdCyclesPoints::HdCyclesPoints(SdfPath const& id, SdfPath const& instancerId,
                               HdCyclesRenderDelegate* a_renderDelegate)
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
    m_cyclesPointCloud->point_style = (ccl::PointCloudPointStyle)m_pointStyle;
    m_renderDelegate->GetCyclesRenderParam()->AddGeometry(m_cyclesPointCloud);

    m_cyclesObject             = new ccl::Object();
    m_cyclesObject->geometry   = m_cyclesPointCloud;
    m_cyclesObject->tfm        = ccl::transform_identity();
    m_cyclesObject->pass_id    = -1;
    m_cyclesObject->visibility = ccl::PATH_RAY_ALL_VISIBILITY;
    m_renderDelegate->GetCyclesRenderParam()->AddObject(m_cyclesObject);
}

void
HdCyclesPoints::_PopulatePoints(HdSceneDelegate* sceneDelegate, const SdfPath& id) {
    VtValue pointsValue = sceneDelegate->Get(id, HdTokens->points);

    if (pointsValue.IsEmpty()) {
        TF_WARN("Empty point data for: %s", id.GetText());
        return;
    }

    if (!pointsValue.IsHolding<VtVec3fArray>()) {
        TF_WARN("Invalid point data! Can not convert points for: %s", id.GetText());
        return;
    }

    VtVec3fArray points = pointsValue.UncheckedGet<VtVec3fArray>();

    printf("Initializing point cloud %d\n", (int)points.size());

    assert(m_cyclesPointCloud);
    m_cyclesPointCloud->clear();
    m_cyclesPointCloud->resize(points.size());
    for (size_t i = 0; i < points.size(); ++i) {
        m_cyclesPointCloud->points[i] = vec3f_to_float3(points[i]);
    }
}

void
HdCyclesPoints::_PopulateScales(HdSceneDelegate* sceneDelegate, const SdfPath& id) {
    VtValue widthsValue = sceneDelegate->Get(id, HdTokens->widths);

    if (widthsValue.IsEmpty()) {
        TF_WARN("Empty widths data for: %s", id.GetText());
        return;
    }

    if (!widthsValue.IsHolding<VtFloatArray>()) {
        TF_WARN("Invalid point data! Can not convert points for: %s", id.GetText());
        return;
    }

    VtFloatArray widths = widthsValue.UncheckedGet<VtFloatArray>();

    assert(m_cyclesPointCloud);

    if (widths.size() != m_cyclesPointCloud->points.size()) {
        TF_WARN("Unexpected number of widths %d with %d points", (int)widths.size(), 
            (int)m_cyclesPointCloud->points.size());
        return;
    }

    // TODO: It's likely that this can cause double transforms due to modifying the core transform
    HdTimeSampleArray<VtValue, 2> radius_steps;
    sceneDelegate->SamplePrimvar(id, HdTokens->widths, &radius_steps);
    if (radius_steps.count > 0) {
        const VtFloatArray& radius
            = radius_steps.values[0].Get<VtFloatArray>();

        // The more correct way to figure out the interpolation
        // would be to get a primvar descriptor array but it requires
        // refactoring the sync into a loop.
        assert(m_cyclesPointCloud->points.size()
               == m_cyclesPointCloud->radius.size());
        if (radius.size() == 1) {
            for (size_t i = 0; i < m_cyclesPointCloud->points.size();
                 ++i) {
                m_cyclesPointCloud->radius[i] = radius[0] * 0.5f;
            }
        } else if (radius.size() == m_cyclesPointCloud->points.size()) {
            for (size_t i = 0; i < m_cyclesPointCloud->points.size();
                 ++i) {
                m_cyclesPointCloud->radius[i] = radius[i] * 0.5f;
            }
        } else {
            std::cout
                << "Unknown interpolation type for pointcloud. Have "
                << m_cyclesPointCloud->points.size()
                << " points but primvar has size " << radius.size()
                << std::endl;
        }
    }
}

void
HdCyclesPoints::_PopulatePrimvars(const HdDirtyBits* dirtyBits, HdSceneDelegate* sceneDelegate, HdCyclesRenderParam* param, const SdfPath& id) {
    // Add velocities
    if (HdChangeTracker::IsPrimvarDirty(*dirtyBits, id,
                                        HdTokens->velocities)) {
        VtValue velocitiesValue = sceneDelegate->Get(id,
                                                     HdTokens->velocities);
        if (velocitiesValue.IsHolding<VtVec3fArray>()) {
            const VtVec3fArray& velocities
                = velocitiesValue.UncheckedGet<VtVec3fArray>();
            _AddVelocities(velocities);
        } else {
            TF_WARN("Unexpected type for points velocities");
        }
    }

    // Add accelerations

    if (HdChangeTracker::IsPrimvarDirty(*dirtyBits, id,
                                        HdTokens->accelerations)) {
        VtValue accelerationsValue
            = sceneDelegate->Get(id, HdTokens->accelerations);
        if (accelerationsValue.IsHolding<VtVec3fArray>()) {
            const VtVec3fArray& accelerations
                = accelerationsValue.UncheckedGet<VtVec3fArray>();
            //_AddAccelerations(accelerations);
        } else {
            TF_WARN("Unexpected type for points accelerations");
        }
    }

    // Add colors

    if (HdChangeTracker::IsPrimvarDirty(*dirtyBits, id,
                                        HdTokens->displayColor)) {
        VtValue colorsValue = sceneDelegate->Get(id, HdTokens->displayColor);
        if (colorsValue.IsHolding<VtVec3fArray>()) {
            const VtVec3fArray& colors
                = colorsValue.UncheckedGet<VtVec3fArray>();
            _AddColors(colors, param);
        } else {
            TF_WARN("Unexpected type for points colors");
        }
    }

    // Add opacities

    if (HdChangeTracker::IsPrimvarDirty(*dirtyBits, id, HdTokens->displayOpacity)) {
        VtValue alphasValue = sceneDelegate->Get(id, HdTokens->displayOpacity);
        if (alphasValue.IsHolding<VtFloatArray>()) {
            const VtFloatArray& alphas
            = alphasValue.UncheckedGet<VtFloatArray>();
            _AddAlphas(alphas);
        } else {
            TF_WARN("Unexpected type for points alphas");
        }
    }
}

void
HdCyclesPoints::_UpdateObject(ccl::Scene* scene, HdCyclesRenderParam* param, HdDirtyBits* dirtyBits) {
    // todo: fix this
    //m_cyclesObject->visibility = _sharedData.visible ? m_visibilityFlags : 0;
    m_cyclesObject->tag_update(scene);

    // todo: check the mesh code that marks the visibility

    param->Interrupt();
}

// todo remove:
static unsigned long x=123456789, y=362436069, z=521288629;
unsigned long xorshf96(void) {          //period 2^96-1
unsigned long t;
    x ^= x << 16;
    x ^= x >> 5;
    x ^= x << 1;

   t = x;
   x = y;
   y = z;
   z = t ^ x ^ y;

  return z;
}
#define RAND_FLT ((float)(xorshf96()) / std::numeric_limits<unsigned long>::max())

void
HdCyclesPoints::Sync(HdSceneDelegate* sceneDelegate, HdRenderParam* renderParam,
                     HdDirtyBits* dirtyBits, TfToken const& reprSelector)
{
    HdCyclesRenderParam* param = dynamic_cast<HdCyclesRenderParam*>(renderParam);

    ccl::Scene* scene = param->GetCyclesScene();
    const SdfPath& id = GetId();

    std::lock_guard<std::mutex>(scene->mutex);

    //HdCyclesPDPIMap pdpi;

    bool needs_update  = false;
    bool needs_newMesh = true;

    // USD Cycles

#ifdef USE_USD_CYCLES_SCHEMA

    if (HdChangeTracker::IsPrimvarDirty(
            *dirtyBits, id, usdCyclesTokens->cyclesObjectPoint_style)) {
        needs_newMesh = true;

        HdTimeSampleArray<VtValue, 1> xf;
        sceneDelegate->SamplePrimvar(id,
                                     usdCyclesTokens->cyclesObjectPoint_style,
                                     &xf);
        if (xf.count > 0) {
            const TfToken& styles = xf.values[0].Get<TfToken>();
            m_pointStyle          = ccl::POINT_CLOUD_POINT_DISC;
            if (styles == usdCyclesTokens->sphere) {
                m_pointStyle = ccl::POINT_CLOUD_POINT_SPHERE;
            }
        }
    }

    // todo: remove this once the pointcloud implementation is finished
    if (HdChangeTracker::IsPrimvarDirty(
            *dirtyBits, id, usdCyclesTokens->cyclesObjectPoint_resolution)) {
        needs_newMesh = true;

        HdTimeSampleArray<VtValue, 1> xf;
        sceneDelegate->SamplePrimvar(
            id, usdCyclesTokens->cyclesObjectPoint_resolution, &xf);
        if (xf.count > 0) {
            const int& resolutions = xf.values[0].Get<int>();
            m_pointResolution      = std::max(resolutions, 10);
        }
    }

#endif

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
            std::cout << "Primvar " << description.name << " Interpolation " << interpolation_description.first << std::endl;
        }
    }


    if (*dirtyBits & HdChangeTracker::DirtyVisibility) {
        needs_update = true;

#if 0
        bool visible = sceneDelegate->GetVisible(id);
        for (int i = 0; i < m_cyclesObjects.size(); i++) {
            if (visible) {
                m_cyclesObjects[i]->visibility
                    |= ccl::PATH_RAY_ALL_VISIBILITY;
            } else {
                m_cyclesObjects[i]->visibility
                    &= ~ccl::PATH_RAY_ALL_VISIBILITY;
            }
        }
#endif
    }

    if (*dirtyBits & HdChangeTracker::DirtyPoints) {
        _PopulatePoints(sceneDelegate, id);
        needs_update = true;
    }

    if (*dirtyBits & HdChangeTracker::DirtyWidths) {
        _PopulateScales(sceneDelegate, id);
        needs_update = true;
    }

    if (*dirtyBits & HdChangeTracker::DirtyPrimvar) {
        _PopulatePrimvars(dirtyBits, sceneDelegate, param, id);
        needs_update = true;
    }

    if (*dirtyBits & HdChangeTracker::DirtyPrimID) {
        m_cyclesObject->pass_id = this->GetPrimId() + 1;
    }

    if (*dirtyBits & HdChangeTracker::DirtyTransform) {
        ccl::Transform newTransform
                = HdCyclesExtractTransform(sceneDelegate, id);
        m_cyclesObject->tfm = newTransform;
        m_transform         = newTransform;

        needs_update = true;
    }

    if (needs_update) {
        _UpdateObject(scene, param, dirtyBits);
        param->Interrupt();
    }

    *dirtyBits = HdChangeTracker::Clean;

    m_cyclesPointCloud->point_style = ccl::POINT_CLOUD_POINT_SPHERE;

#if 1
    size_t n_particles = 250000000;
    ccl::float3 bb_min = ccl::make_float3(-3.f, -3.f, -3.f);
    ccl::float3 bb_max = ccl::make_float3(3.f, 3.f, 3.f);
    printf("Generating %llu particles\n", n_particles);

    m_cyclesPointCloud->clear();
    m_cyclesPointCloud->resize(n_particles);
    for (size_t i = 0; i < n_particles; ++i) {
        ccl::float3 pos;
        pos.x = RAND_FLT;
        pos.y = RAND_FLT;
        pos.z = RAND_FLT;
        pos = bb_min + (bb_max - bb_min) * pos;
        m_cyclesPointCloud->points[i] = pos;
        m_cyclesPointCloud->radius[i] = ((float)rand() / RAND_MAX) * 0.01f;
    }

    ccl::Attribute* attr_C = m_cyclesPointCloud->attributes.add(ccl::ustring("displayColor"), ccl::TypeDesc::TypeFloat4, ccl::ATTR_ELEMENT_VERTEX);
    ccl::float4* C = attr_C->data_float4();
    for (size_t i = 0; i < n_particles; ++i) {
        C[i].x = (float)(i % 500) / 500;
        C[i].y = (float)(i % 1242) / 5125;
        C[i].z = (float)(i % 661231) / 12516;
        C[i].w = 1.f;
    }

    if (m_cyclesPointCloud->used_shaders.empty()) {
        m_cyclesPointCloud->used_shaders.push_back(param->default_vcol_display_color_surface);
    }

    printf("Finished generating\n");
#endif

#if 0
    ccl::AttributeSet& attributes = m_cyclesPointCloud->attributes;
    ccl::Attribute* normals = attributes.find(ccl::ATTR_STD_VERTEX_NORMAL);
    if (normals) {
        attributes.remove(ccl::ATTR_STD_VERTEX_NORMAL);
    }
    normals = attributes.add(ccl::ATTR_STD_VERTEX_NORMAL);
    ccl::float3* N = normals->data_float3();
    srand(12312152125);
    for (size_t i = 0; i < m_cyclesPointCloud->points.size(); ++i) {
        N[i].x = (float)rand() / RAND_MAX;
        N[i].y = (float)rand() / RAND_MAX;
        N[i].z = (float)rand() / RAND_MAX;
        N[i] = ccl::normalize(N[i]);
    }
#endif


#if 0
    const bool needToUpdatePoints = (*dirtyBits & HdChangeTracker::DirtyPoints)
                                    || needs_newMesh;


    if (needToUpdatePoints
        && m_pointStyle == HdCyclesPointStyle::POINT_SPHERES) {
        needs_update = true;

        m_cyclesPointCloud->clear();
        m_cyclesPointCloud->tag_update(scene, true);
        m_cyclesObject->tag_update(scene);



        


        if (m_cyclesPointCloud->used_shaders.empty()) {
            m_cyclesPointCloud->used_shaders.push_back(param->default_vcol_surface);
        } else if (m_cyclesPointCloud->used_shaders[0] == scene->default_surface) {
            
        }

    } else if (needToUpdatePoints) {
        needs_update = true;

        m_cyclesMesh->clear();

        if (m_pointStyle == HdCyclesPointStyle::POINT_DISCS) {
            _CreateDiscMesh();
        } else {
            _CreateSphereMesh();
        }

        m_cyclesMesh->tag_update(scene, true);

        // Positions

        const auto pointsValue = sceneDelegate->Get(id, HdTokens->points);
        if (!pointsValue.IsEmpty() && pointsValue.IsHolding<VtVec3fArray>()) {
            const VtVec3fArray& points = pointsValue.Get<VtVec3fArray>();

            for (size_t i = 0; i < m_cyclesObjects.size(); i++) {
                param->RemoveObject(m_cyclesObjects[i]);
            }

            m_cyclesObjects.clear();

            for (size_t i = 0; i < points.size(); i++) {
                ccl::Object* pointObject = _CreatePointsObject(
                    ccl::transform_translate(vec3f_to_float3(points[i])),
                    m_cyclesMesh);

                pointObject->random_id = i;
                pointObject->name
                    = ccl::ustring::format("%s@%08x", pointObject->name,
                                           pointObject->random_id);
                m_cyclesObjects.push_back(pointObject);
                param->AddObject(pointObject);
            }
        }

        // Transforms

        if (*dirtyBits & HdChangeTracker::DirtyTransform) {
            ccl::Transform newTransform
                = HdCyclesExtractTransform(sceneDelegate, id);

            for (int i = 0; i < m_cyclesObjects.size(); i++) {
                m_cyclesObjects[i]->tfm = ccl::transform_inverse(m_transform)
                                          * m_cyclesObjects[i]->tfm;
                m_cyclesObjects[i]->tfm = newTransform
                                          * m_cyclesObjects[i]->tfm;
            }

            m_transform = newTransform;

            needs_update = true;
        }

        // Widths

        // TODO: It's likely that this can cause double transforms due to modifying the core transform
        if (HdChangeTracker::IsPrimvarDirty(*dirtyBits, id, HdTokens->widths)) {
            needs_update = true;

            if (m_cyclesObjects.size() > 0) {
                HdTimeSampleArray<VtValue, 2> xf;
                sceneDelegate->SamplePrimvar(id, HdTokens->widths, &xf);
                if (xf.count > 0) {
                    const VtFloatArray& widths
                        = xf.values[0].Get<VtFloatArray>();
                    for (int i = 0; i < widths.size(); i++) {
                        if (i < m_cyclesObjects.size()) {
                            float w = widths[i];
                            m_cyclesObjects[i]->tfm
                                = m_cyclesObjects[i]->tfm
                                  * ccl::transform_scale(w, w, w);
                        }
                    }
                }
            }
        }

        if (HdChangeTracker::IsPrimvarDirty(*dirtyBits, id, HdTokens->normals)) {
            needs_update = true;

            if (m_cyclesObjects.size() > 0) {
                HdTimeSampleArray<VtValue, 1> xf;
                sceneDelegate->SamplePrimvar(id, HdTokens->normals, &xf);
                if (xf.count > 0) {
                    const VtVec3fArray& normals
                        = xf.values[0].Get<VtVec3fArray>();
                    for (int i = 0; i < normals.size(); i++) {
                        if (i < m_cyclesObjects.size()) {
                            ccl::float3 rotAxis
                                = ccl::cross(ccl::make_float3(0.0f, 0.0f, 1.0f),
                                             ccl::make_float3(normals[i][0],
                                                              normals[i][1],
                                                              normals[i][2]));
                            float d
                                = ccl::dot(ccl::make_float3(0.0f, 0.0f, 1.0f),
                                           ccl::make_float3(normals[i][0],
                                                            normals[i][1],
                                                            normals[i][2]));
                            float angle = atan2f(ccl::len(rotAxis), d);
                            m_cyclesObjects[i]->tfm
                                = m_cyclesObjects[i]->tfm
                                  * ccl::transform_rotate((angle), rotAxis);
                        }
                    }
                } else {
                    // handle orient to camera
                }
            }
        }
    }
#endif
}

HdDirtyBits
HdCyclesPoints::GetInitialDirtyBitsMask() const
{
    return HdChangeTracker::DirtyPoints | HdChangeTracker::DirtyTransform
           | HdChangeTracker::DirtyVisibility | HdChangeTracker::DirtyPrimvar
           | HdChangeTracker::DirtyWidths | HdChangeTracker::DirtyMaterialId
           | HdChangeTracker::DirtyInstanceIndex
           | HdChangeTracker::DirtyNormals;
}

bool
HdCyclesPoints::IsValid() const
{
    return true;
}

bool
HdCyclesPoints::_usingPointCloud() const
{
    return m_cyclesPointCloud != nullptr && m_cyclesObject != nullptr;
}

ccl::Object*
HdCyclesPoints::_CreatePointsObject(const ccl::Transform& transform,
                                    ccl::Mesh* mesh)
{
    /* create object*/
    ccl::Object* object = new ccl::Object();
    object->geometry    = mesh;
    object->tfm         = transform;
    object->motion.clear();

    return object;
}

void
HdCyclesPoints::_AddVelocities(const VtVec3fArray& velocities)
{
    if (_usingPointCloud()) {
        ccl::AttributeSet* attributes = &m_cyclesPointCloud->attributes;

        // If motion positions have been authored, let's not waste memory
        ccl::Attribute* attr_mP = attributes->find(
            ccl::ATTR_STD_MOTION_VERTEX_POSITION);
        if (attr_mP) {
            TF_WARN("Velocities will be ignored since motion positions exist");
            return;
        }

        ccl::Attribute* attr_V = attributes->find(
            ccl::ATTR_STD_VERTEX_VELOCITY);

        if (!attr_V) {
            attr_V = attributes->add(ccl::ATTR_STD_VERTEX_VELOCITY);
        }

        ccl::float3* V = attr_V->data_float3();
        if (velocities.size() == 1) {
            for (int i = 0; i < m_cyclesPointCloud->points.size(); ++i) {
                V[i] = vec3f_to_float3(velocities[0]);
            }
        } else if (velocities.size() == m_cyclesPointCloud->points.size()) {
            for (int i = 0; i < m_cyclesPointCloud->points.size(); ++i) {
                V[i] = vec3f_to_float3(velocities[i]);
            }
        } else {
            TF_WARN("Unsupported interpolation type for velocities");
            return;
        }

        if (m_useMotionBlur) {
            m_cyclesPointCloud->use_motion_blur = true;
            m_cyclesPointCloud->motion_steps    = 3;
        }
    }
}

void
HdCyclesPoints::_AddAccelerations(const VtVec3fArray& accelerations)
{
    if (_usingPointCloud()) {
        ccl::AttributeSet* attributes = &m_cyclesPointCloud->attributes;

        // If motion positions have been authored, let's not waste memory
        ccl::Attribute* attr_mP = attributes->find(
            ccl::ATTR_STD_MOTION_VERTEX_POSITION);
        if (attr_mP) {
            TF_WARN(
                "Accelerations will be ignored since motion positions exist");
            return;
        }

        ccl::Attribute* attr_A = attributes->find(
            ccl::ATTR_STD_VERTEX_ACCELERATION);
        if (!attr_A) {
            attr_A = attributes->add(ccl::ATTR_STD_VERTEX_ACCELERATION);
        }

        ccl::float3* A = attr_A->data_float3();
        if (accelerations.size() == 1) {
            const ccl::float3 A0 = vec3f_to_float3(accelerations[0]);
            for (int i = 0; i < m_cyclesPointCloud->points.size(); ++i) {
                A[i] = A0;
            }
        } else if (accelerations.size() == m_cyclesPointCloud->points.size()) {
            for (int i = 0; i < m_cyclesPointCloud->points.size(); ++i) {
                A[i] = vec3f_to_float3(accelerations[i]);
            }
        } else {
            TF_WARN("Unsupported interpolation type for accelerations");
        }
    }
}

void
HdCyclesPoints::_AddColors(const VtVec3fArray& colors, HdCyclesRenderParam* param) {
    if (_usingPointCloud()) {
        ccl::AttributeSet* attributes = &m_cyclesPointCloud->attributes;

        ccl::Attribute* attr_C = attributes->find(
            ccl::ATTR_STD_VERTEX_COLOR);
        if (!attr_C) {
            ccl::ustring attrib_name("displayColor");
            attr_C = attributes->add(attrib_name, ccl::TypeDesc::TypeFloat4, ccl::ATTR_ELEMENT_VERTEX);
        }

        ccl::float4* C = attr_C->data_float4();
        if (colors.size() == 1) {
            const ccl::float3 C0 = vec3f_to_float3(colors[0]);
            for (int i = 0; i < m_cyclesPointCloud->points.size(); ++i) {
                C[i].x = C0.x;
                C[i].y = C0.y;
                C[i].z = C0.z;
                C[i].w = 0.f;
            }
        } else if (colors.size() == m_cyclesPointCloud->points.size()) {
            for (int i = 0; i < m_cyclesPointCloud->points.size(); ++i) {
                C[i].x = colors[i][0];
                C[i].y = colors[i][1];
                C[i].z = colors[i][2];
                C[i].w = 1.f;
            }
        } else {
            TF_WARN("Unexpcted number of vertex colors\n");
        }

        if (m_cyclesPointCloud->used_shaders.empty()) {
            m_cyclesPointCloud->used_shaders.push_back(param->default_vcol_display_color_surface);
        }
    }
}

void
HdCyclesPoints::_AddAlphas(const VtFloatArray& alphas) {
    if (_usingPointCloud()) {
        ccl::AttributeSet* attributes = &m_cyclesPointCloud->attributes;

        ccl::ustring attrib_name("displayColor");
        ccl::Attribute* attr_A = attributes->find(attrib_name);
        ccl::float4* A = attr_A->data_float4();

        for (int i = 0; i < m_cyclesPointCloud->points.size(); ++i) {
            A[i].w = alphas[i];
        }
    }
}

PXR_NAMESPACE_CLOSE_SCOPE
