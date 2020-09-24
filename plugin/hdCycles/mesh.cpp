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

#include "mesh.h"

#include "config.h"
#include "instancer.h"
#include "material.h"
#include "renderDelegate.h"
#include "renderParam.h"
#include "utils.h"

#include "Mikktspace/mikktspace.h"

#include <vector>

#include <render/mesh.h>
#include <render/object.h>
#include <render/scene.h>
#include <render/shader.h>
#include <subd/subd_dice.h>
#include <subd/subd_split.h>
#include <util/util_math_float2.h>
#include <util/util_math_float3.h>

#include <pxr/base/gf/matrix4d.h>
#include <pxr/base/gf/matrix4f.h>
#include <pxr/base/gf/vec2f.h>
#include <pxr/base/gf/vec3f.h>
#include <pxr/base/gf/vec3i.h>
#include <pxr/imaging/hd/extComputationUtils.h>
#include <pxr/imaging/hd/mesh.h>
#include <pxr/imaging/hd/meshUtil.h>
#include <pxr/imaging/hd/points.h>
#include <pxr/imaging/hd/sceneDelegate.h>
#include <pxr/imaging/hd/smoothNormals.h>
#include <pxr/imaging/pxOsd/tokens.h>

PXR_NAMESPACE_OPEN_SCOPE

// clang-format off
TF_DEFINE_PRIVATE_TOKENS(_tokens, 
    (st)
    (uv)
);
// clang-format on

HdCyclesMesh::HdCyclesMesh(SdfPath const& id, SdfPath const& instancerId,
                           HdCyclesRenderDelegate* a_renderDelegate)
    : HdMesh(id, instancerId)
    , m_renderDelegate(a_renderDelegate)
    , m_cyclesMesh(nullptr)
    , m_cyclesObject(nullptr)
    , m_hasVertexColors(false)
    , m_velocityScale(1.0f)
{
    static const HdCyclesConfig& config = HdCyclesConfig::GetInstance();
    m_subdivEnabled                     = config.enable_subdivision;
    m_dicingRate                        = config.subdivision_dicing_rate;
    m_maxSubdivision                    = config.max_subdivision;
    m_useMotionBlur                     = config.enable_motion_blur;

    m_cyclesObject = _CreateCyclesObject();

    m_cyclesMesh = _CreateCyclesMesh();

    m_numTransformSamples = HD_CYCLES_MOTION_STEPS;

    if (m_useMotionBlur) {
        // TODO: Get this from usdCycles schema
        //m_motionSteps = config.motion_steps;
        m_motionSteps = m_numTransformSamples;

        // TODO: Needed when we properly handle motion_verts
        //m_cyclesMesh->motion_steps    = m_motionSteps;
        //m_cyclesMesh->use_motion_blur = m_useMotionBlur;
    }

    m_cyclesObject->geometry = m_cyclesMesh;

    m_renderDelegate->GetCyclesRenderParam()->AddGeometry(m_cyclesMesh);
    m_renderDelegate->GetCyclesRenderParam()->AddObject(m_cyclesObject);
}

HdCyclesMesh::~HdCyclesMesh()
{
    if (m_cyclesMesh) {
        m_renderDelegate->GetCyclesRenderParam()->RemoveMesh(m_cyclesMesh);
        delete m_cyclesMesh;
    }

    if (m_cyclesObject) {
        m_renderDelegate->GetCyclesRenderParam()->RemoveObject(m_cyclesObject);
        delete m_cyclesObject;
    }

    if (m_cyclesInstances.size() > 0) {
        for (auto instance : m_cyclesInstances) {
            if (instance) {
                m_renderDelegate->GetCyclesRenderParam()->RemoveObject(
                    instance);
                delete instance;
            }
        }
    }
}

