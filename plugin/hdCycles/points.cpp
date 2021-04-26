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

    m_cyclesMesh = new ccl::Mesh();
    m_renderDelegate->GetCyclesRenderParam()->AddGeometry(m_cyclesMesh);
}

HdCyclesPoints::~HdCyclesPoints()
{
    // Remove points
    for (size_t i = 0; i < m_cyclesObjects.size(); i++) {
        m_renderDelegate->GetCyclesRenderParam()->RemoveObject(m_cyclesObjects[i]);
    }

    m_cyclesObjects.clear();

    // Remove mesh

    m_renderDelegate->GetCyclesRenderParam()->RemoveMesh(m_cyclesMesh);

    delete m_cyclesMesh;
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
HdCyclesPoints::Sync(HdSceneDelegate* sceneDelegate, HdRenderParam* renderParam, HdDirtyBits* dirtyBits,
                     TfToken const& reprSelector)
{
    HdCyclesRenderParam* param = static_cast<HdCyclesRenderParam*>(renderParam);

    const SdfPath& id = GetId();

    HdCyclesPDPIMap pdpi;

    ccl::Scene* scene = param->GetCyclesScene();

    bool needs_update  = false;
    bool needs_newMesh = true;

    // Read Cycles Primvars

#ifdef USE_USD_CYCLES_SCHEMA

    if (HdChangeTracker::IsPrimvarDirty(*dirtyBits, id, usdCyclesTokens->cyclesObjectPoint_style)) {
        needs_newMesh = true;

        HdTimeSampleArray<VtValue, 1> xf;
        sceneDelegate->SamplePrimvar(id, usdCyclesTokens->cyclesObjectPoint_style, &xf);
        if (xf.count > 0) {
            const TfToken& styles = xf.values[0].Get<TfToken>();
            m_pointStyle          = POINT_DISCS;
            if (styles == usdCyclesTokens->sphere) {
                m_pointStyle = POINT_SPHERES;
            }
        }
    }

    if (HdChangeTracker::IsPrimvarDirty(*dirtyBits, id, usdCyclesTokens->cyclesObjectPoint_resolution)) {
        needs_newMesh = true;

        HdTimeSampleArray<VtValue, 1> xf;
        sceneDelegate->SamplePrimvar(id, usdCyclesTokens->cyclesObjectPoint_resolution, &xf);
        if (xf.count > 0) {
            const int& resolutions = xf.values[0].Get<int>();
            m_pointResolution      = std::max(resolutions, 10);
        }
    }

#endif

    // Create Points

    if (*dirtyBits & HdChangeTracker::DirtyPoints || needs_newMesh) {
        needs_update = true;

        m_cyclesMesh->clear();

        if (m_pointStyle == HdCyclesPointStyle::POINT_DISCS) {
            _CreateDiscMesh();
        } else {
            _CreateSphereMesh();
        }

        m_cyclesMesh->tag_update(scene, true);

        const auto pointsValue = sceneDelegate->Get(id, HdTokens->points);
        if (!pointsValue.IsEmpty() && pointsValue.IsHolding<VtVec3fArray>()) {
            const VtVec3fArray& points = pointsValue.Get<VtVec3fArray>();

            for (size_t i = 0; i < m_cyclesObjects.size(); i++) {
                param->RemoveObject(m_cyclesObjects[i]);
            }

            m_cyclesObjects.clear();

            for (size_t i = 0; i < points.size(); i++) {
                ccl::Object* pointObject = _CreatePointsObject(ccl::transform_translate(vec3f_to_float3(points[i])),
                                                               m_cyclesMesh);

                pointObject->set_random_id(i);
                pointObject->name = ccl::ustring::format("%s@%08x", pointObject->name, pointObject->get_random_id());
                m_cyclesObjects.push_back(pointObject);
                param->AddObject(pointObject);
            }
        }
    }

    if (*dirtyBits & HdChangeTracker::DirtyTransform) {
        ccl::Transform newTransform = HdCyclesExtractTransform(sceneDelegate, id);

        for (size_t i = 0; i < m_cyclesObjects.size(); i++) {
            m_cyclesObjects[i]->set_tfm(ccl::transform_inverse(m_transform) * m_cyclesObjects[i]->get_tfm());
            m_cyclesObjects[i]->set_tfm(newTransform * m_cyclesObjects[i]->get_tfm());
        }

        m_transform = newTransform;

        needs_update = true;
    }

    // TODO: It's likely that this can cause double transforms due to modifying the core transform
    if (HdChangeTracker::IsPrimvarDirty(*dirtyBits, id, HdTokens->widths)) {
        needs_update = true;

        if (m_cyclesObjects.size() > 0) {
            HdTimeSampleArray<VtValue, 2> xf;
            sceneDelegate->SamplePrimvar(id, HdTokens->widths, &xf);
            if (xf.count > 0) {
                const VtFloatArray& widths = xf.values[0].Get<VtFloatArray>();
                for (size_t i = 0; i < widths.size(); i++) {
                    if (i < m_cyclesObjects.size()) {
                        float w = widths[i];
                        m_cyclesObjects[i]->set_tfm(m_cyclesObjects[i]->get_tfm() * ccl::transform_scale(w, w, w));
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
                const VtVec3fArray& normals = xf.values[0].Get<VtVec3fArray>();
                for (size_t i = 0; i < normals.size(); i++) {
                    if (i < m_cyclesObjects.size()) {
                        ccl::float3 rotAxis = ccl::cross(ccl::make_float3(0.0f, 0.0f, 1.0f),
                                                         ccl::make_float3(normals[i][0], normals[i][1], normals[i][2]));
                        float d             = ccl::dot(ccl::make_float3(0.0f, 0.0f, 1.0f),
                                           ccl::make_float3(normals[i][0], normals[i][1], normals[i][2]));
                        float angle         = atan2f(ccl::len(rotAxis), d);
                        m_cyclesObjects[i]->set_tfm(m_cyclesObjects[i]->get_tfm()
                                                    * ccl::transform_rotate((angle), rotAxis));
                    }
                }
            } else {
                // handle orient to camera
            }
        }
    }

    if (*dirtyBits & HdChangeTracker::DirtyVisibility) {
        needs_update = true;

        bool visible = sceneDelegate->GetVisible(id);
        for (size_t i = 0; i < m_cyclesObjects.size(); i++) {
            if (visible) {
                m_cyclesObjects[i]->set_visibility(m_cyclesObjects[i]->get_visibility() | ccl::PATH_RAY_ALL_VISIBILITY);
            } else {
                m_cyclesObjects[i]->set_visibility(m_cyclesObjects[i]->get_visibility()
                                                   & ~ccl::PATH_RAY_ALL_VISIBILITY);
            }
        }
    }

    if (needs_update)
        param->Interrupt();

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

// Creates z up disc
void
HdCyclesPoints::_CreateDiscMesh()
{
    m_cyclesMesh->clear();
    m_cyclesMesh->name = ccl::ustring("generated_disc");
    m_cyclesMesh->set_subdivision_type(ccl::Mesh::SUBDIVISION_NONE);

    int numVerts = m_pointResolution;
    int numFaces = m_pointResolution - 2;

    m_cyclesMesh->reserve_mesh(numVerts, numFaces);

    for (int i = 0; i < m_pointResolution; i++) {
        float d = (static_cast<float>(i) / static_cast<float>(m_pointResolution)) * 2.0f * M_PI_F;
        float x = sin(d) * 0.5f;
        float y = cos(d) * 0.5f;
        m_cyclesMesh->add_vertex(ccl::make_float3(x, y, 0.0f));
    }

    for (int i = 1; i < m_pointResolution - 1; i++) {
        int v0 = 0;
        int v1 = i;
        int v2 = i + 1;
        m_cyclesMesh->add_triangle(v0, v1, v2, 0, true);
    }

    m_cyclesMesh->compute_bounds();
}

void
HdCyclesPoints::_CreateSphereMesh()
{
    float radius = 0.5f;

    int sectorCount = m_pointResolution;
    int stackCount  = m_pointResolution;

    m_cyclesMesh->clear();
    m_cyclesMesh->name = ccl::ustring("generated_sphere");
    m_cyclesMesh->set_subdivision_type(ccl::Mesh::SUBDIVISION_NONE);

    float z, xy;

    float sectorStep = 2.0f * M_PI_F / sectorCount;
    float stackStep  = M_PI_F / stackCount;
    float sectorAngle, stackAngle;

    for (int i = 0; i <= stackCount; ++i) {
        stackAngle = M_PI_F / 2.0f - i * stackStep;
        xy         = radius * cosf(stackAngle);
        z          = radius * sinf(stackAngle);

        for (int j = 0; j <= sectorCount; ++j) {
            sectorAngle = j * sectorStep;

            m_cyclesMesh->add_vertex_slow(ccl::make_float3(xy * cosf(sectorAngle), xy * sinf(sectorAngle), z));
            // TODO: Add normals and uvs
        }
    }

    int k1, k2;
    for (int i = 0; i < stackCount; ++i) {
        k1 = i * (sectorCount + 1);
        k2 = k1 + sectorCount + 1;

        for (int j = 0; j < sectorCount; ++j, ++k1, ++k2) {
            if (i != 0) {
                m_cyclesMesh->add_triangle(k1, k2, k1 + 1, 0, true);
            }

            if (i != (stackCount - 1)) {
                m_cyclesMesh->add_triangle(k1 + 1, k2, k2 + 1, 0, true);
            }
        }
    }

    m_cyclesMesh->compute_bounds();
}

ccl::Object*
HdCyclesPoints::_CreatePointsObject(const ccl::Transform& transform, ccl::Mesh* mesh)
{
    /* create object*/
    ccl::Object* object = new ccl::Object();
    object->set_geometry(mesh);
    object->set_tfm(transform);
    object->get_motion().clear();

    return object;
}

PXR_NAMESPACE_CLOSE_SCOPE
