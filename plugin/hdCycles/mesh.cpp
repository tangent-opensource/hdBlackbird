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

#include <pxr/imaging/hd/extComputationUtils.h>

#ifdef USE_USD_CYCLES_SCHEMA
#    include <usdCycles/tokens.h>
#endif

PXR_NAMESPACE_OPEN_SCOPE

// clang-format off
TF_DEFINE_PRIVATE_TOKENS(_tokens, 
    (st)
    (uv)
);
// clang-format on

HdCyclesMesh::HdCyclesMesh(SdfPath const& id, SdfPath const& instancerId, HdCyclesRenderDelegate* a_renderDelegate)
    : HdMesh(id, instancerId)
    , m_renderDelegate(a_renderDelegate)
    , m_cyclesMesh(nullptr)
    , m_cyclesObject(nullptr)
    , m_visibilityFlags(ccl::PATH_RAY_ALL_VISIBILITY)
    , m_visCamera(true)
    , m_visDiffuse(true)
    , m_visGlossy(true)
    , m_visScatter(true)
    , m_visShadow(true)
    , m_visTransmission(true)
    , m_velocityScale(1.0f)
    , m_useMotionBlur(false)
    , m_useDeformMotionBlur(false)
{
    static const HdCyclesConfig& config = HdCyclesConfig::GetInstance();
    config.enable_motion_blur.eval(m_useMotionBlur, true);

    _InitializeNewCyclesMesh();
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
                m_renderDelegate->GetCyclesRenderParam()->RemoveObject(instance);
                delete instance;
            }
        }
    }
}

void
HdCyclesMesh::_AddUVSet(const TfToken& name, const VtValue& uvs, ccl::Scene* scene, HdInterpolation interpolation)
{
    VtValue uvs_value = uvs;

    if (!uvs_value.IsHolding<VtVec2fArray>()) {
        if (!uvs_value.CanCast<VtVec2fArray>()) {
            TF_WARN("Invalid uv data! Can not convert uv for: %s", "object");
            return;
        }

        uvs_value = uvs_value.Cast<VtVec2fArray>();
    }

    ccl::ustring uv_name = ccl::ustring(name.GetString());
    bool need_uv         = m_cyclesMesh->need_attribute(scene, uv_name)
                   || m_cyclesMesh->need_attribute(scene, ccl::ATTR_STD_UV);
    if (!need_uv) {
        return;
    }

    // To avoid face varying computations we take attribute and we refine it with
    // respecting incoming interpolation. Then we convert it to face varying because
    // ATTR_STD_UV is a face varying data.

    ccl::AttributeSet* attributes = &m_cyclesMesh->attributes;
    ccl::Attribute* uv_attr       = attributes->add(ccl::ATTR_STD_UV, uv_name);
    auto attrib_data              = uv_attr->data_float2();

    if (interpolation == HdInterpolationConstant) {
        VtValue refined_value = m_refiner->RefineConstantData(name, HdPrimvarRoleTokens->textureCoordinate, uvs_value);
        if (refined_value.GetArraySize() != 1) {
            TF_WARN("Failed to refine constant texture coordinates!");
            return;
        }

        auto refined_uvs                    = refined_value.UncheckedGet<VtVec2fArray>();
        const VtVec3iArray& refined_indices = m_refiner->GetRefinedVertexIndices();
        for (size_t face {}, offset {}; face < refined_indices.size(); ++face) {
            for (size_t i {}; i < 3; ++i, ++offset) {
                attrib_data[offset][0] = refined_uvs[0][0];
                attrib_data[offset][1] = refined_uvs[0][1];
            }
        }
    }

    if (interpolation == HdInterpolationUniform) {
        VtValue refined_value = m_refiner->RefineUniformData(name, HdPrimvarRoleTokens->textureCoordinate, uvs_value);
        if (refined_value.GetArraySize() != m_cyclesMesh->num_triangles()) {
            TF_WARN("Failed to refine uniform texture coordinates!");
            return;
        }

        auto refined_uvs                    = refined_value.UncheckedGet<VtVec2fArray>();
        const VtVec3iArray& refined_indices = m_refiner->GetRefinedVertexIndices();
        for (size_t face {}, offset {}; face < refined_indices.size(); ++face) {
            for (size_t i {}; i < 3; ++i, ++offset) {
                attrib_data[offset][0] = refined_uvs[face][0];
                attrib_data[offset][1] = refined_uvs[face][1];
            }
        }
        return;
    }

    // convert vertex and varying

    auto add_vertex_or_varying_attrib = [&](const VtValue& refined_value) {
        auto refined_uvs                    = refined_value.UncheckedGet<VtVec2fArray>();
        const VtVec3iArray& refined_indices = m_refiner->GetRefinedVertexIndices();
        for (size_t face {}, offset {}; face < refined_indices.size(); ++face) {
            for (size_t i {}; i < 3; ++i, ++offset) {
                const int& vertex_index = refined_indices[face][i];
                attrib_data[offset][0]  = refined_uvs[vertex_index][0];
                attrib_data[offset][1]  = refined_uvs[vertex_index][1];
            }
        }
    };

    if (interpolation == HdInterpolationVertex) {
        VtValue refined_value = m_refiner->RefineVertexData(name, HdPrimvarRoleTokens->textureCoordinate, uvs_value);
        if (refined_value.GetArraySize() != m_cyclesMesh->verts.size()) {
            TF_WARN("Failed to refine vertex texture coordinates!");
            return;
        }

        add_vertex_or_varying_attrib(refined_value);
        return;
    }

    if (interpolation == HdInterpolationVarying) {
        VtValue refined_value = m_refiner->RefineVaryingData(name, HdPrimvarRoleTokens->textureCoordinate, uvs_value);
        if (refined_value.GetArraySize() != m_cyclesMesh->verts.size()) {
            TF_WARN("Failed to refine varying texture coordinates!");
            return;
        }

        add_vertex_or_varying_attrib(refined_value);
        return;
    }

    if (interpolation == HdInterpolationFaceVarying) {
        VtValue refined_value = m_refiner->RefineFaceVaryingData(name, HdPrimvarRoleTokens->textureCoordinate,
                                                                 uvs_value);
        if (refined_value.GetArraySize() != m_refiner->GetNumRefinedTriangles() * 3) {
            TF_WARN("Invalid number of refined vertices");
            return;
        }

        auto refined_uvs = refined_value.UncheckedGet<VtVec2fArray>();
        for (size_t i {}; i < refined_uvs.size(); ++i) {
            attrib_data[i][0] = refined_uvs[i][0];
            attrib_data[i][1] = refined_uvs[i][1];
        }

        return;
    }

    //
    // Tangents
    //

    ccl::ustring tangent_name = ccl::ustring(name.GetString() + ".tangent");
    ccl::ustring sign_name    = ccl::ustring(name.GetString() + ".tangent_sign");
    bool need_tangent         = m_cyclesMesh->need_attribute(scene, tangent_name)
                        || m_cyclesMesh->need_attribute(scene, ccl::ATTR_STD_UV_TANGENT);
    if (!need_tangent) {
        return;
    }

    // Take tangent from subdivision limit surface
    if (m_refiner->IsSubdivided()) {
        // subdivided tangents are per vertex

        if (m_cyclesMesh->need_attribute(scene, ccl::ATTR_STD_UV_TANGENT)) {
            ccl::Attribute* tangent_attrib = attributes->add(ccl::ATTR_STD_UV_TANGENT, tangent_name);
            ccl::float3* tangent_data      = tangent_attrib->data_float3();

            for (size_t i {}; i < m_cyclesMesh->triangles.size(); ++i) {
                auto vertex_index = m_cyclesMesh->triangles[i];
                tangent_data[i]   = m_limit_us[vertex_index];
            }
        }

        if (m_cyclesMesh->need_attribute(scene, ccl::ATTR_STD_UV_TANGENT_SIGN)) {
            auto sign_attrib = attributes->add(ccl::ATTR_STD_UV_TANGENT_SIGN, sign_name);
            auto sign_data   = sign_attrib->data_float();

            for (size_t i {}; i < m_cyclesMesh->triangles.size(); ++i) {
                sign_data[i] = 1.0f;
            }
        }

        return;
    }

    // Forced true for now... Should be based on shader compilation needs
    need_tangent = true;
    if (need_tangent) {
        bool need_sign = m_cyclesMesh->need_attribute(scene, sign_name)
                         || m_cyclesMesh->need_attribute(scene, ccl::ATTR_STD_UV_TANGENT_SIGN);


        // Forced for now
        need_sign = true;
        mikk_compute_tangents(name.GetString().c_str(), m_cyclesMesh, need_sign, true);
    }
}