HdDirtyBits
HdCyclesMesh::GetInitialDirtyBitsMask() const
{
    return HdChangeTracker::Clean | HdChangeTracker::DirtyPoints
           | HdChangeTracker::DirtyTransform | HdChangeTracker::DirtyPrimvar
           | HdChangeTracker::DirtyTopology | HdChangeTracker::DirtyVisibility
           | HdChangeTracker::DirtyMaterialId | HdChangeTracker::DirtySubdivTags
           | HdChangeTracker::DirtyDisplayStyle
           | HdChangeTracker::DirtyDoubleSided;
}
template<typename T>
bool
HdCyclesMesh::GetPrimvarData(TfToken const& name,
                             HdSceneDelegate* sceneDelegate,
                             std::map<HdInterpolation, HdPrimvarDescriptorVector>
                                 primvarDescsPerInterpolation,
                             VtArray<T>& out_data, VtIntArray& out_indices)
{
    out_data.clear();
    out_indices.clear();

    for (auto& primvarDescsEntry : primvarDescsPerInterpolation) {
        for (auto& pv : primvarDescsEntry.second) {
            if (pv.name == name) {
                auto value = GetPrimvar(sceneDelegate, name);
                if (value.IsHolding<VtArray<T>>()) {
                    out_data = value.UncheckedGet<VtArray<T>>();
                    if (primvarDescsEntry.first == HdInterpolationFaceVarying) {
                        out_indices.reserve(m_faceVertexIndices.size());
                        for (int i = 0; i < m_faceVertexIndices.size(); ++i) {
                            out_indices.push_back(i);
                        }
                    }
                    return true;
                }
                return false;
            }
        }
    }

    return false;
}
template bool
HdCyclesMesh::GetPrimvarData<GfVec2f>(
    TfToken const&, HdSceneDelegate*,
    std::map<HdInterpolation, HdPrimvarDescriptorVector>, VtArray<GfVec2f>&,
    VtIntArray&);
template bool
HdCyclesMesh::GetPrimvarData<GfVec3f>(
    TfToken const&, HdSceneDelegate*,
    std::map<HdInterpolation, HdPrimvarDescriptorVector>, VtArray<GfVec3f>&,
    VtIntArray&);

HdDirtyBits
HdCyclesMesh::_PropagateDirtyBits(HdDirtyBits bits) const
{
    return bits;
}

void
HdCyclesMesh::_InitRepr(TfToken const& reprToken, HdDirtyBits* dirtyBits)
{
}

void
HdCyclesMesh::_ComputeTangents(bool needsign)
{
    const ccl::AttributeSet& attributes = (m_useSubdivision && m_subdivEnabled)
                                              ? m_cyclesMesh->subd_attributes
                                              : m_cyclesMesh->attributes;

    ccl::Attribute* attr = attributes.find(ccl::ATTR_STD_UV);
    if (attr) {
        mikk_compute_tangents(attr->standard_name(ccl::ATTR_STD_UV),
                              m_cyclesMesh, needsign, true);
    }
}

void
HdCyclesMesh::_AddUVSet(TfToken name, VtVec2fArray& uvs,
                        HdInterpolation interpolation)
{
    ccl::AttributeSet* attributes = (m_useSubdivision && m_subdivEnabled)
                                        ? &m_cyclesMesh->subd_attributes
                                        : &m_cyclesMesh->attributes;
    bool subdivide_uvs = false;

    ccl::Attribute* attr = attributes->add(ccl::ATTR_STD_UV,
                                           ccl::ustring(name.GetString()));
    ccl::float2* fdata   = attr->data_float2();

    if (m_useSubdivision && subdivide_uvs && m_subdivEnabled)
        attr->flags |= ccl::ATTR_SUBDIVIDED;

    if (interpolation == HdInterpolationVertex) {
        VtIntArray::const_iterator idxIt = m_faceVertexIndices.begin();

        // TODO: Add support for subd faces?
        for (int i = 0; i < m_faceVertexCounts.size(); i++) {
            const int vCount = m_faceVertexCounts[i];

            for (int i = 1; i < vCount - 1; ++i) {
                int v0 = *idxIt;
                int v1 = *(idxIt + i + 0);
                int v2 = *(idxIt + i + 1);

                fdata[0] = vec2f_to_float2(uvs[v0]);
                fdata[1] = vec2f_to_float2(uvs[v1]);
                fdata[2] = vec2f_to_float2(uvs[v2]);
                fdata += 3;
            }
            idxIt += vCount;
        }
    } else {
        for (size_t i = 0; i < uvs.size(); i++) {
            fdata[0] = vec2f_to_float2(uvs[i]);
            fdata += 1;
        }
    }
}

