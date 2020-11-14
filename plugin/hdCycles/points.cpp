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

PXR_NAMESPACE_OPEN_SCOPE

// TODO: Read these from usdCycles schema
// clang-format off
TF_DEFINE_PRIVATE_TOKENS(_tokens,
    ((cyclesPointStyle, "cycles:object:point_style"))
    ((cyclesPointResolution, "cycles:object:point_resolution"))
);
// clang-format on

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
}

HdCyclesPoints::~HdCyclesPoints()
{
    // Remove points
    for (int i = 0; i < m_cyclesObjects.size(); i++) {
        m_renderDelegate->GetCyclesRenderParam()->RemoveObject(
            m_cyclesObjects[i]);
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
HdCyclesPoints::Sync(HdSceneDelegate* sceneDelegate, HdRenderParam* renderParam,
                     HdDirtyBits* dirtyBits, TfToken const& reprSelector)
{
    HdCyclesRenderParam* param = (HdCyclesRenderParam*)renderParam;

    const SdfPath& id = GetId();

    HdCyclesPDPIMap pdpi;

    ccl::Scene* scene = param->GetCyclesScene();

    bool needs_update = false;

    // Read Cycles Primvars

    if (HdChangeTracker::IsPrimvarDirty(*dirtyBits, id,
                                        _tokens->cyclesPointStyle)) {
        needs_update = true;

        HdTimeSampleArray<VtValue, 1> xf;
        sceneDelegate->SamplePrimvar(id, _tokens->cyclesPointStyle, &xf);
        if (xf.count > 0) {
            const VtIntArray& styles = xf.values[0].Get<VtIntArray>();
            if (styles.size() > 0) {
                m_pointStyle = styles[0];
            }
        }
    }

    if (HdChangeTracker::IsPrimvarDirty(*dirtyBits, id,
                                        _tokens->cyclesPointResolution)) {
        needs_update = true;

        HdTimeSampleArray<VtValue, 1> xf;
        sceneDelegate->SamplePrimvar(id, _tokens->cyclesPointResolution, &xf);
        if (xf.count > 0) {
            const VtIntArray& resolutions = xf.values[0].Get<VtIntArray>();
            if (resolutions.size() > 0) {
                m_pointResolution = std::max(resolutions[0], 3);
            }
        }
    }

    // Create Points

    if (*dirtyBits & HdChangeTracker::DirtyPoints) {
        needs_update = true;

        if (m_pointStyle == HdCyclesPointStyle::POINT_DISCS) {
            m_cyclesMesh = _CreateDiscMesh();
        } else {
            m_cyclesMesh = _CreateSphereMesh();
        }

        m_cyclesMesh->tag_update(scene, true);
        param->AddGeometry(m_cyclesMesh);

        const auto pointsValue = sceneDelegate->Get(id, HdTokens->points);
        if (!pointsValue.IsEmpty() && pointsValue.IsHolding<VtVec3fArray>()) {
            const VtVec3fArray& points = pointsValue.Get<VtVec3fArray>();

            for (int i = 0; i < m_cyclesObjects.size(); i++) {
                param->RemoveObject(m_cyclesObjects[i]);
            }

            m_cyclesObjects.clear();

            for (int i = 0; i < points.size(); i++) {
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
    }

    if (*dirtyBits & HdChangeTracker::DirtyTransform) {
        ccl::Transform newTransform = HdCyclesExtractTransform(sceneDelegate,
                                                               id);

        for (int i = 0; i < m_cyclesObjects.size(); i++) {
            m_cyclesObjects[i]->tfm = ccl::transform_inverse(m_transform)
                                      * m_cyclesObjects[i]->tfm;
            m_cyclesObjects[i]->tfm = newTransform * m_cyclesObjects[i]->tfm;
        }

        m_transform = newTransform;

        needs_update = true;
    }


    if (HdChangeTracker::IsPrimvarDirty(*dirtyBits, id, HdTokens->widths)) {
        needs_update = true;

        if (m_cyclesObjects.size() > 0) {
            HdTimeSampleArray<VtValue, 2> xf;
            sceneDelegate->SamplePrimvar(id, HdTokens->widths, &xf);
            if (xf.count > 0) {
                const VtFloatArray& widths = xf.values[0].Get<VtFloatArray>();
                for (int i = 0; i < widths.size(); i++) {
                    if (i < m_cyclesObjects.size()) {
                        float w                 = widths[i];
                        m_cyclesObjects[i]->tfm = m_cyclesObjects[i]->tfm
                                                  * ccl::transform_scale(w, w,
                                                                         w);
                    }
                }
            }
        }
    }

    if (HdChangeTracker::IsPrimvarDirty(*dirtyBits, id, HdTokens->normals)) {
        needs_update = true;

        if (m_cyclesObjects.size() > 0) {
            HdTimeSampleArray<VtValue, 2> xf;
            sceneDelegate->SamplePrimvar(id, HdTokens->normals, &xf);
            if (xf.count > 0) {
                const VtVec3fArray& normals = xf.values[0].Get<VtVec3fArray>();
                for (int i = 0; i < normals.size(); i++) {
                    if (i < m_cyclesObjects.size()) {
                        ccl::float3 rotAxis
                            = ccl::cross(ccl::make_float3(0.0f, 0.0f, 1.0f),
                                         ccl::make_float3(normals[i][0],
                                                          normals[i][1],
                                                          normals[i][2]));
                        float d = ccl::dot(ccl::make_float3(0.0f, 0.0f, 1.0f),
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

    if (*dirtyBits & HdChangeTracker::DirtyVisibility) {
        needs_update = true;

        bool visible = sceneDelegate->GetVisible(id);
        for (int i = 0; i < m_cyclesObjects.size(); i++) {
            if (visible) {
                m_cyclesObjects[i]->visibility |= ccl::PATH_RAY_ALL_VISIBILITY;
            } else {
                m_cyclesObjects[i]->visibility &= ~ccl::PATH_RAY_ALL_VISIBILITY;
            }
        }
    }

    /*if (*dirtyBits & HdChangeTracker::DirtyMaterialId) {
        if (m_cyclesMesh) {
            m_cachedMaterialId = sceneDelegate->GetMaterialId(id);
            if (m_faceVertexCounts.size() > 0) {
                if (!m_cachedMaterialId.IsEmpty()) {
                    const HdCyclesMaterial* material
                        = static_cast<const HdCyclesMaterial*>(
                            sceneDelegate->GetRenderIndex().GetSprim(
                                HdPrimTypeTokens->material, m_cachedMaterialId));

                    if (material && material->GetCyclesShader()) {
                        m_usedShaders.push_back(material->GetCyclesShader());

                        material->GetCyclesShader()->tag_update(scene);
                    } else {
                        m_usedShaders.push_back(fallbackShader);
                    }
                } else {
                    m_usedShaders.push_back(fallbackShader);
                }

                m_cyclesMesh->used_shaders = m_usedShaders;
            }
        }
    }*/

    if (needs_update)
        param->Interrupt();

    *dirtyBits = HdChangeTracker::Clean;
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

// Creates z up disc
ccl::Mesh*
HdCyclesPoints::_CreateDiscMesh()
{
    ccl::Mesh* mesh = new ccl::Mesh();
    mesh->clear();
    mesh->name             = ccl::ustring("generated_disc");
    mesh->subdivision_type = ccl::Mesh::SUBDIVISION_NONE;

    int numVerts = m_pointResolution;
    int numFaces = m_pointResolution - 2;

    mesh->reserve_mesh(numVerts, numFaces);

    mesh->verts.reserve(numVerts);

    for (int i = 0; i < m_pointResolution; i++) {
        float d = ((float)i / (float)m_pointResolution) * 2.0f * M_PI;
        float x = sin(d) * 0.5f;
        float y = cos(d) * 0.5f;
        mesh->verts.push_back_reserved(ccl::make_float3(x, y, 0.0f));
    }

    for (int i = 1; i < m_pointResolution - 1; i++) {
        int v0 = 0;
        int v1 = i;
        int v2 = i + 1;
        mesh->add_triangle(v0, v1, v2, 0, true);
    }

    mesh->compute_bounds();

    return mesh;
}

ccl::Mesh*
HdCyclesPoints::_CreateSphereMesh()
{
    float radius = 0.5f;

    int sectorCount = m_pointResolution;
    int stackCount  = m_pointResolution;

    ccl::Mesh* mesh = new ccl::Mesh();
    mesh->clear();
    mesh->name             = ccl::ustring("generated_sphere");
    mesh->subdivision_type = ccl::Mesh::SUBDIVISION_NONE;

    float z, xy;

    float sectorStep = 2 * M_PI / sectorCount;
    float stackStep  = M_PI / stackCount;
    float sectorAngle, stackAngle;

    for (int i = 0; i <= stackCount; ++i) {
        stackAngle = M_PI / 2 - i * stackStep;
        xy         = radius * cosf(stackAngle);
        z          = radius * sinf(stackAngle);

        for (int j = 0; j <= sectorCount; ++j) {
            sectorAngle = j * sectorStep;

            mesh->verts.push_back_slow(ccl::make_float3(xy * cosf(sectorAngle),
                                                        xy * sinf(sectorAngle),
                                                        z));
            // TODO: Add normals and uvs
        }
    }

    int k1, k2;
    for (int i = 0; i < stackCount; ++i) {
        k1 = i * (sectorCount + 1);
        k2 = k1 + sectorCount + 1;

        for (int j = 0; j < sectorCount; ++j, ++k1, ++k2) {
            if (i != 0) {
                mesh->add_triangle(k1, k2, k1 + 1, 0, true);
            }

            if (i != (stackCount - 1)) {
                mesh->add_triangle(k1 + 1, k2, k2 + 1, 0, true);
            }
        }
    }

    mesh->compute_bounds();

    return mesh;
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

PXR_NAMESPACE_CLOSE_SCOPE