void
HdCyclesMesh::_AddVelocities(VtVec3fArray& velocities, HdInterpolation interpolation)
{
    ccl::AttributeSet* attributes = &m_cyclesMesh->attributes;
    m_cyclesMesh->use_motion_blur = true;
    m_cyclesMesh->motion_steps    = 3;

    ccl::Attribute* attr_mP = attributes->find(ccl::ATTR_STD_MOTION_VERTEX_POSITION);

    if (attr_mP)
        attributes->remove(attr_mP);

    if (!attr_mP) {
        attr_mP = attributes->add(ccl::ATTR_STD_MOTION_VERTEX_POSITION);
    }

    ccl::float3* mP = attr_mP->data_float3();

    for (size_t i = 0; i < m_cyclesMesh->motion_steps; ++i) {
        //VtVec3fArray pp;
        //pp = m_pointSamples.values.data()[i].Get<VtVec3fArray>();

        for (size_t j = 0; j < velocities.size(); ++j, ++mP) {
            *mP = vec3f_to_float3(m_points[j] + (velocities[j] * m_velocityScale));
        }
    }
}

void
HdCyclesMesh::_PopulateColors(const TfToken& name, const TfToken& role, const VtValue& data, ccl::Scene* scene,
                              HdInterpolation interpolation, const SdfPath& id)
{
    VtValue colors_value = data;

    if (!colors_value.IsHolding<VtVec3fArray>()) {
        if (!colors_value.CanCast<VtVec3fArray>()) {
            TF_WARN("Invalid color data! Can not convert color for: %s", id.GetText());
            return;
        }

        colors_value = colors_value.Cast<VtVec3fArray>();
    }

    auto override_default_shader = [scene](ccl::Mesh* mesh, ccl::Shader* new_default_shader) {
        if (mesh->used_shaders.empty()) {
            mesh->used_shaders.push_back(new_default_shader);
        } else {
            // only override if shader is a default shader
            if (mesh->used_shaders[0] == scene->default_surface) {
                mesh->used_shaders[0] = new_default_shader;
            }
        }
    };

    // Object color

    if (interpolation == HdInterpolationConstant && name == HdTokens->displayColor) {
        auto colors = colors_value.UncheckedGet<VtVec3fArray>();
        if (colors.empty()) {
            TF_WARN("Empty colors can not be assigned to an object!");
            return;
        }

        m_cyclesObject->color = ccl::make_float3(colors[0][0], colors[0][1], colors[0][2]);
        override_default_shader(m_cyclesMesh, m_object_display_color_shader);
        return;
    }

    // Primvar color attributes

    ccl::AttributeSet* attributes = &m_cyclesMesh->attributes;

    if (interpolation == HdInterpolationUniform) {
        VtValue refined_value = m_refiner->RefineUniformData(name, role, data);
        if (refined_value.GetArraySize() != m_cyclesMesh->num_triangles()) {
            TF_WARN("Empty colors can not be assigned to an faces!");
            return;
        }

        ccl::ustring attrib_name { name.GetString().c_str(), name.GetString().size() };
        ccl::Attribute* color_attrib = attributes->add(attrib_name, ccl::TypeDesc::TypeColor, ccl::ATTR_ELEMENT_FACE);
        ccl::float3* cycles_colors   = color_attrib->data_float3();

        auto refined_colors = refined_value.UncheckedGet<VtVec3fArray>();
        for (size_t i {}; i < m_cyclesMesh->num_triangles(); ++i) {
            cycles_colors[i][0] = refined_colors[i][0];
            cycles_colors[i][1] = refined_colors[i][1];
            cycles_colors[i][2] = refined_colors[i][2];
        }

        override_default_shader(m_cyclesMesh, m_attrib_display_color_shader);
        return;
    }

    auto add_vertex_or_varying_attrib = [&](const VtValue& refined_value) {
        if (!refined_value.GetArraySize()) {
            TF_WARN("Empty colors can not be assigned to an vertices!");
            return;
        }

        ccl::ustring attrib_name { name.GetString().c_str(), name.GetString().size() };
        ccl::Attribute* color_attrib = attributes->add(attrib_name, ccl::TypeDesc::TypeColor, ccl::ATTR_ELEMENT_VERTEX);
        ccl::float3* cycles_colors   = color_attrib->data_float3();

        auto refined_colors = refined_value.UncheckedGet<VtVec3fArray>();
        for (size_t i {}; i < m_cyclesMesh->verts.size(); ++i) {
            cycles_colors[i][0] = refined_colors[i][0];
            cycles_colors[i][1] = refined_colors[i][1];
            cycles_colors[i][2] = refined_colors[i][2];
        }

        override_default_shader(m_cyclesMesh, m_attrib_display_color_shader);
    };

    // varying/vertex is assigned to vertices
    if (interpolation == HdInterpolationVertex) {
        VtValue refined_value = m_refiner->RefineVertexData(name, role, data);
        if (refined_value.GetArraySize() != m_refiner->GetNumRefinedVertices()) {
            TF_WARN("Invalid number of refined vertices");
            return;
        }

        add_vertex_or_varying_attrib(refined_value);
        return;
    }

    if (interpolation == HdInterpolationVarying) {
        VtValue refined_value = m_refiner->RefineVaryingData(name, role, data);
        if (refined_value.GetArraySize() != m_refiner->GetNumRefinedVertices()) {
            TF_WARN("Invalid number of refined vertices");
            return;
        }

        add_vertex_or_varying_attrib(refined_value);
        return;
    }

    if (interpolation == HdInterpolationFaceVarying) {
        VtValue refined_value = m_refiner->RefineFaceVaryingData(name, role, data);
        if (refined_value.GetArraySize() != m_refiner->GetNumRefinedTriangles() * 3) {
            TF_WARN("Invalid number of refined vertices");
            return;
        }

        const VtVec3iArray& refined_indices = m_refiner->GetRefinedVertexIndices();

        ccl::ustring attrib_name { name.GetString().c_str(), name.GetString().size() };
        ccl::Attribute* color_attrib = attributes->add(attrib_name, ccl::TypeDesc::TypeColor, ccl::ATTR_ELEMENT_CORNER);
        ccl::float3* attrib_data     = color_attrib->data_float3();

        auto refined_color = refined_value.UncheckedGet<VtVec3fArray>();
        for (size_t i {}; i < refined_color.size(); ++i) {
            attrib_data[i][0] = refined_color[i][0];
            attrib_data[i][1] = refined_color[i][1];
            attrib_data[i][2] = refined_color[i][2];
        }

        override_default_shader(m_cyclesMesh, m_attrib_display_color_shader);
        return;
    }

    TF_WARN("Unsupported displayColor interpolation for primitive: %s", id.GetText());
}