void
HdCyclesMesh::_AddVelocities(VtVec3fArray& velocities,
                             HdInterpolation interpolation)
{
    ccl::AttributeSet* attributes = (m_useSubdivision && m_subdivEnabled)
                                        ? &m_cyclesMesh->subd_attributes
                                        : &m_cyclesMesh->attributes;

    m_cyclesMesh->use_motion_blur = true;
    m_cyclesMesh->motion_steps = 3;

    ccl::Attribute* attr_mP = attributes->find(
        ccl::ATTR_STD_MOTION_VERTEX_POSITION);

    attributes->remove(attr_mP);

    //if (!attr_mP) {
        attr_mP = attributes->add(ccl::ATTR_STD_MOTION_VERTEX_POSITION);
    //}

    //ccl::float3* vdata = attr_mP->data_float3();

    /*if (interpolation == HdInterpolationVertex) {
        VtIntArray::const_iterator idxIt = m_faceVertexIndices.begin();

        // TODO: Add support for subd faces?
        for (int i = 0; i < m_faceVertexCounts.size(); i++) {
            const int vCount = m_faceVertexCounts[i];

            for (int i = 1; i < vCount - 1; ++i) {
                int v0 = *idxIt;
                int v1 = *(idxIt + i + 0);
                int v2 = *(idxIt + i + 1);

                vdata[0] = vec3f_to_float3(velocities[v0]);
                vdata[1] = vec3f_to_float3(velocities[v1]);
                vdata[2] = vec3f_to_float3(velocities[v2]);
                vdata += 3;
            }
            idxIt += vCount;
        }
    } else {*/

    ccl::float3* mP = attr_mP->data_float3();

    std::cout << "motion_steps: " << m_cyclesMesh->motion_steps << '\n';
    std::cout << "velocities: " << velocities.size() << '\n';
    std::cout << "m_points: " << m_points.size() << '\n';
    for (size_t i = 0; i < m_cyclesMesh->motion_steps; ++i) {
        //VtVec3fArray pp;
        //pp = m_pointSamples.values.data()[i].Get<VtVec3fArray>();

        for (size_t j = 0; j < velocities.size(); ++j, ++mP) {
            
            *mP = vec3f_to_float3(m_points[j] + (velocities[j] * m_velocityScale));
        }
    }

}

void
HdCyclesMesh::_AddColors(TfToken name, VtVec3fArray& colors, ccl::Scene* scene,
                         HdInterpolation interpolation)
{
    if (colors.size() <= 0)
        return;

    ccl::AttributeSet* attributes = (m_useSubdivision && m_subdivEnabled)
                                        ? &m_cyclesMesh->subd_attributes
                                        : &m_cyclesMesh->attributes;

    ccl::AttributeStandard vcol_std = ccl::ATTR_STD_VERTEX_COLOR;
    ccl::ustring vcol_name          = ccl::ustring(name.GetString());

    const bool need_vcol = m_cyclesMesh->need_attribute(scene, vcol_name)
                           || m_cyclesMesh->need_attribute(scene, vcol_std);

    ccl::Attribute* vcol_attr = NULL;
    vcol_attr                 = attributes->add(vcol_std, vcol_name);

    ccl::uchar4* cdata = vcol_attr->data_uchar4();

    if (interpolation == HdInterpolationVertex) {
        VtIntArray::const_iterator idxIt = m_faceVertexIndices.begin();

        // TODO: Add support for subd faces?
        for (int i = 0; i < m_faceVertexCounts.size(); i++) {
            const int vCount = m_faceVertexCounts[i];

            for (int i = 1; i < vCount - 1; ++i) {
                int v0 = *idxIt;
                int v1 = *(idxIt + i + 0);
                int v2 = *(idxIt + i + 1);

                cdata[0] = ccl::color_float4_to_uchar4(
                    ccl::color_srgb_to_linear_v4(vec3f_to_float4(colors[v0])));
                cdata[1] = ccl::color_float4_to_uchar4(
                    ccl::color_srgb_to_linear_v4(vec3f_to_float4(colors[v1])));
                cdata[2] = ccl::color_float4_to_uchar4(
                    ccl::color_srgb_to_linear_v4(vec3f_to_float4(colors[v2])));
                cdata += 3;
            }
            idxIt += vCount;
        }

    } else if (interpolation == HdInterpolationVarying
               || interpolation == HdInterpolationConstant
               || interpolation == HdInterpolationUniform) {
        for (size_t i = 0; i < m_numMeshFaces * 3; i++) {
            GfVec3f pv_col  = colors[0];
            ccl::float4 col = vec3f_to_float4(pv_col);

            cdata[0] = ccl::color_float4_to_uchar4(
                ccl::color_srgb_to_linear_v4(col));
            cdata += 1;
        }
    } else if (interpolation == HdInterpolationFaceVarying) {
        for (size_t i = 0; i < m_numMeshFaces * 3; i++) {
            int idx = i;
            if (idx > colors.size())
                idx = colors.size() - 1;

            GfVec3f pv_col  = colors[idx];
            ccl::float4 col = vec3f_to_float4(pv_col);

            cdata[0] = ccl::color_float4_to_uchar4(
                ccl::color_srgb_to_linear_v4(col));
            cdata += 1;
        }
    }
}