void
HdCyclesMesh::_PopulateNormals(HdSceneDelegate* sceneDelegate, const SdfPath& id)
{
    // normals can be populated in few ways
    // * authored normals passed by primvar
    // * auto generated from limit surface for subdivision surfaces

    //
    // Auto generated normals from limit surface
    //
    if (m_refiner->IsSubdivided()) {
        assert(m_limit_us.size() == m_cyclesMesh->verts.size());
        assert(m_limit_vs.size() == m_cyclesMesh->verts.size());

        ccl::AttributeSet& attributes = m_cyclesMesh->attributes;
        ccl::Attribute* normal_attr   = attributes.add(ccl::ATTR_STD_VERTEX_NORMAL);
        ccl::float3* normal_data      = normal_attr->data_float3();

        for (size_t i {}; i < m_limit_vs.size(); ++i) {
            normal_data[i] = ccl::normalize(ccl::cross(m_limit_us[i], m_limit_vs[i]));
        }

        return;
    }

    //
    // Authored normals from Primvar
    //
    auto GetPrimvarInterpolation = [sceneDelegate, &id](HdInterpolation& interpolation) -> bool {
        for (size_t i {}; i < HdInterpolationCount; ++i) {
            HdPrimvarDescriptorVector d = sceneDelegate->GetPrimvarDescriptors(id, static_cast<HdInterpolation>(i));
            auto predicate              = [](const HdPrimvarDescriptor& d) -> bool {
                return d.name == HdTokens->normals && d.role == HdPrimvarRoleTokens->normal;
            };
            if (std::find_if(d.begin(), d.end(), predicate) != d.end()) {
                return true;
            }
        }
        return false;
    };

    HdInterpolation interpolation;
    if (!GetPrimvarInterpolation(interpolation)) {
        // TODO: Should we autogenerate normals or let Cycles generate them?
        return;
    }

    VtValue normals_value = GetNormals(sceneDelegate);
    if (normals_value.IsEmpty()) {
        TF_WARN("Empty normals!");
        return;
    }

    // not supported interpolation types
    if (interpolation == HdInterpolationFaceVarying) {
        TF_WARN("Face varying normals are not implemented!");
        return;
    }

    if (!normals_value.IsHolding<VtVec3fArray>()) {
        if (!normals_value.CanCast<VtVec3fArray>()) {
            TF_WARN("Invalid normals data! Can not convert normals for: %s", id.GetText());
            return;
        }

        normals_value = normals_value.Cast<VtVec3fArray>();
    }

    ccl::AttributeSet& attributes = m_cyclesMesh->attributes;

    if (interpolation == HdInterpolationConstant) {
        ccl::Attribute* normal_attr = attributes.add(ccl::ATTR_STD_FACE_NORMAL);
        ccl::float3* normal_data    = normal_attr->data_float3();

        const size_t num_triangles = m_refiner->GetNumRefinedTriangles();
        memset(normal_data, 0, num_triangles * sizeof(ccl::float3));

        VtValue refined_value = m_refiner->RefineConstantData(HdTokens->normals, HdPrimvarRoleTokens->normal,
                                                              normals_value);
        if (refined_value.GetArraySize() != 1) {
            TF_WARN("Invalid uniform normals for: %s", id.GetText());
            return;
        }

        VtVec3fArray refined_normals = refined_value.Get<VtVec3fArray>();
        for (int i = 0; i < num_triangles; i++) {
            normal_data[i] = vec3f_to_float3(refined_normals[i]);
        }

        return;
    }

    if (interpolation == HdInterpolationUniform) {
        ccl::Attribute* normal_attr = attributes.add(ccl::ATTR_STD_FACE_NORMAL);
        ccl::float3* normal_data    = normal_attr->data_float3();

        const size_t num_triangles = m_refiner->GetNumRefinedTriangles();
        memset(normal_data, 0, num_triangles * sizeof(ccl::float3));

        VtValue refined_value = m_refiner->RefineVertexData(HdTokens->normals, HdPrimvarRoleTokens->normal,
                                                            normals_value);
        if (refined_value.GetArraySize() != num_triangles) {
            TF_WARN("Invalid uniform normals for: %s", id.GetText());
            return;
        }

        VtVec3fArray refined_normals = refined_value.Get<VtVec3fArray>();
        for (int i = 0; i < num_triangles; i++) {
            normal_data[i] = vec3f_to_float3(refined_normals[i]);
        }

        return;
    }

    if (interpolation == HdInterpolationVertex || interpolation == HdInterpolationVarying) {
        ccl::Attribute* normal_attr = attributes.add(ccl::ATTR_STD_VERTEX_NORMAL);
        ccl::float3* normal_data    = normal_attr->data_float3();

        const size_t num_vertices = m_refiner->GetNumRefinedVertices();
        memset(normal_data, 0, num_vertices * sizeof(ccl::float3));

        VtValue refined_value;
        if (interpolation == HdInterpolationVertex) {
            refined_value = m_refiner->RefineVertexData(HdTokens->normals, HdPrimvarRoleTokens->normal, normals_value);
        } else {
            refined_value = m_refiner->RefineVaryingData(HdTokens->normals, HdPrimvarRoleTokens->normal, normals_value);
        }

        if (refined_value.GetArraySize() != num_vertices) {
            TF_WARN("Invalid vertex normals for: %s", id.GetText());
            return;
        }

        VtVec3fArray refined_normals = refined_value.Get<VtVec3fArray>();
        for (size_t i {}; i < num_vertices; ++i) {
            normal_data[i] = vec3f_to_float3(refined_normals[i]);
        }

        return;
    }
}