void
HdCyclesMesh::_AddNormals(VtVec3fArray& normals, HdInterpolation interpolation)
{
    m_cyclesMesh->add_face_normals();
    m_cyclesMesh->add_vertex_normals();

    //TODO: Implement
}

ccl::Mesh*
HdCyclesMesh::_CreateCyclesMesh()
{
    ccl::Mesh* mesh = new ccl::Mesh();
    mesh->clear();

    if (m_useMotionBlur) {
        mesh->use_motion_blur = m_useMotionBlur;
    }

    m_numMeshVerts = 0;
    m_numMeshFaces = 0;

    mesh->subdivision_type = ccl::Mesh::SUBDIVISION_NONE;
    return mesh;
}

ccl::Object*
HdCyclesMesh::_CreateCyclesObject()
{
    ccl::Object* object = new ccl::Object();

    object->tfm = ccl::transform_identity();

    object->visibility = ccl::PATH_RAY_ALL_VISIBILITY;

    return object;
}

void
HdCyclesMesh::_PopulateVertices()
{
    m_cyclesMesh->verts.reserve(m_numMeshVerts);
    for (int i = 0; i < m_points.size(); i++) {
        m_cyclesMesh->verts.push_back_reserved(vec3f_to_float3(m_points[i]));
    }
}

void
HdCyclesMesh::_PopulateMotion()
{
    m_cyclesMesh->use_motion_blur = true;

    m_cyclesMesh->motion_steps = m_pointSamples.count + 1;

    ccl::Attribute* attr_mP = m_cyclesMesh->attributes.find(
        ccl::ATTR_STD_MOTION_VERTEX_POSITION);

    //if(attr_mP)
    //m_cyclesMesh->attributes.remove(attr_mP);

    if (!attr_mP) {
        attr_mP = m_cyclesMesh->attributes.add(
            ccl::ATTR_STD_MOTION_VERTEX_POSITION);
    }

    ccl::float3* mP = attr_mP->data_float3();
    for (size_t i = 0; i < m_pointSamples.count; ++i) {
        VtVec3fArray pp;
        pp = m_pointSamples.values.data()[i].Get<VtVec3fArray>();

        for (size_t j = 0; j < m_numMeshVerts; ++j, ++mP) {
            *mP = vec3f_to_float3(pp[j]);
        }
    }
}

void
HdCyclesMesh::_PopulateFaces(const std::vector<int>& a_faceMaterials,
                             bool a_subdivide)
{
    if (a_subdivide) {
        m_cyclesMesh->subdivision_type = ccl::Mesh::SUBDIVISION_CATMULL_CLARK;
        m_cyclesMesh->reserve_subd_faces(m_numMeshFaces, m_numNgons,
                                         m_numCorners);
    } else {
        m_cyclesMesh->reserve_mesh(m_numMeshVerts, m_numMeshFaces);
    }

    VtIntArray::const_iterator idxIt = m_faceVertexIndices.begin();

    if (a_subdivide) {
        bool smooth = true;
        std::vector<int> vi;
        for (int i = 0; i < m_faceVertexCounts.size(); i++) {
            const int vCount = m_faceVertexCounts[i];
            int materialId   = 0;

            vi.resize(vCount);

            for (int i = 0; i < vCount; ++i) {
                vi[i] = *(idxIt + i);
            }
            idxIt += vCount;

            m_cyclesMesh->add_subd_face(&vi[0], vCount, materialId, true);
        }
    } else {
        for (int i = 0; i < m_faceVertexCounts.size(); i++) {
            const int vCount = m_faceVertexCounts[i];
            int materialId   = 0;

            if (i < a_faceMaterials.size()) {
                materialId = a_faceMaterials[i];
            }

            for (int i = 1; i < vCount - 1; ++i) {
                int v0 = *idxIt;
                int v1 = *(idxIt + i + 0);
                int v2 = *(idxIt + i + 1);
                if (v0 < m_numMeshVerts && v1 < m_numMeshVerts
                    && v2 < m_numMeshVerts) {
                    m_cyclesMesh->add_triangle(v0, v1, v2, materialId, true);
                }
            }
            idxIt += vCount;
        }
    }
}

void
HdCyclesMesh::_PopulateCreases()
{
    size_t num_creases = m_creaseLengths.size();

    m_cyclesMesh->subd_creases.resize(num_creases);

    ccl::Mesh::SubdEdgeCrease* crease = m_cyclesMesh->subd_creases.data();
    for (int i = 0; i < num_creases; i++) {
        crease->v[0]   = m_creaseIndices[(i * 2) + 0];
        crease->v[1]   = m_creaseIndices[(i * 2) + 1];
        crease->crease = m_creaseWeights[i];

        crease++;
    }
}