ccl::Mesh*
HdCyclesMesh::_CreateCyclesMesh()
{
    ccl::Mesh* mesh = new ccl::Mesh();
    mesh->clear();

    if (m_useMotionBlur && m_useDeformMotionBlur) {
        mesh->use_motion_blur = true;
    }

    mesh->subdivision_type = ccl::Mesh::SUBDIVISION_NONE;
    return mesh;
}

ccl::Object*
HdCyclesMesh::_CreateCyclesObject()
{
    ccl::Object* object = new ccl::Object();

    object->tfm     = ccl::transform_identity();
    object->pass_id = -1;

    object->visibility = ccl::PATH_RAY_ALL_VISIBILITY;

    return object;
}

void
HdCyclesMesh::_PopulateMotion()
{
    if (m_pointSamples.count <= 1) {
        return;
    }

    ccl::AttributeSet* attributes = &m_cyclesMesh->attributes;

    m_cyclesMesh->use_motion_blur = true;

    m_cyclesMesh->motion_steps = m_pointSamples.count + 1;

    ccl::Attribute* attr_mP = attributes->find(ccl::ATTR_STD_MOTION_VERTEX_POSITION);

    if (attr_mP)
        attributes->remove(attr_mP);

    if (!attr_mP) {
        attr_mP = attributes->add(ccl::ATTR_STD_MOTION_VERTEX_POSITION);
    }

    ccl::float3* mP = attr_mP->data_float3();
    for (size_t i = 0; i < m_pointSamples.count; ++i) {
        if (m_pointSamples.times.data()[i] == 0.0f)
            continue;

        VtVec3fArray pp;
        pp = m_pointSamples.values.data()[i].Get<VtVec3fArray>();

        for (size_t j = 0; j < m_refiner->GetNumRefinedVertices(); ++j, ++mP) {
            *mP = vec3f_to_float3(pp[j]);
        }
    }
}

void
HdCyclesMesh::_PopulateTopology(HdSceneDelegate* sceneDelegate, const SdfPath& id)
{
    HdMeshTopology topology = GetMeshTopology(sceneDelegate);
    topology.SetSubdivTags(GetSubdivTags(sceneDelegate));

    HdDisplayStyle display_style = sceneDelegate->GetDisplayStyle(id);
#ifdef USE_USD_CYCLES_SCHEMA
    auto refine_value         = GetPrimvar(sceneDelegate, usdCyclesTokens->primvarsCyclesMeshSubdivision_max_level);
    int refine_level          = refine_value.IsEmpty() ? 0 : refine_value.Cast<int>().UncheckedGet<int>();
    display_style.refineLevel = refine_level;
#endif  // USE_USD_CYCLES_SCHEMA

    // Refiner holds pointer to topology therefore refiner can't outlive the topology
    m_topology = HdMeshTopology(topology, display_style.refineLevel);
    m_refiner  = HdCyclesMeshRefiner::Create(m_topology, id);

    // Mesh is independently updated in two stages, faces(topology) and vertices(data).
    // Because process of updating vertices can fail for unknown reason,
    // we can end up with an empty vertex array. Indices must point to a valid vertex array(resize).
    m_cyclesMesh->clear();
    m_cyclesMesh->resize_mesh(m_refiner->GetNumRefinedVertices(), m_refiner->GetNumRefinedTriangles());

    const VtVec3iArray& refined_indices = m_refiner->GetRefinedVertexIndices();
    for (size_t i {}; i < refined_indices.size(); ++i) {
        const GfVec3i& triangle_indices = refined_indices[i];

        m_cyclesMesh->triangles[i * 3 + 0] = triangle_indices[0];
        m_cyclesMesh->triangles[i * 3 + 1] = triangle_indices[1];
        m_cyclesMesh->triangles[i * 3 + 2] = triangle_indices[2];

        m_cyclesMesh->smooth[i] = true;  // TODO: move to Populate normals?
    }
}

void
HdCyclesMesh::_PopulateMaterials(HdSceneDelegate* sceneDelegate, HdCyclesRenderParam* renderParam,
                                 ccl::Shader* default_surface, const SdfPath& id)
{
    // Any topology change will mark MaterialId as dirty and automatically trigger material discovery.
    // During topology population process, material id for each face is set to 0.
    // That means default shader must be always present under 0 index in the shader table.
    // Object material discovery overrides the default shader.
    // SubSet material discovery appends to the shaders table and overrides materials ids for subset faces.
    // This behaviour is to cover a corner case, where there is no object material, but there is a sub set,
    // that does not assign materials to all faces.
    m_cyclesMesh->used_shaders = { default_surface };

    constexpr int default_shader_id = 0;
    for (size_t i {}; i < m_cyclesMesh->num_triangles(); ++i) {
        m_cyclesMesh->shader[i] = default_shader_id;
    }

    _PopulateObjectMaterial(sceneDelegate, id);
    _PopulateSubSetsMaterials(sceneDelegate, id);

    renderParam->UpdateShadersTag(m_cyclesMesh->used_shaders);
}

void
HdCyclesMesh::_PopulateObjectMaterial(HdSceneDelegate* sceneDelegate, const SdfPath& id)
{
    HdRenderIndex& render_index = sceneDelegate->GetRenderIndex();

    // object material overrides face materials
    auto& used_shaders = m_cyclesMesh->used_shaders;

    const SdfPath& material_id = sceneDelegate->GetMaterialId(id);
    if (material_id.IsEmpty()) {
        return;
    }

    // search for state primitive that contains cycles shader
    const HdSprim* material = render_index.GetSprim(HdPrimTypeTokens->material, material_id);
    auto cycles_material    = dynamic_cast<const HdCyclesMaterial*>(material);
    if (!cycles_material) {
        TF_WARN("Invalid HdCycles material %s", material_id.GetText());
        return;
    }

    ccl::Shader* cycles_shader = cycles_material->GetCyclesShader();
    if (!cycles_shader) {
        return;
    }

    // override default material
    used_shaders[0] = cycles_shader;
}

void
HdCyclesMesh::_PopulateSubSetsMaterials(HdSceneDelegate* sceneDelegate, const SdfPath& id)
{
    // optimization to avoid unnecessary allocations
    if (m_topology.GetGeomSubsets().empty()) {
        return;
    }

    HdRenderIndex& render_index = sceneDelegate->GetRenderIndex();

    // collect unrefined material ids for each face
    VtIntArray face_materials(m_topology.GetNumFaces(), 0);

    auto& used_shaders = m_cyclesMesh->used_shaders;
    TfHashMap<SdfPath, int, SdfPath::Hash> material_map;
    for (auto& subset : m_topology.GetGeomSubsets()) {
        int subset_material_id = 0;

        if (!subset.materialId.IsEmpty()) {
            const HdSprim* state_prim = render_index.GetSprim(HdPrimTypeTokens->material, subset.materialId);
            auto sub_mat              = dynamic_cast<const HdCyclesMaterial*>(state_prim);

            if (!sub_mat)
                continue;
            if (!sub_mat->GetCyclesShader())
                continue;

            auto search_it = material_map.find(subset.materialId);
            if (search_it == material_map.end()) {
                used_shaders.push_back(sub_mat->GetCyclesShader());
                material_map[subset.materialId] = used_shaders.size();
                subset_material_id              = used_shaders.size();
            } else {
                subset_material_id = search_it->second;
            }
        }

        for (int i : subset.indices) {
            face_materials[i] = std::max(subset_material_id - 1, 0);
        }
    }

    // no subset materials discovered, no refinement required
    if (used_shaders.empty()) {
        return;
    }

    // refine material ids and assign them to refined geometry
    VtValue refined_value = m_refiner->RefineUniformData(HdTokens->materialParams, HdPrimvarRoleTokens->none,
                                                         VtValue { face_materials });

    if (refined_value.GetArraySize() != m_cyclesMesh->shader.size()) {
        TF_WARN("Failed to assign refined materials for: %s", id.GetText());
        return;
    }

    auto refined_material_ids = refined_value.UncheckedGet<VtIntArray>();
    for (size_t i {}; i < refined_material_ids.size(); ++i) {
        m_cyclesMesh->shader[i] = refined_material_ids[i];
    }
}

void
HdCyclesMesh::_PopulatePrimvars(HdSceneDelegate* sceneDelegate, ccl::Scene* scene, const SdfPath& id,
                                HdDirtyBits* dirtyBits)
{
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
            if (!HdChangeTracker::IsPrimvarDirty(*dirtyBits, id, description.name)) {
                continue;
            }

            // Those are special primvars
            if (description.name == HdTokens->points || description.name == HdTokens->normals) {
                continue;
            }

            auto interpolation = interpolation_description.first;
            auto value         = GetPrimvar(sceneDelegate, description.name);

            if (description.name == HdTokens->displayColor || description.role == HdPrimvarRoleTokens->color) {
                _PopulateColors(description.name, description.role, value, scene, interpolation, id);
                continue;
            }

            if (description.role == HdPrimvarRoleTokens->textureCoordinate) {
                _AddUVSet(description.name, value, scene, interpolation);
                continue;
            }

            // TODO: Add arbitrary primvar support when AOVs are working
        }
    }
}

void
HdCyclesMesh::_PopulateVertices(HdSceneDelegate* sceneDelegate, const SdfPath& id, HdDirtyBits* dirtyBits)
{
    VtValue points_value;

    //
    // Vertices from Usd Skel
    //
    bool points_computed     = false;
    auto extComputationDescs = sceneDelegate->GetExtComputationPrimvarDescriptors(id, HdInterpolationVertex);
    for (auto& desc : extComputationDescs) {
        if (desc.name != HdTokens->points) {
            continue;
        }

        if (HdChangeTracker::IsPrimvarDirty(*dirtyBits, id, desc.name)) {
            auto valueStore   = HdExtComputationUtils::GetComputedPrimvarValues({ desc }, sceneDelegate);
            auto pointValueIt = valueStore.find(desc.name);
            if (pointValueIt != valueStore.end()) {
                if (!pointValueIt->second.IsEmpty()) {
                    points_value    = pointValueIt->second;
                    points_computed = true;
                }
            }
        }
        break;
    }

    //
    // Vertices from PrimVar
    //
    if (!points_computed) {
        points_value = GetPrimvar(sceneDelegate, HdTokens->points);
    }

    if (!points_value.IsHolding<VtVec3fArray>()) {
        if (!points_value.CanCast<VtVec3fArray>()) {
            TF_WARN("Invalid points data! Can not convert points for: %s", id.GetText());
            return;
        }

        points_value = points_value.Cast<VtVec3fArray>();
    }

    if (!points_value.IsHolding<VtVec3fArray>()) {
        if (!points_value.CanCast<VtVec3fArray>()) {
            TF_WARN("Invalid point data! Can not convert points for: %s", id.GetText());
            return;
        }

        points_value = points_value.Cast<VtVec3fArray>();
    }

    VtVec3fArray points;
    VtValue refined_points_value = m_refiner->RefineVertexData(HdTokens->points, HdPrimvarRoleTokens->point,
                                                               points_value);
    if (refined_points_value.IsHolding<VtVec3fArray>()) {
        points = refined_points_value.Get<VtVec3fArray>();
    }

    for (size_t i {}; i < points.size(); ++i) {
        const GfVec3f& point   = points[i];
        m_cyclesMesh->verts[i] = ccl::make_float3(point[0], point[1], point[2]);
    }

    //
    // Compute limit attributes once, then in the FinishMesh clean up the data
    //
    if (m_refiner->IsSubdivided()) {
        Vt_ArrayForeignDataSource foreign_data_source {};
        VtFloat3Array refined_vertices { &foreign_data_source, m_cyclesMesh->verts.data(), m_cyclesMesh->verts.size(),
                                         false };

        VtFloat3Array limit_ps(refined_vertices.size());
        m_limit_us.resize(refined_vertices.size());
        m_limit_vs.resize(refined_vertices.size());
        m_refiner->EvaluateLimit(refined_vertices, limit_ps, m_limit_us, m_limit_vs);
        std::memcpy(m_cyclesMesh->verts.data(), limit_ps.data(), limit_ps.size() * sizeof(ccl::float3));
    }

    // TODO: populate motion ?
}