void
HdCyclesMesh::_FinishMesh(ccl::Scene* scene)
{
    _ComputeTangents(true);

    if (m_cyclesMesh->need_attribute(scene, ccl::ATTR_STD_GENERATED)) {
        ccl::AttributeSet* attributes = (m_useSubdivision && m_subdivEnabled)
                                            ? &m_cyclesMesh->subd_attributes
                                            : &m_cyclesMesh->attributes;
        ccl::Attribute* attr = attributes->add(ccl::ATTR_STD_GENERATED);
        memcpy(attr->data_float3(), m_cyclesMesh->verts.data(),
               sizeof(ccl::float3) * m_cyclesMesh->verts.size());
    }

    m_cyclesMesh->compute_bounds();
}

void
HdCyclesMesh::Sync(HdSceneDelegate* sceneDelegate, HdRenderParam* renderParam,
                   HdDirtyBits* dirtyBits, TfToken const& reprToken)
{
    HdCyclesRenderParam* param = (HdCyclesRenderParam*)renderParam;
    ccl::Scene* scene          = param->GetCyclesScene();


    scene->mutex.lock();

    const SdfPath& id = GetId();

    // -------------------------------------
    // -- Pull scene data

    bool mesh_updated = false;

    bool newMesh = false;

    bool pointsIsComputed = false;

    auto extComputationDescs
        = sceneDelegate->GetExtComputationPrimvarDescriptors(
            id, HdInterpolationVertex);
    /*for (auto& desc : extComputationDescs) {
        if (desc.name != HdTokens->points)
            continue;

        if (HdChangeTracker::IsPrimvarDirty(*dirtyBits, id, desc.name)) {
            mesh_updated    = true;
            auto valueStore = HdExtComputationUtils::GetComputedPrimvarValues(
                { desc }, sceneDelegate);
            auto pointValueIt = valueStore.find(desc.name);
            if (pointValueIt != valueStore.end()) {
                if (!pointValueIt->second.IsEmpty()) {
                    m_points       = pointValueIt->second.Get<VtVec3fArray>();
                    m_numMeshVerts = m_points.size();

                    m_normalsValid   = false;
                    pointsIsComputed = true;
                    newMesh          = true;
                }
            }
        }
        break;
    }*/

    if (/*!pointsIsComputed
        && */
        HdChangeTracker::IsPrimvarDirty(*dirtyBits, id, HdTokens->points)) {
        mesh_updated        = true;
        VtValue pointsValue = sceneDelegate->Get(id, HdTokens->points);
        if (!pointsValue.IsEmpty()) {
            m_points = pointsValue.Get<VtVec3fArray>();
            if (m_points.size() > 0) {
                m_numMeshVerts = m_points.size();

                m_normalsValid = false;
                newMesh        = true;
            }

            // TODO: Should we check if time varying?
            // TODO: can we use this for m_points too?
            sceneDelegate->SamplePrimvar(id, HdTokens->points, &m_pointSamples);
        } /*
        size_t maxSample = 3;

        HdCyclesSampledPrimvarType 4;
        sceneDelegate->SamplePrimvar(id, HdTokens->points, &samples );
        std::cout << "Found time sampled points "<< samples.count << '\n';*/
    }

    static const HdCyclesConfig& config = HdCyclesConfig::GetInstance();

    if (HdChangeTracker::IsTopologyDirty(*dirtyBits, id)) {
        m_topology          = GetMeshTopology(sceneDelegate);
        m_faceVertexCounts  = m_topology.GetFaceVertexCounts();
        m_faceVertexIndices = m_topology.GetFaceVertexIndices();
        m_geomSubsets       = m_topology.GetGeomSubsets();

        m_numMeshFaces = 0;
        for (int i = 0; i < m_faceVertexCounts.size(); i++) {
            m_numMeshFaces += m_faceVertexCounts[i] - 2;
        }

        m_numNgons   = 0;
        m_numCorners = 0;

        for (int i = 0; i < m_faceVertexCounts.size(); i++) {
            // TODO: This seems wrong, but works for now
            m_numNgons += 1;  // (m_faceVertexCounts[i] == 4) ? 0 : 1;
            m_numCorners += m_faceVertexCounts[i];
        }

        m_adjacencyValid = false;
        m_normalsValid   = false;
        if (m_subdivEnabled) {
            m_useSubdivision = m_topology.GetScheme()
                               == PxOsdOpenSubdivTokens->catmullClark;
        }

        newMesh = true;
    }

    std::map<HdInterpolation, HdPrimvarDescriptorVector>
        primvarDescsPerInterpolation = {
            { HdInterpolationFaceVarying, sceneDelegate->GetPrimvarDescriptors(
                                              id, HdInterpolationFaceVarying) },
            { HdInterpolationVertex,
              sceneDelegate->GetPrimvarDescriptors(id, HdInterpolationVertex) },
            { HdInterpolationConstant,
              sceneDelegate->GetPrimvarDescriptors(id,
                                                   HdInterpolationConstant) },
        };

    if (*dirtyBits & HdChangeTracker::DirtyDoubleSided) {
        mesh_updated  = true;
        m_doubleSided = sceneDelegate->GetDoubleSided(id);
    }

    // -------------------------------------
    // -- Resolve Drawstyles

    bool isRefineLevelDirty = false;
    if (*dirtyBits & HdChangeTracker::DirtyDisplayStyle) {
        mesh_updated = true;

        m_displayStyle = sceneDelegate->GetDisplayStyle(id);
        if (m_refineLevel != m_displayStyle.refineLevel) {
            isRefineLevelDirty = true;
            m_refineLevel      = m_displayStyle.refineLevel;
            newMesh            = true;
        }
    }

    if (HdChangeTracker::IsSubdivTagsDirty(*dirtyBits, id)) {
        const PxOsdSubdivTags subdivTags = GetSubdivTags(sceneDelegate);

        m_cornerIndices = subdivTags.GetCornerIndices();
        m_cornerWeights = subdivTags.GetCornerWeights();
        m_creaseIndices = subdivTags.GetCreaseIndices();
        m_creaseLengths = subdivTags.GetCreaseLengths();
        m_creaseWeights = subdivTags.GetCreaseWeights();

        newMesh = true;
    }

    // -------------------------------------
    // -- Create Cycles Mesh

    HdMeshUtil meshUtil(&m_topology, id);
    if (newMesh) {
        m_cyclesMesh->clear();

        _PopulateVertices();
        _PopulateMotion();

        std::vector<int> faceMaterials;
        faceMaterials.resize(m_numMeshFaces);

        for (auto const& subset : m_geomSubsets) {
            int subsetMaterialIndex = 0;

            if (!subset.materialId.IsEmpty()) {
                const HdCyclesMaterial* subMat
                    = static_cast<const HdCyclesMaterial*>(
                        sceneDelegate->GetRenderIndex().GetSprim(
                            HdPrimTypeTokens->material, subset.materialId));
                if (subMat && subMat->GetCyclesShader()) {
                    if (m_materialMap.find(subset.materialId)
                        == m_materialMap.end()) {
                        m_cyclesMesh->used_shaders.push_back(
                            subMat->GetCyclesShader());
                        subMat->GetCyclesShader()->tag_update(scene);

                        m_materialMap.insert(std::pair<SdfPath, int>(
                            subset.materialId,
                            m_cyclesMesh->used_shaders.size()));
                        subsetMaterialIndex = m_cyclesMesh->used_shaders.size();
                    } else {
                        subsetMaterialIndex = m_materialMap.at(
                            subset.materialId);
                    }
                }
            }

            for (int i : subset.indices) {
                faceMaterials[i] = std::max(subsetMaterialIndex - 1, 0);
            }
        }

        _PopulateFaces(faceMaterials, (m_useSubdivision && m_subdivEnabled));

        if (m_useSubdivision && m_subdivEnabled) {
            _PopulateCreases();

            if (!m_cyclesMesh->subd_params) {
                m_cyclesMesh->subd_params = new ccl::SubdParams(m_cyclesMesh);
            }

            ccl::SubdParams& subd_params = *m_cyclesMesh->subd_params;

            subd_params.dicing_rate = m_dicingRate
                                      / ((m_refineLevel + 1) * 2.0f);
            subd_params.max_level = m_maxSubdivision;

            subd_params.objecttoworld = ccl::transform_identity();
        }

        // Get all uvs (assumes all GfVec2f are uvs)
        for (auto& primvarDescsEntry : primvarDescsPerInterpolation) {
            for (auto& pv : primvarDescsEntry.second) {
                if (HdChangeTracker::IsPrimvarDirty(*dirtyBits, id, pv.name)) {
                    auto value = GetPrimvar(sceneDelegate, pv.name);
                    VtValue triangulated;


                    if (pv.name == HdTokens->normals) {
                        VtVec3fArray normals;
                        normals = value.UncheckedGet<VtArray<GfVec3f>>();

                        // TODO: Properly implement
                        /*if (primvarDescsEntry.first
                            == HdInterpolationFaceVarying) {
                            // Triangulate primvar normals
                            meshUtil.ComputeTriangulatedFaceVaryingPrimvar(
                                normals.data(), normals.size(), HdTypeFloatVec3,
                                &triangulated);
                            normals = triangulated.Get<VtVec3fArray>();
                        }*/

                        _AddNormals(normals, primvarDescsEntry.first);
                    }

                    // TODO: Properly implement
                    VtValue triangulatedVal;
                    if (pv.name == HdTokens->velocities) {
                        if (value.IsHolding<VtArray<GfVec3f>>()) {
                            VtVec3fArray vels;
                            vels = value.UncheckedGet<VtArray<GfVec3f>>();

                            /*meshUtil.ComputeTriangulatedFaceVaryingPrimvar(
                            vels.data(), vels.size(), HdTypeFloatVec3,
                            &triangulatedVal);
                        VtVec3fArray m_vels_tri
                            = triangulatedVal.Get<VtVec3fArray>();
                        _AddVelocities(m_vels_tri, primvarDescsEntry.first);*/


                            if (primvarDescsEntry.first
                                == HdInterpolationFaceVarying) {
                                meshUtil.ComputeTriangulatedFaceVaryingPrimvar(
                                    vels.data(), vels.size(), HdTypeFloatVec3,
                                    &triangulated);

                                VtVec3fArray triangulatedVels
                                    = triangulated.Get<VtVec3fArray>();

                                //_AddVelocities(triangulatedVels,
                                //               primvarDescsEntry.first);
                            } else {
                                //_AddVelocities(vels, primvarDescsEntry.first);
                            }
                        }
                    }

                    if (pv.role == HdPrimvarRoleTokens->color) {
                        m_hasVertexColors = true;

                        if (value.IsHolding<VtArray<GfVec3f>>()) {
                            // Get primvar colors
                            VtVec3fArray colors;
                            colors = value.UncheckedGet<VtArray<GfVec3f>>();

                            if (primvarDescsEntry.first
                                == HdInterpolationFaceVarying) {
                                // Triangulate primvar colors
                                meshUtil.ComputeTriangulatedFaceVaryingPrimvar(
                                    colors.data(), colors.size(),
                                    HdTypeFloatVec3, &triangulated);
                                colors = triangulated.Get<VtVec3fArray>();
                            }

                            // Add colors to attribute
                            _AddColors(pv.name, colors, scene,
                                       primvarDescsEntry.first);
                        }
                    }

                    // TODO: Add more general uv support
                    //if (pv.role == HdPrimvarRoleTokens->textureCoordinate) {
                    if (value.IsHolding<VtArray<GfVec2f>>()) {
                        VtVec2fArray uvs
                            = value.UncheckedGet<VtArray<GfVec2f>>();
                        if (primvarDescsEntry.first
                            == HdInterpolationFaceVarying) {
                            meshUtil.ComputeTriangulatedFaceVaryingPrimvar(
                                uvs.data(), uvs.size(), HdTypeFloatVec2,
                                &triangulated);

                            VtVec2fArray triangulatedUvs
                                = triangulated.Get<VtVec2fArray>();

                            _AddUVSet(pv.name, triangulatedUvs,
                                      primvarDescsEntry.first);
                        } else {
                            _AddUVSet(pv.name, uvs, primvarDescsEntry.first);
                        }
                    }
                }
            }
        }
    }

    if (*dirtyBits & HdChangeTracker::DirtyTransform) {
        m_transformSamples = HdCyclesSetTransform(m_cyclesObject, sceneDelegate,
                                                  id, m_useMotionBlur);

        if (m_cyclesMesh && m_cyclesMesh->subd_params) {
            m_cyclesMesh->subd_params->objecttoworld = m_cyclesObject->tfm;
        }

        mesh_updated = true;
    }

    ccl::Shader* fallbackShader = scene->default_surface;

    if (m_hasVertexColors) {
        fallbackShader = param->default_vcol_surface;
    }

    if (*dirtyBits & HdChangeTracker::DirtyMaterialId) {
        if (m_cyclesMesh) {
            m_cachedMaterialId = sceneDelegate->GetMaterialId(id);
            if (m_faceVertexCounts.size() > 0) {
                if (!m_cachedMaterialId.IsEmpty()) {
                    const HdCyclesMaterial* material
                        = static_cast<const HdCyclesMaterial*>(
                            sceneDelegate->GetRenderIndex().GetSprim(
                                HdPrimTypeTokens->material, m_cachedMaterialId));

                    if (material && material->GetCyclesShader()) {
                        m_cyclesMesh->used_shaders.push_back(
                            material->GetCyclesShader());

                        material->GetCyclesShader()->tag_update(scene);
                    } else {
                        m_cyclesMesh->used_shaders.push_back(fallbackShader);
                    }
                } else {
                    m_cyclesMesh->used_shaders.push_back(fallbackShader);
                }
            }
        }
    }

    if (*dirtyBits & HdChangeTracker::DirtyVisibility) {
        mesh_updated        = true;
        _sharedData.visible = sceneDelegate->GetVisible(id);
        if (_sharedData.visible) {
            m_cyclesObject->visibility |= ccl::PATH_RAY_ALL_VISIBILITY;
        } else {
            m_cyclesObject->visibility &= ~ccl::PATH_RAY_ALL_VISIBILITY;
        }
    }

    // -------------------------------------
    // -- Handle point instances

    if (newMesh || (*dirtyBits & HdChangeTracker::DirtyInstancer)) {
        mesh_updated = true;
        if (auto instancer = static_cast<HdCyclesInstancer*>(
                sceneDelegate->GetRenderIndex().GetInstancer(
                    GetInstancerId()))) {
            auto instanceTransforms = instancer->SampleInstanceTransforms(id);
            auto newNumInstances    = (instanceTransforms.count > 0)
                                       ? instanceTransforms.values[0].size()
                                       : 0;
            // Clear all instances...
            if (m_cyclesInstances.size() > 0) {
                for (auto instance : m_cyclesInstances) {
                    if (instance) {
                        m_renderDelegate->GetCyclesRenderParam()->RemoveObject(
                            instance);
                        delete instance;
                    }
                }
                m_cyclesInstances.clear();
            }

            if (newNumInstances != 0) {
                std::vector<TfSmallVector<GfMatrix4d, 1>> combinedTransforms;
                combinedTransforms.reserve(newNumInstances);
                for (size_t i = 0; i < newNumInstances; ++i) {
                    // Apply prototype transform (m_transformSamples) to all the instances
                    combinedTransforms.emplace_back(instanceTransforms.count);
                    auto& instanceTransform = combinedTransforms.back();

                    if (m_transformSamples.count == 0
                        || (m_transformSamples.count == 1
                            && (m_transformSamples.values[0]
                                == GfMatrix4d(1)))) {
                        for (size_t j = 0; j < instanceTransforms.count; ++j) {
                            instanceTransform[j]
                                = instanceTransforms.values[j][i];
                        }
                    } else {
                        for (size_t j = 0; j < instanceTransforms.count; ++j) {
                            GfMatrix4d xf_j = m_transformSamples.Resample(
                                instanceTransforms.times[j]);
                            instanceTransform[j]
                                = xf_j * instanceTransforms.values[j][i];
                        }
                    }
                }

                for (int j = 0; j < newNumInstances; ++j) {
                    ccl::Object* instanceObj = _CreateCyclesObject();

                    instanceObj->tfm = mat4d_to_transform(
                        combinedTransforms[j].data()[0]);
                    instanceObj->geometry = m_cyclesMesh;

                    // TODO: Implement motion blur for point instanced objects
                    if (m_useMotionBlur) {
                        m_cyclesMesh->motion_steps    = m_motionSteps;
                        m_cyclesMesh->use_motion_blur = m_useMotionBlur;

                        instanceObj->motion.clear();
                        instanceObj->motion.resize(m_motionSteps);
                        for (int j = 0; j < m_motionSteps; j++) {
                            instanceObj->motion[j] = mat4d_to_transform(
                                combinedTransforms[j].data()[j]);
                        }
                    }

                    m_cyclesInstances.push_back(instanceObj);

                    m_renderDelegate->GetCyclesRenderParam()->AddObject(
                        instanceObj);
                }

                // Hide prototype
                if (m_cyclesObject)
                    m_cyclesObject->visibility = 0;
            }
        }
    }

    // -------------------------------------
    // -- Finish Mesh

    if (newMesh && m_cyclesMesh) {
        _FinishMesh(scene);
    }

    if (mesh_updated || newMesh) {
        m_cyclesMesh->tag_update(scene, false);
        m_cyclesObject->tag_update(scene);
        param->Interrupt();
    }

    scene->mutex.unlock();

    *dirtyBits = HdChangeTracker::Clean;
}

PXR_NAMESPACE_CLOSE_SCOPE