void
HdCyclesMesh::_PopulateGenerated(ccl::Scene* scene)
{
    if (m_cyclesMesh->need_attribute(scene, ccl::ATTR_STD_GENERATED)) {
        ccl::float3 loc, size;
        HdCyclesMeshTextureSpace(m_cyclesMesh, loc, size);

        ccl::AttributeSet* attributes = &m_cyclesMesh->attributes;
        ccl::Attribute* attr          = attributes->add(ccl::ATTR_STD_GENERATED);

        ccl::float3* generated = attr->data_float3();
        for (int i = 0; i < m_cyclesMesh->verts.size(); i++) {
            generated[i] = m_cyclesMesh->verts[i] * size - loc;
        }
    }
}

void
HdCyclesMesh::_FinishMesh(ccl::Scene* scene)
{
    // cleanup limit surface temporary data
    m_limit_us = {};
    m_limit_vs = {};

    // This must be done first, because HdCyclesMeshTextureSpace requires computed min/max
    m_cyclesMesh->compute_bounds();

    _PopulateGenerated(scene);
}

void
HdCyclesMesh::_InitializeNewCyclesMesh()
{
    if (m_cyclesMesh) {
        m_renderDelegate->GetCyclesRenderParam()->RemoveMesh(m_cyclesMesh);
        delete m_cyclesMesh;
    }

    if (m_cyclesObject) {
        m_renderDelegate->GetCyclesRenderParam()->RemoveObject(m_cyclesObject);
        delete m_cyclesObject;
    }

    m_cyclesObject        = _CreateCyclesObject();
    m_cyclesMesh          = _CreateCyclesMesh();
    m_numTransformSamples = HD_CYCLES_MOTION_STEPS;

    if (m_useMotionBlur) {
        // Motion steps are currently a static const compile time
        // variable... This is likely an issue...
        // TODO: Get this from usdCycles schema
        //m_motionSteps = config.motion_steps;
        m_motionSteps = m_numTransformSamples;

        // Hardcoded for now until schema PR
        m_useDeformMotionBlur = true;

        // TODO: Needed when we properly handle motion_verts
        m_cyclesMesh->motion_steps    = m_motionSteps;
        m_cyclesMesh->use_motion_blur = m_useDeformMotionBlur;
    }

    m_cyclesObject->geometry = m_cyclesMesh;

    m_renderDelegate->GetCyclesRenderParam()->AddGeometry(m_cyclesMesh);
    m_renderDelegate->GetCyclesRenderParam()->AddObject(m_cyclesObject);
}

void
HdCyclesMesh::_InitRepr(TfToken const& reprToken, HdDirtyBits* dirtyBits)
{
}

HdDirtyBits
HdCyclesMesh::GetInitialDirtyBitsMask() const
{
    return HdChangeTracker::Clean | HdChangeTracker::DirtyPoints | HdChangeTracker::DirtyTransform
           | HdChangeTracker::DirtyPrimvar | HdChangeTracker::DirtyTopology | HdChangeTracker::DirtyVisibility
           | HdChangeTracker::DirtyMaterialId | HdChangeTracker::DirtySubdivTags | HdChangeTracker::DirtyPrimID
           | HdChangeTracker::DirtyDisplayStyle | HdChangeTracker::DirtyDoubleSided;
}

HdDirtyBits
HdCyclesMesh::_PropagateDirtyBits(HdDirtyBits bits) const
{
#ifdef USE_USD_CYCLES_SCHEMA
    // Usd controls subdivision level globally, passed through the topology. Change of custom max subdiv level primvar
    // must mark SubdivTags dirty.
    if (HdChangeTracker::IsPrimvarDirty(bits, GetId(), usdCyclesTokens->primvarsCyclesMeshSubdivision_max_level)) {
        bits |= HdChangeTracker::DirtySubdivTags;
    }
#endif  // USE_USD_CYCLES_SCHEMA

    // subdivision request requires full topology update
    if (bits & HdChangeTracker::DirtySubdivTags) {
        bits |= (HdChangeTracker::DirtyPoints | HdChangeTracker::DirtyNormals | HdChangeTracker::DirtyPrimvar
                 | HdChangeTracker::DirtyTopology | HdChangeTracker::DirtyDisplayStyle);
    }

    // We manage face sets materials, when topology changes we need to trigger face materials update
    if (bits & HdChangeTracker::DirtyTopology) {
        bits |= HdChangeTracker::DirtyMaterialId;
    }

    if (bits & HdChangeTracker::DirtyMaterialId) {
        bits |= (HdChangeTracker::DirtyPoints | HdChangeTracker::DirtyNormals | HdChangeTracker::DirtyPrimvar
                 | HdChangeTracker::DirtyTopology);
    }

    return bits;
}

void
HdCyclesMesh::Sync(HdSceneDelegate* sceneDelegate, HdRenderParam* renderParam, HdDirtyBits* dirtyBits,
                   TfToken const& reprToken)
{
    auto param                    = dynamic_cast<HdCyclesRenderParam*>(renderParam);
    m_object_display_color_shader = param->default_object_display_color_surface;
    m_attrib_display_color_shader = param->default_attrib_display_color_surface;

    ccl::Scene* scene = param->GetCyclesScene();
    const SdfPath& id = GetId();

    // -------------------------------------
    // -- Resolve Drawstyles
#ifdef USE_USD_CYCLES_SCHEMA

    std::map<HdInterpolation, HdPrimvarDescriptorVector> primvarDescsPerInterpolation = {
        { HdInterpolationFaceVarying, sceneDelegate->GetPrimvarDescriptors(id, HdInterpolationFaceVarying) },
        { HdInterpolationVertex, sceneDelegate->GetPrimvarDescriptors(id, HdInterpolationVertex) },
        { HdInterpolationConstant, sceneDelegate->GetPrimvarDescriptors(id, HdInterpolationConstant) },
        { HdInterpolationUniform, sceneDelegate->GetPrimvarDescriptors(id, HdInterpolationUniform) },

    };

    for (auto& primvarDescsEntry : primvarDescsPerInterpolation) {
        for (auto& pv : primvarDescsEntry.second) {
            // Mesh Specific

            m_useMotionBlur = _HdCyclesGetMeshParam<bool>(pv, dirtyBits, id, this, sceneDelegate,
                                                          usdCyclesTokens->primvarsCyclesObjectMblur, m_useMotionBlur);

            m_useDeformMotionBlur = _HdCyclesGetMeshParam<bool>(pv, dirtyBits, id, this, sceneDelegate,
                                                                usdCyclesTokens->primvarsCyclesObjectMblurDeform,
                                                                m_useDeformMotionBlur);

            m_motionSteps = _HdCyclesGetMeshParam<bool>(pv, dirtyBits, id, this, sceneDelegate,
                                                        usdCyclesTokens->primvarsCyclesObjectMblurSteps, m_motionSteps);

            // Object Generic

            m_cyclesObject->is_shadow_catcher
                = _HdCyclesGetMeshParam<bool>(pv, dirtyBits, id, this, sceneDelegate,
                                              usdCyclesTokens->primvarsCyclesObjectIs_shadow_catcher,
                                              m_cyclesObject->is_shadow_catcher);

            m_cyclesObject->pass_id = _HdCyclesGetMeshParam<bool>(pv, dirtyBits, id, this, sceneDelegate,
                                                                  usdCyclesTokens->primvarsCyclesObjectPass_id,
                                                                  m_cyclesObject->pass_id);

            m_cyclesObject->use_holdout = _HdCyclesGetMeshParam<bool>(pv, dirtyBits, id, this, sceneDelegate,
                                                                      usdCyclesTokens->primvarsCyclesObjectUse_holdout,
                                                                      m_cyclesObject->use_holdout);

            // Visibility

            m_visibilityFlags = 0;

            m_visCamera = _HdCyclesGetMeshParam<bool>(pv, dirtyBits, id, this, sceneDelegate,
                                                      usdCyclesTokens->primvarsCyclesObjectVisibilityCamera,
                                                      m_visCamera);

            m_visDiffuse = _HdCyclesGetMeshParam<bool>(pv, dirtyBits, id, this, sceneDelegate,
                                                       usdCyclesTokens->primvarsCyclesObjectVisibilityDiffuse,
                                                       m_visDiffuse);

            m_visGlossy = _HdCyclesGetMeshParam<bool>(pv, dirtyBits, id, this, sceneDelegate,
                                                      usdCyclesTokens->primvarsCyclesObjectVisibilityGlossy,
                                                      m_visGlossy);

            m_visScatter = _HdCyclesGetMeshParam<bool>(pv, dirtyBits, id, this, sceneDelegate,
                                                       usdCyclesTokens->primvarsCyclesObjectVisibilityScatter,
                                                       m_visScatter);

            m_visShadow = _HdCyclesGetMeshParam<bool>(pv, dirtyBits, id, this, sceneDelegate,
                                                      usdCyclesTokens->primvarsCyclesObjectVisibilityShadow,
                                                      m_visShadow);

            m_visTransmission = _HdCyclesGetMeshParam<bool>(pv, dirtyBits, id, this, sceneDelegate,
                                                            usdCyclesTokens->primvarsCyclesObjectVisibilityTransmission,
                                                            m_visTransmission);

            m_visibilityFlags |= m_visCamera ? ccl::PATH_RAY_CAMERA : 0;
            m_visibilityFlags |= m_visDiffuse ? ccl::PATH_RAY_DIFFUSE : 0;
            m_visibilityFlags |= m_visGlossy ? ccl::PATH_RAY_GLOSSY : 0;
            m_visibilityFlags |= m_visScatter ? ccl::PATH_RAY_VOLUME_SCATTER : 0;
            m_visibilityFlags |= m_visShadow ? ccl::PATH_RAY_SHADOW : 0;
            m_visibilityFlags |= m_visTransmission ? ccl::PATH_RAY_TRANSMIT : 0;
        }
    }
#endif

    if (*dirtyBits & HdChangeTracker::DirtyVisibility) {
        _sharedData.visible = sceneDelegate->GetVisible(id);
        _UpdateObject(scene, param, dirtyBits);
        if (!_sharedData.visible) {
            return;
        }
    }

    if (HdChangeTracker::IsTopologyDirty(*dirtyBits, id)) {
        _PopulateTopology(sceneDelegate, id);
    }

    if (*dirtyBits & HdChangeTracker::DirtyMaterialId) {
        _PopulateMaterials(sceneDelegate, param, scene->default_surface, id);
    }

    // For subdivided meshes, data conversion has to happen in a specific order:
    // 1) vertices - generate limit surface position and tangents
    // 2) normals - limit surface tangents are used to generate normals
    // 3) uvs - limit surface tangents
    // After conversion, class members that hold du and dv are cleared in FinishMesh.
    if (*dirtyBits & HdChangeTracker::DirtyPoints) {
        _PopulateVertices(sceneDelegate, id, dirtyBits);
    }

    if (*dirtyBits & HdChangeTracker::DirtyNormals) {
        _PopulateNormals(sceneDelegate, id);
    }

    if (*dirtyBits & HdChangeTracker::DirtyPrimvar) {
        _PopulatePrimvars(sceneDelegate, scene, id, dirtyBits);
    }

    if (*dirtyBits & HdChangeTracker::DirtyDoubleSided) {
        //         m_doubleSided = sceneDelegate->GetDoubleSided(id);
    }

    if (*dirtyBits & HdChangeTracker::DirtyTransform) {
        m_transformSamples = HdCyclesSetTransform(m_cyclesObject, sceneDelegate, id, m_useMotionBlur);
    }

    if (*dirtyBits & HdChangeTracker::DirtyPrimID) {
        // Offset of 1 added because Cycles primId pass needs to be shifted down to -1
        m_cyclesObject->pass_id = this->GetPrimId() + 1;
    }

    // -------------------------------------
    // -- Handle point instances
    if (*dirtyBits & HdChangeTracker::DirtyInstancer) {
        if (auto instancer = static_cast<HdCyclesInstancer*>(
                sceneDelegate->GetRenderIndex().GetInstancer(GetInstancerId()))) {
            auto instanceTransforms = instancer->SampleInstanceTransforms(id);
            auto newNumInstances    = (instanceTransforms.count > 0) ? instanceTransforms.values[0].size() : 0;
            // Clear all instances...
            if (m_cyclesInstances.size() > 0) {
                for (auto instance : m_cyclesInstances) {
                    if (instance) {
                        m_renderDelegate->GetCyclesRenderParam()->RemoveObject(instance);
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
                        || (m_transformSamples.count == 1 && (m_transformSamples.values[0] == GfMatrix4d(1)))) {
                        for (size_t j = 0; j < instanceTransforms.count; ++j) {
                            instanceTransform[j] = instanceTransforms.values[j][i];
                        }
                    } else {
                        for (size_t j = 0; j < instanceTransforms.count; ++j) {
                            GfMatrix4d xf_j      = m_transformSamples.Resample(instanceTransforms.times[j]);
                            instanceTransform[j] = xf_j * instanceTransforms.values[j][i];
                        }
                    }
                }

                for (int j = 0; j < newNumInstances; ++j) {
                    ccl::Object* instanceObj = _CreateCyclesObject();

                    instanceObj->tfm      = mat4d_to_transform(combinedTransforms[j].data()[0]);
                    instanceObj->geometry = m_cyclesMesh;

                    // TODO: Implement motion blur for point instanced objects
                    /*if (m_useMotionBlur) {
                        m_cyclesMesh->motion_steps    = m_motionSteps;
                        m_cyclesMesh->use_motion_blur = m_useMotionBlur;

                        instanceObj->motion.clear();
                        instanceObj->motion.resize(m_motionSteps);
                        for (int j = 0; j < m_motionSteps; j++) {
                            instanceObj->motion[j] = mat4d_to_transform(
                                combinedTransforms[j].data()[j]);
                        }
                    }*/

                    m_cyclesInstances.push_back(instanceObj);

                    m_renderDelegate->GetCyclesRenderParam()->AddObject(instanceObj);
                }

                // Hide prototype
                if (m_cyclesObject)
                    m_visibilityFlags = 0;
            }
        }
    }

    _FinishMesh(scene);
    _UpdateObject(scene, param, dirtyBits);
    *dirtyBits = HdChangeTracker::Clean;
}

void
HdCyclesMesh::_UpdateObject(ccl::Scene* scene, HdCyclesRenderParam* param, HdDirtyBits* dirtyBits)
{
    m_cyclesObject->visibility = _sharedData.visible ? m_visibilityFlags : 0;
    m_cyclesMesh->tag_update(scene, true);
    m_cyclesObject->tag_update(scene);

    // Mark visibility clean. When sync method is called object might be invisible. At that point we do not
    // need to trigger the topology and data generation. It can be postponed until visibility becomes on.
    // We need to manually mark visibility clean, but other flags remain dirty.
    if (!_sharedData.visible) {
        *dirtyBits &= ~HdChangeTracker::DirtyVisibility;
    }

    param->Interrupt();
}

namespace {

template<typename From>
VtValue
value_to_vec3f_cast(const VtValue& input)
{
    auto& array = input.UncheckedGet<VtArray<From>>();
    VtArray<GfVec3f> output(array.size());
    std::transform(array.begin(), array.end(), output.begin(), [](const From& val) -> GfVec3f {
        return { static_cast<float>(val), static_cast<float>(val), static_cast<float>(val) };
    });
    return VtValue { output };
}

template<typename From>
VtValue
vec2T_to_vec3f_cast(const VtValue& input)
{
    auto& array = input.UncheckedGet<VtArray<From>>();

    VtArray<GfVec3f> output(array.size());
    std::transform(array.begin(), array.end(), output.begin(), [](const From& val) -> GfVec3f {
        return { static_cast<float>(val[0]), static_cast<float>(val[1]), 0.0 };
    });
    return VtValue { output };
};

template<typename From>
VtValue
vec3T_to_vec3f_cast(const VtValue& input)
{
    auto& array = input.UncheckedGet<VtArray<From>>();

    VtArray<GfVec3f> output(array.size());
    std::transform(array.begin(), array.end(), output.begin(), [](const From& val) -> GfVec3f {
        return { static_cast<float>(val[0]), static_cast<float>(val[1]), static_cast<float>(val[2]) };
    });
    return VtValue { output };
};

}  // namespace

TF_REGISTRY_FUNCTION_WITH_TAG(VtValue, HdCyclesMesh)
{
    VtValue::RegisterCast<VtArray<int>, VtArray<GfVec3f>>(&value_to_vec3f_cast<int>);
    VtValue::RegisterCast<VtArray<bool>, VtArray<GfVec3f>>(&value_to_vec3f_cast<bool>);
    VtValue::RegisterCast<VtArray<float>, VtArray<GfVec3f>>(&value_to_vec3f_cast<float>);

    VtValue::RegisterCast<VtArray<GfVec2i>, VtArray<GfVec3f>>(&vec2T_to_vec3f_cast<GfVec2i>);
    VtValue::RegisterCast<VtArray<GfVec3i>, VtArray<GfVec3f>>(&vec3T_to_vec3f_cast<GfVec3i>);
    VtValue::RegisterCast<VtArray<GfVec4i>, VtArray<GfVec3f>>(&vec3T_to_vec3f_cast<GfVec4i>);
}

PXR_NAMESPACE_CLOSE_SCOPE
