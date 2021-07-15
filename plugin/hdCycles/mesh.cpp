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

#include "attributeSource.h"
#include "config.h"
#include "debug_codes.h"
#include "instancer.h"
#include "material.h"
#include "meshSource.h"
#include "renderDelegate.h"
#include "renderParam.h"
#include "transformSource.h"
#include "utils.h"

#include <render/instance_group.h>

#include <pxr/imaging/hd/extComputationUtils.h>

#include <usdCycles/tokens.h>

PXR_NAMESPACE_OPEN_SCOPE

// clang-format off
#ifdef __GNUC__
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-variable"
#endif
TF_DEFINE_PRIVATE_TOKENS(_tokens, 
    (st)
    (uv)
);
#ifdef __GNUC__
#pragma GCC diagnostic pop
#endif
// clang-format on

HdCyclesMesh::HdCyclesMesh(SdfPath const& id, SdfPath const& instancerId, HdCyclesRenderDelegate* a_renderDelegate)
    : HdBbRPrim<HdMesh>(id, instancerId)
    , m_cyclesMesh(nullptr)
    , m_velocityScale(1.0f)
    , m_renderDelegate(a_renderDelegate)
{
    _InitializeNewCyclesMesh();
}

HdCyclesMesh::~HdCyclesMesh()
{
    if (m_cyclesMesh) {
        m_renderDelegate->GetCyclesRenderParam()->RemoveGeometrySafe(m_cyclesMesh);
        delete m_cyclesMesh;
    }

    if (m_cyclesObject) {
        m_renderDelegate->GetCyclesRenderParam()->RemoveObjectSafe(m_cyclesObject);
        delete m_cyclesObject;
    }

    if (m_cyclesInstances.size()) {
        m_renderDelegate->GetCyclesRenderParam()->RemoveObjectArray(m_cyclesInstances);
        std::vector<ccl::Object> empty = {};
        m_cyclesInstances.swap(empty);
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
    bool need_uv = m_cyclesMesh->need_attribute(scene, uv_name)
                   || m_cyclesMesh->need_attribute(scene, ccl::ATTR_STD_UV);
    if (!need_uv) {
        return;
    }

    // To avoid face varying computations we take attribute and we refine it with
    // respecting incoming interpolation. Then we convert it to face varying because
    // ATTR_STD_UV is a face varying data.

    ccl::AttributeSet* attributes = &m_cyclesMesh->attributes;
    ccl::Attribute* uv_attr = attributes->add(ccl::ATTR_STD_UV, uv_name);
    auto attrib_data = uv_attr->data_float2();

    const HdCyclesMeshRefiner* refiner = m_topology->GetRefiner();

    if (interpolation == HdInterpolationConstant) {
        VtValue refined_value = refiner->RefineConstantData(name, HdPrimvarRoleTokens->textureCoordinate, uvs_value);
        if (refined_value.GetArraySize() != 1) {
            TF_WARN("Failed to refine constant texture coordinates!");
            return;
        }

        auto refined_uvs = refined_value.UncheckedGet<VtVec2fArray>();
        for (size_t face = 0, offset = 0; face < refiner->GetTriangulatedTopology().GetNumFaces(); ++face) {
            for (size_t i = 0; i < 3; ++i, ++offset) {
                attrib_data[offset][0] = refined_uvs[0][0];
                attrib_data[offset][1] = refined_uvs[0][1];
            }
        }
    }

    if (interpolation == HdInterpolationUniform) {
        VtValue refined_value = refiner->RefineUniformData(name, HdPrimvarRoleTokens->textureCoordinate, uvs_value);
        if (refined_value.GetArraySize() != m_cyclesMesh->num_triangles()) {
            TF_WARN("Failed to refine uniform texture coordinates!");
            return;
        }

        auto refined_uvs = refined_value.UncheckedGet<VtVec2fArray>();
        for (size_t face = 0, offset = 0; face < refiner->GetTriangulatedTopology().GetNumFaces(); ++face) {
            for (size_t i = 0; i < 3; ++i, ++offset) {
                attrib_data[offset][0] = refined_uvs[face][0];
                attrib_data[offset][1] = refined_uvs[face][1];
            }
        }
        return;
    }

    // convert vertex and varying

    auto add_vertex_or_varying_attrib = [&](const VtValue& refined_value) {
        auto refined_uvs = refined_value.UncheckedGet<VtVec2fArray>();
        const VtIntArray& refined_indices = refiner->GetTriangulatedTopology().GetFaceVertexIndices();
        for (size_t offset = 0; offset < refined_indices.size(); ++offset) {
            const int& vertex_index = refined_indices[offset];
            attrib_data[offset][0] = refined_uvs[vertex_index][0];
            attrib_data[offset][1] = refined_uvs[vertex_index][1];
        }
    };

    if (interpolation == HdInterpolationVertex) {
        VtValue refined_value = refiner->RefineVertexData(name, HdPrimvarRoleTokens->textureCoordinate, uvs_value);
        if (refined_value.GetArraySize() != m_cyclesMesh->verts.size()) {
            TF_WARN("Failed to refine vertex texture coordinates!");
            return;
        }

        add_vertex_or_varying_attrib(refined_value);
        return;
    }

    if (interpolation == HdInterpolationVarying) {
        VtValue refined_value = refiner->RefineVaryingData(name, HdPrimvarRoleTokens->textureCoordinate, uvs_value);
        if (refined_value.GetArraySize() != m_cyclesMesh->verts.size()) {
            TF_WARN("Failed to refine varying texture coordinates!");
            return;
        }

        add_vertex_or_varying_attrib(refined_value);
        return;
    }

    if (interpolation == HdInterpolationFaceVarying) {
        VtValue refined_value = refiner->RefineFaceVaryingData(name, HdPrimvarRoleTokens->textureCoordinate, uvs_value);
        if (refined_value.GetArraySize() != refiner->GetTriangulatedTopology().GetNumFaces() * 3) {
            TF_WARN("Invalid number of refined vertices");
            return;
        }

        auto refined_uvs = refined_value.UncheckedGet<VtVec2fArray>();
        for (size_t i = 0; i < refined_uvs.size(); ++i) {
            attrib_data[i][0] = refined_uvs[i][0];
            attrib_data[i][1] = refined_uvs[i][1];
        }

        return;
    }
}

void
HdCyclesMesh::_PopulateTangents(HdSceneDelegate* sceneDelegate, const SdfPath& id, ccl::Scene* scene)
{
    // Iterate over all uvs and check if tangent is requested, populate primvars must be called before
    // PopulateTangents

    ccl::AttributeSet* attributes = &m_cyclesMesh->attributes;
    const HdCyclesMeshRefiner* refiner = m_topology->GetRefiner();

    for (const ccl::ustring& name : m_texture_names) {
        ccl::ustring tangent_name = ccl::ustring(name.string() + ".tangent");
        ccl::ustring sign_name = ccl::ustring(name.string() + ".tangent_sign");
        bool need_tangent = false;
        need_tangent |= m_cyclesMesh->need_attribute(scene, tangent_name);
        need_tangent |= m_cyclesMesh->need_attribute(scene, ccl::ATTR_STD_UV_TANGENT);

        if (!need_tangent) {
            continue;
        }

        // Take tangent from subdivision limit surface
        if (refiner->IsSubdivided()) {
            // subdivided tangents are per vertex

            if (m_cyclesMesh->need_attribute(scene, ccl::ATTR_STD_UV_TANGENT)) {
                ccl::Attribute* tangent_attrib = attributes->add(ccl::ATTR_STD_UV_TANGENT, tangent_name);
                ccl::float3* tangent_data = tangent_attrib->data_float3();

                for (size_t i = 0; i < m_cyclesMesh->triangles.size(); ++i) {
                    auto vertex_index = m_cyclesMesh->triangles[i];
                    tangent_data[i] = m_limit_us[vertex_index];
                }
            }

            if (m_cyclesMesh->need_attribute(scene, ccl::ATTR_STD_UV_TANGENT_SIGN)) {
                auto sign_attrib = attributes->add(ccl::ATTR_STD_UV_TANGENT_SIGN, sign_name);
                auto sign_data = sign_attrib->data_float();

                for (size_t i = 0; i < m_cyclesMesh->triangles.size(); ++i) {
                    sign_data[i] = 1.0f;
                }
            }

            continue;
        }

        // Forced true for now... Should be based on shader compilation needs
        need_tangent = true;
        if (need_tangent) {
            // Forced for now
            bool need_sign = true;
            mikk_compute_tangents(name.c_str(), m_cyclesMesh, need_sign, true);
        }
    }
}

/*
    Setting velocities for each vertex and letting Cycles take care
    of interpolating them once the shuttertime is known.

*/
void
HdCyclesMesh::_AddVelocities(const SdfPath& id, const VtValue& value, HdInterpolation interpolation)
{
    if (!m_motionBlur || m_motionDeformSteps <= 1) {
        return;
    }

    if (!value.IsHolding<VtVec3fArray>()) {
        TF_WARN("Unexpected type for velocities for: %s", id.GetText());
        return;
    }
    VtVec3fArray velocities = value.UncheckedGet<VtVec3fArray>();

    ccl::AttributeSet* attributes = &m_cyclesMesh->attributes;

    ccl::Attribute* attr_mP = attributes->find(ccl::ATTR_STD_MOTION_VERTEX_POSITION);
    if (attr_mP) {
        TF_WARN("Velocities will be ignored since motion positions exist for: %s", id.GetText());
        return;
    }

    ccl::Attribute* attr_V = attributes->find(ccl::ATTR_STD_VERTEX_VELOCITY);
    if (!attr_V) {
        attr_V = attributes->add(ccl::ATTR_STD_VERTEX_VELOCITY);
    }

    if (interpolation == HdInterpolationVertex) {
        assert(velocities.size() == m_cyclesMesh->verts.size());

        ccl::float3* V = attr_V->data_float3();
        for (size_t i = 0; i < velocities.size(); ++i) {
            V[i] = vec3f_to_float3(velocities[i]);
        }
    } else {
        TF_WARN("Velocity requries per-vertex interpolation for: %s", id.GetText());
    }
}

/*
    Adding accelerations to improve the quality of the motion blur geometry.
    They will be active only if the velocities are present
*/
void
HdCyclesMesh::_AddAccelerations(const SdfPath& id, const VtValue& value, HdInterpolation interpolation)
{
    if (!m_motionBlur || m_motionDeformSteps <= 1) {
        return;
    }

    if (!value.IsHolding<VtVec3fArray>()) {
        TF_WARN("Unexpected type for accelerations for: %s", id.GetText());
        return;
    }
    VtVec3fArray accelerations = value.UncheckedGet<VtVec3fArray>();

    ccl::AttributeSet* attributes = &m_cyclesMesh->attributes;

    ccl::Attribute* attr_mP = attributes->find(ccl::ATTR_STD_MOTION_VERTEX_POSITION);
    if (attr_mP) {
        TF_WARN("Accelerations will be ignored since motion positions exist");
        return;
    }

    ccl::Attribute* attr_accel = attributes->find(ccl::ATTR_STD_VERTEX_ACCELERATION);
    if (!attr_accel) {
        attr_accel = attributes->add(ccl::ATTR_STD_VERTEX_ACCELERATION);
    }

    if (interpolation == HdInterpolationVertex) {
        assert(accelerations.size() == m_cyclesMesh->verts.size());

        ccl::float3* A = attr_accel->data_float3();
        for (size_t i = 0; i < accelerations.size(); ++i) {
            A[i] = vec3f_to_float3(accelerations[i]);
        }
    } else {
        TF_WARN("Acceleration requires per-vertex interpolation");
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
    m_object_source->CreateAttributeSource<HdBbMeshAttributeSource>(name, role, data, m_cyclesMesh, interpolation,
                                                                    m_topology);
}

void
HdCyclesMesh::_PopulateNormals(HdSceneDelegate* sceneDelegate, const SdfPath& id)
{
    // Normals are tricky in Hd. When subdivisionSchema is being set to != none,
    // then Hd interface suppresses loading any normals as 'normals' or 'primvar:normals' from primvars.
    // It happens because Hd assumes that normals are going to come from the limit surface of Osd.
    // Also, dirty normals bit is never set. To evaluate this function, whenever points change,
    // we force normal bits to be dirty in the PropagateDirtyBits.

    // Normals can come from:
    // * authored normals passed by primvar, as long as subdivisionSchema == none
    // * auto generated from limit surface for subdivisionSchema != none

    // cleanup pre existing normals, that will force cycles to evaluate normals
    m_cyclesMesh->attributes.remove(ccl::ATTR_STD_FACE_NORMAL);
    m_cyclesMesh->attributes.remove(ccl::ATTR_STD_VERTEX_NORMAL);
    m_cyclesMesh->attributes.remove(ccl::ATTR_STD_CORNER_NORMAL);
    m_cyclesMesh->attributes.remove(ccl::ATTR_STD_MOTION_VERTEX_NORMAL);
    m_cyclesMesh->attributes.remove(ccl::ATTR_STD_MOTION_CORNER_NORMAL);

    //
    // Auto generated normals from limit surface
    //
    const HdCyclesMeshRefiner* refiner = m_topology->GetRefiner();
    if (refiner->IsSubdivided()) {
        assert(m_limit_us.size() == m_cyclesMesh->verts.size());
        assert(m_limit_vs.size() == m_cyclesMesh->verts.size());

        ccl::AttributeSet& attributes = m_cyclesMesh->attributes;
        ccl::Attribute* normal_attr = attributes.add(ccl::ATTR_STD_VERTEX_NORMAL);
        ccl::float3* normal_data = normal_attr->data_float3();

        for (size_t i = 0; i < m_limit_vs.size(); ++i) {
            normal_data[i] = ccl::normalize(ccl::cross(m_limit_us[i], m_limit_vs[i]));
        }

        return;
    }

    //
    // Authored normals from Primvar
    //
    auto GetPrimvarInterpolation = [sceneDelegate, &id](HdInterpolation& interpolation) -> bool {
        for (size_t i = 0; i < HdInterpolationCount; ++i) {
            HdPrimvarDescriptorVector d = sceneDelegate->GetPrimvarDescriptors(id, static_cast<HdInterpolation>(i));
            auto predicate = [](const HdPrimvarDescriptor& desc) -> bool {
                return desc.name == HdTokens->normals && desc.role == HdPrimvarRoleTokens->normal;
            };
            if (std::find_if(d.begin(), d.end(), predicate) != d.end()) {
                interpolation = static_cast<HdInterpolation>(i);
                return true;
            }
        }
        return false;
    };

    HdInterpolation interpolation = HdInterpolationCount;
    if (!GetPrimvarInterpolation(interpolation)) {
        m_cyclesMesh->add_face_normals();
        m_cyclesMesh->add_vertex_normals();
        TF_INFO(HDCYCLES_MESH).Msg("Generating smooth normals for: %s", id.GetText());
        return;
    }
    assert(interpolation >= 0 && interpolation < HdInterpolationCount);

    VtValue normals_value = GetNormals(sceneDelegate);
    if (normals_value.IsEmpty()) {
        TF_WARN("Empty normals for: %s", id.GetText());
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
        ccl::float3* normal_data = normal_attr->data_float3();

        const size_t num_triangles = refiner->GetTriangulatedTopology().GetNumFaces();
        memset(normal_data, 0, num_triangles * sizeof(ccl::float3));

        VtValue refined_value = refiner->RefineConstantData(HdTokens->normals, HdPrimvarRoleTokens->normal,
                                                            normals_value);
        if (refined_value.GetArraySize() != 1) {
            TF_WARN("Invalid uniform normals for: %s", id.GetText());
            return;
        }

        VtVec3fArray refined_normals = refined_value.Get<VtVec3fArray>();
        for (size_t i = 0; i < num_triangles; i++) {
            normal_data[i] = vec3f_to_float3(refined_normals[i]);
        }
    } else if (interpolation == HdInterpolationUniform) {
        // This is the correct way to handle face normals, but Cycles does not support
        // custom values, it will just use the geometric normal based on the 'smooth' flag.
        // To support custom normals we go through ATTR_STD_CORNER_NORMAL,
        // which results in 3x the amount of memory but respects the custom value.
        // This is more of an attempt to adhere to the USD specification, as it's
        // hard to imagine rendering a surface with a per-face normal different
        // from the geometric one.
#if 0
        ccl::Attribute* normal_attr = attributes.add(ccl::ATTR_STD_FACE_NORMAL);
        ccl::float3* normal_data    = normal_attr->data_float3();

        const size_t num_triangles = m_refiner->GetNumRefinedTriangles();
        memset(normal_data, 0, num_triangles * sizeof(ccl::float3));

        VtValue refined_value = m_refiner->RefineUniformData(HdTokens->normals, HdPrimvarRoleTokens->normal,
                                                            normals_value);
        if (refined_value.GetArraySize() != num_triangles) {
            TF_WARN("Invalid uniform normals for: %s", id.GetText());
            return;
        }

        VtVec3fArray refined_normals = refined_value.Get<VtVec3fArray>();
        for (size_t i = 0; i < num_triangles; i++) {
            normal_data[i] = vec3f_to_float3(refined_normals[i]);
        }
#else
        ccl::Attribute* normal_attr = attributes.add(ccl::ATTR_STD_CORNER_NORMAL);
        ccl::float3* normal_data = normal_attr->data_float3();

        const size_t num_triangles = refiner->GetTriangulatedTopology().GetNumFaces();
        memset(normal_data, 0, num_triangles * sizeof(ccl::float3));

        VtValue refined_value = refiner->RefineUniformData(HdTokens->normals, HdPrimvarRoleTokens->normal,
                                                           normals_value);
        if (refined_value.GetArraySize() != num_triangles) {
            TF_WARN("Invalid uniform normals for: %s", id.GetText());
            return;
        }

        VtVec3fArray refined_normals = refined_value.Get<VtVec3fArray>();
        ccl::float3 N;
        for (size_t i = 0; i < num_triangles; ++i) {
            N = vec3f_to_float3(refined_normals[i]);
            for (int j = 0; j < 3; ++j) {
                normal_data[i * 3 + j] = N;
            }
        }

        if (m_cyclesMesh->use_motion_blur && m_cyclesMesh->motion_steps > 1) {
            _PopulateMotionAttributeVec3f(sceneDelegate, id, HdTokens->normals, HdPrimvarRoleTokens->normal,
                                          HdInterpolationUniform, HdInterpolationFaceVarying,
                                          ccl::ATTR_STD_MOTION_CORNER_NORMAL, m_cyclesMesh->motion_steps);
        }
#endif
    } else if (interpolation == HdInterpolationVertex || interpolation == HdInterpolationVarying) {
        ccl::Attribute* normal_attr = attributes.add(ccl::ATTR_STD_VERTEX_NORMAL);
        ccl::float3* normal_data = normal_attr->data_float3();

        const size_t num_vertices = refiner->GetTriangulatedTopology().GetNumPoints();
        memset(normal_data, 0, num_vertices * sizeof(ccl::float3));

        VtValue refined_value;
        if (interpolation == HdInterpolationVertex) {
            refined_value = refiner->RefineVertexData(HdTokens->normals, HdPrimvarRoleTokens->normal, normals_value);
        } else {
            refined_value = refiner->RefineVaryingData(HdTokens->normals, HdPrimvarRoleTokens->normal, normals_value);
        }

        if (refined_value.GetArraySize() != num_vertices) {
            TF_WARN("Invalid vertex normals for: %s", id.GetText());
            return;
        }

        VtVec3fArray refined_normals = refined_value.Get<VtVec3fArray>();
        for (size_t i = 0; i < num_vertices; ++i) {
            normal_data[i] = vec3f_to_float3(refined_normals[i]);
        }

        if (m_cyclesMesh->use_motion_blur && m_cyclesMesh->motion_steps > 1) {
            _PopulateMotionAttributeVec3f(sceneDelegate, id, HdTokens->normals, HdPrimvarRoleTokens->normal,
                                          HdInterpolationVertex, HdInterpolationVertex,
                                          ccl::ATTR_STD_MOTION_VERTEX_NORMAL, m_cyclesMesh->motion_steps);
        }
    } else if (interpolation == HdInterpolationFaceVarying) {
        ccl::Attribute* normal_attr = attributes.add(ccl::ATTR_STD_CORNER_NORMAL);
        ccl::float3* normal_data = normal_attr->data_float3();

        const size_t num_triangles = refiner->GetTriangulatedTopology().GetNumFaces();
        memset(normal_data, 0, num_triangles * sizeof(ccl::float3));

        VtValue refined_value = refiner->RefineFaceVaryingData(HdTokens->normals, HdPrimvarRoleTokens->normal,
                                                               normals_value);
        if (refined_value.GetArraySize() != num_triangles * 3) {
            TF_WARN("Invalid facevarying normals for: %s", id.GetText());
            return;
        }

        VtVec3fArray refined_normals = refined_value.Get<VtVec3fArray>();
        for (size_t i = 0; i < num_triangles * 3; ++i) {
            normal_data[i] = vec3f_to_float3(refined_normals[i]);
        }

        if (m_cyclesMesh->use_motion_blur && m_cyclesMesh->motion_steps > 1) {
            _PopulateMotionAttributeVec3f(sceneDelegate, id, HdTokens->normals, HdPrimvarRoleTokens->normal,
                                          HdInterpolationFaceVarying, HdInterpolationFaceVarying,
                                          ccl::ATTR_STD_MOTION_CORNER_NORMAL, m_cyclesMesh->motion_steps);
        }
    } else {
        TF_WARN("Invalid normal interpolation for: %s", id.GetText());
    }
}

ccl::Mesh*
HdCyclesMesh::_CreateCyclesMesh()
{
    ccl::Mesh* mesh = new ccl::Mesh();
    mesh->clear();
    mesh->subdivision_type = ccl::Mesh::SUBDIVISION_NONE;
    return mesh;
}

ccl::Object*
HdCyclesMesh::_CreateCyclesObject()
{
    ccl::Object* object = new ccl::Object();

    object->tfm = ccl::transform_identity();
    object->pass_id = -1;

    object->visibility = ccl::PATH_RAY_ALL_VISIBILITY;

    return object;
}

void
HdCyclesMesh::_PopulateMotion(HdSceneDelegate* sceneDelegate, const SdfPath& id)
{
    // todo: this needs to be check to see if it is time-varying
    // todo: this should be shared with the points for the center motion step
    // TODO: implement resampling based on number of requested samples
    HdCyclesValueTimeSampleArray motion_samples;
    sceneDelegate->SamplePrimvar(id, HdTokens->points, &motion_samples);

    const size_t numSamples = motion_samples.count;
    auto& times = motion_samples.times;
    auto& values = motion_samples.values;

    ccl::AttributeSet* attributes = &m_cyclesMesh->attributes;
    ccl::Attribute* attr_mP = attributes->find(ccl::ATTR_STD_MOTION_VERTEX_POSITION);
    if (attr_mP) {
        attributes->remove(attr_mP);
    }

    if (numSamples <= 1) {
        m_cyclesMesh->use_motion_blur = false;
        m_cyclesMesh->motion_steps = 0;
        return;
    }

    m_cyclesMesh->use_motion_blur = true;
    m_cyclesMesh->motion_steps = static_cast<unsigned int>(numSamples + ((numSamples % 2) ? 0 : 1));

    const HdCyclesMeshRefiner* refiner = m_topology->GetRefiner();

    attr_mP = attributes->add(ccl::ATTR_STD_MOTION_VERTEX_POSITION);
    ccl::float3* mP = attr_mP->data_float3();

    for (unsigned int i = 0; i < numSamples; ++i) {
        if (times[i] == 0.0f)  // todo: more flexible check?
            continue;

        VtValue refined_points_value = refiner->RefineVertexData(HdTokens->points, HdPrimvarRoleTokens->point,
                                                                 values[i]);
        if (!refined_points_value.IsHolding<VtVec3fArray>()) {
            TF_WARN("Cannot fill in motion step %d for: %s\n", static_cast<int>(i), id.GetText());
            continue;
        }

        VtVec3fArray refined_points = refined_points_value.UncheckedGet<VtVec3fArray>();

        for (size_t j = 0; j < refiner->GetTriangulatedTopology().GetNumPoints(); ++j, ++mP) {
            *mP = vec3f_to_float3(refined_points[j]);
        }
    }
}

void
HdCyclesMesh::_PopulateMotionAttributeVec3f(HdSceneDelegate* sceneDelegate, const SdfPath& id, const TfToken& token,
                                            const TfToken& role, const HdInterpolation& interpolation_refine,
                                            const HdInterpolation& interpolation, int cycles_motion_attribute,
                                            size_t n_expected_samples)
{
    // todo: this needs to be check to see if it is time-varying
    // todo: this should be shared with the points for the center motion step
    // TODO: implement resampling based on number of requested samples
    HdCyclesValueTimeSampleArray motion_samples;
    sceneDelegate->SamplePrimvar(id, token, &motion_samples);

    const size_t numSamples = motion_samples.count;
    auto& times = motion_samples.times;
    auto& values = motion_samples.values;

    // This should eventually be replaced with resampling
    if (n_expected_samples != numSamples && n_expected_samples != numSamples - 1) {
        return;
    }

    ccl::AttributeSet* attributes = &m_cyclesMesh->attributes;
    ccl::Attribute* attr_m = attributes->find((ccl::AttributeStandard)cycles_motion_attribute);
    if (attr_m) {
        attributes->remove(attr_m);
    }

    if (numSamples <= 1) {
        return;
    }

    const HdCyclesMeshRefiner* refiner = m_topology->GetRefiner();

    attr_m = attributes->add((ccl::AttributeStandard)cycles_motion_attribute);
    ccl::float3* m = attr_m->data_float3();

    for (unsigned int i = 0; i < numSamples; ++i) {
        if (times[i] == 0.0f)  // todo: more flexible check?
            continue;

        VtValue refined_value = refiner->Refine(token, role, values[i], interpolation_refine);
        if (!refined_value.IsHolding<VtVec3fArray>()) {
            TF_WARN("Cannot fill in motion step %d for: %s\n", static_cast<int>(i), id.GetText());
            continue;
        }

        VtVec3fArray value = refined_value.UncheckedGet<VtVec3fArray>();

        if (interpolation == HdInterpolationVertex) {
            for (size_t j = 0; j < refiner->GetTriangulatedTopology().GetNumPoints(); ++j, ++m) {
                *m = vec3f_to_float3(value[j]);
            }
        } else if (interpolation == HdInterpolationFaceVarying) {
            const size_t numRefinedFaces = refiner->GetTriangulatedTopology().GetNumFaces();
            // Uniform -> FaceVarying
            if (value.size() == numRefinedFaces) {
                for (size_t j = 0; j < numRefinedFaces; ++j) {
                    for (size_t k = 0; k < 3; ++k, ++m) {
                        *m = vec3f_to_float3(value[j]);
                    }
                }
            } else {
                for (size_t j = 0; j < numRefinedFaces * 3; ++j, ++m) {
                    *m = vec3f_to_float3(value[j]);
                }
            }
        }
    }
}

void
HdCyclesMesh::_PopulateTopology(HdSceneDelegate* sceneDelegate, const SdfPath& id)
{
    HdMeshTopology topology = GetMeshTopology(sceneDelegate);
    topology.SetSubdivTags(GetSubdivTags(sceneDelegate));

    HdDisplayStyle display_style = sceneDelegate->GetDisplayStyle(id);
    display_style.refineLevel = m_refineLevel;

    // Refiner holds pointer to topology therefore refiner can't outlive the topology
    m_topology = std::make_shared<HdBbMeshTopology>(id, topology, display_style.refineLevel);
    const HdCyclesMeshRefiner* refiner = m_topology->GetRefiner();

    // Mesh is independently updated in two stages, faces(topology) and vertices(data).
    // Because process of updating vertices can fail for unknown reason,
    // we can end up with an empty vertex array. Indices must point to a valid vertex array(resize).
    m_cyclesMesh->clear();
    m_cyclesMesh->resize_mesh(refiner->GetTriangulatedTopology().GetNumPoints(),
                              refiner->GetTriangulatedTopology().GetNumFaces());

    const VtIntArray& refined_indices = refiner->GetTriangulatedTopology().GetFaceVertexIndices();
    for (size_t i = 0; i < refined_indices.size(); ++i) {
        m_cyclesMesh->triangles[i] = refined_indices[i];
    }

    for (size_t i = 0; i < m_cyclesMesh->verts.size(); ++i) {
        m_cyclesMesh->verts[i] = ccl::float3 { 0.f, 0.f, 0.f, 0.f };
    }

    for (size_t i {}; i < refiner->GetTriangulatedTopology().GetNumFaces(); ++i) {
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
    for (size_t i = 0; i < m_cyclesMesh->num_triangles(); ++i) {
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
    auto cycles_material = dynamic_cast<const HdCyclesMaterial*>(material);
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
    if (m_topology->GetGeomSubsets().empty()) {
        return;
    }

    HdRenderIndex& render_index = sceneDelegate->GetRenderIndex();

    // collect unrefined material ids for each face
    VtIntArray face_materials(m_topology->GetNumFaces(), 0);

    auto& used_shaders = m_cyclesMesh->used_shaders;
    TfHashMap<SdfPath, int, SdfPath::Hash> material_map;
    for (auto& subset : m_topology->GetGeomSubsets()) {
        int subset_material_id = 0;

        if (!subset.materialId.IsEmpty()) {
            const HdSprim* state_prim = render_index.GetSprim(HdPrimTypeTokens->material, subset.materialId);
            auto sub_mat = dynamic_cast<const HdCyclesMaterial*>(state_prim);

            if (!sub_mat)
                continue;
            if (!sub_mat->GetCyclesShader())
                continue;

            auto search_it = material_map.find(subset.materialId);
            if (search_it == material_map.end()) {
                used_shaders.push_back(sub_mat->GetCyclesShader());
                material_map[subset.materialId] = static_cast<int>(used_shaders.size());
                subset_material_id = static_cast<int>(used_shaders.size());
            } else {
                subset_material_id = search_it->second;
            }
        }

        for (int i : subset.indices) {
            face_materials[static_cast<size_t>(i)] = std::max(subset_material_id - 1, 0);
        }
    }

    // no subset materials discovered, no refinement required
    if (used_shaders.empty()) {
        return;
    }

    // refine material ids and assign them to refined geometry
    const HdCyclesMeshRefiner* refiner = m_topology->GetRefiner();
    VtValue refined_value = refiner->RefineUniformData(HdTokens->materialParams, HdPrimvarRoleTokens->none,
                                                       VtValue { face_materials });

    if (refined_value.GetArraySize() != m_cyclesMesh->shader.size()) {
        TF_WARN("Failed to assign refined materials for: %s", id.GetText());
        return;
    }

    auto refined_material_ids = refined_value.UncheckedGet<VtIntArray>();
    for (size_t i = 0; i < refined_material_ids.size(); ++i) {
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

    m_texture_names.clear();

    std::cout << "ID " << id << std::endl;
    for (auto& interpolation_description : primvars_desc) {
        for (const HdPrimvarDescriptor& description : interpolation_description.second) {
            std::cout << "Primvar " << description.name << std::endl;
            // collect texture coordinates names, it's needed to re-compute texture tangents.
            if (description.role == HdPrimvarRoleTokens->textureCoordinate) {
                m_texture_names.emplace_back(description.name.data(), description.name.size());
            }

            if (!HdChangeTracker::IsPrimvarDirty(*dirtyBits, id, description.name)) {
                continue;
            }

            // Those are special primvars
            if (description.name == HdTokens->points || description.name == HdTokens->normals) {
                continue;
            }

            auto interpolation = interpolation_description.first;

            if (description.name == HdTokens->displayColor || description.role == HdPrimvarRoleTokens->color) {
                auto value = GetPrimvar(sceneDelegate, description.name);
                _PopulateColors(description.name, description.role, value, scene, interpolation, id);
                continue;
            }

            if (description.role == HdPrimvarRoleTokens->textureCoordinate) {
                auto value = GetPrimvar(sceneDelegate, description.name);
                _AddUVSet(description.name, value, scene, interpolation);
                *dirtyBits |= DirtyBits::DirtyTangents;
                continue;
            }

            if (description.name == HdTokens->velocities) {
                auto value = GetPrimvar(sceneDelegate, description.name);
                _AddVelocities(id, value, interpolation);
                continue;
            }

            if (description.name == HdTokens->accelerations) {
                auto value = GetPrimvar(sceneDelegate, description.name);
                _AddAccelerations(id, value, interpolation);
                continue;
            }

            // do not commit primvars with cycles: prefix
            if (m_cyclesMesh && !TfStringStartsWith(description.name.GetString(), "cycles:")) {
                auto value = GetPrimvar(sceneDelegate, description.name);
                m_object_source->CreateAttributeSource<HdBbMeshAttributeSource>(description.name, description.role,
                                                                                value, m_cyclesMesh,
                                                                                description.interpolation, m_topology);
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
    bool points_computed = false;
    auto extComputationDescs = sceneDelegate->GetExtComputationPrimvarDescriptors(id, HdInterpolationVertex);
    for (auto& desc : extComputationDescs) {
        if (desc.name != HdTokens->points) {
            continue;
        }

        if (HdChangeTracker::IsPrimvarDirty(*dirtyBits, id, desc.name)) {
            auto valueStore = HdExtComputationUtils::GetComputedPrimvarValues({ desc }, sceneDelegate);
            auto pointValueIt = valueStore.find(desc.name);
            if (pointValueIt != valueStore.end()) {
                if (!pointValueIt->second.IsEmpty()) {
                    points_value = pointValueIt->second;
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

    const HdCyclesMeshRefiner* refiner = m_topology->GetRefiner();

    VtVec3fArray points;
    VtValue refined_points_value = refiner->RefineVertexData(HdTokens->points, HdPrimvarRoleTokens->point,
                                                             points_value);
    if (refined_points_value.IsHolding<VtVec3fArray>()) {
        points = refined_points_value.Get<VtVec3fArray>();
    } else {
        TF_WARN("Unsupported point type for: %s", id.GetText());
        return;
    }

    for (size_t i = 0; i < points.size(); ++i) {
        const GfVec3f& point = points[i];
        m_cyclesMesh->verts[i] = ccl::make_float3(point[0], point[1], point[2]);
    }

    //
    // Compute limit attributes once, then in the FinishMesh clean up the data
    //
    if (refiner->IsSubdivided()) {
        Vt_ArrayForeignDataSource foreign_data_source {};
        VtFloat3Array refined_vertices { &foreign_data_source, m_cyclesMesh->verts.data(), m_cyclesMesh->verts.size(),
                                         false };

        VtFloat3Array limit_ps(refined_vertices.size());
        m_limit_us.resize(refined_vertices.size());
        m_limit_vs.resize(refined_vertices.size());
        refiner->EvaluateLimit(refined_vertices, limit_ps, m_limit_us, m_limit_vs);

        // snap to limit surface
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
        ccl::Attribute* attr = attributes->add(ccl::ATTR_STD_GENERATED);

        ccl::float3* generated = attr->data_float3();
        for (size_t i = 0; i < m_cyclesMesh->verts.size(); i++) {
            generated[i] = m_cyclesMesh->verts[i] * size - loc;
        }
    }
}

void
HdCyclesMesh::_FinishMesh(ccl::Scene* scene)
{
    if (m_cyclesMesh->use_motion_blur && m_cyclesMesh->motion_steps > 1) {
        const bool hasCornerNormals = m_cyclesMesh->attributes.find(ccl::ATTR_STD_CORNER_NORMAL);
        const bool hasMotionCornerNormals = m_cyclesMesh->attributes.find(ccl::ATTR_STD_MOTION_CORNER_NORMAL);
        if (hasCornerNormals && !hasMotionCornerNormals) {
            TF_WARN("Mesh %s has corner normals, but no authored motion corner normals. Reverting to vertex normals.",
                    GetId().GetText());
            m_cyclesMesh->attributes.remove(ccl::ATTR_STD_CORNER_NORMAL);
        }
    }

    // This must be done first, because HdCyclesMeshTextureSpace requires computed min/max
    m_cyclesMesh->compute_bounds();

    _PopulateGenerated(scene);
}

void
HdCyclesMesh::_InitializeNewCyclesMesh()
{
    if (m_cyclesMesh) {
        m_renderDelegate->GetCyclesRenderParam()->RemoveGeometrySafe(m_cyclesMesh);
        delete m_cyclesMesh;
    }

    if (m_cyclesObject) {
        m_renderDelegate->GetCyclesRenderParam()->RemoveObjectSafe(m_cyclesObject);
        delete m_cyclesObject;
    }

    m_cyclesObject = _CreateCyclesObject();
    m_cyclesMesh = _CreateCyclesMesh();
    m_cyclesObject->geometry = m_cyclesMesh;

    m_renderDelegate->GetCyclesRenderParam()->AddGeometrySafe(m_cyclesMesh);
    m_renderDelegate->GetCyclesRenderParam()->AddObjectSafe(m_cyclesObject);

    // when time comes to switch object management to resource registry we need to switch off reference
    const SdfPath& id = GetId();
    auto resource_registry = dynamic_cast<HdCyclesResourceRegistry*>(m_renderDelegate->GetResourceRegistry().get());
    HdInstance<HdCyclesObjectSourceSharedPtr> object_instance = resource_registry->GetObjectInstance(id);
    object_instance.SetValue(std::make_shared<HdCyclesObjectSource>(m_cyclesObject, id, true));
    m_object_source = object_instance.GetValue();
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
    // Usd controls subdivision level globally, passed through the topology. Change of custom max subdiv level primvar
    // must mark SubdivTags dirty.
    if (HdChangeTracker::IsPrimvarDirty(bits, GetId(), usdCyclesTokens->primvarsCyclesMeshSubdivision_max_level)) {
        bits |= HdChangeTracker::DirtySubdivTags;
    }

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

    // Check PopulateNormals for more details
    if (bits & HdChangeTracker::DirtyPoints) {
        bits |= HdChangeTracker::DirtyNormals;
    }

    // dirty points trigger dirty tangents
    if (bits & HdChangeTracker::DirtyNormals) {
        bits |= DirtyBits::DirtyTangents;
    }

    return bits;
}

void
HdCyclesMesh::Sync(HdSceneDelegate* sceneDelegate, HdRenderParam* renderParam, HdDirtyBits* dirtyBits,
                   TfToken const& reprToken)
{
    auto param = dynamic_cast<HdCyclesRenderParam*>(renderParam);
    m_object_display_color_shader = param->default_object_display_color_surface;
    m_attrib_display_color_shader = param->default_attrib_display_color_surface;

    ccl::Scene* scene = param->GetCyclesScene();
    const SdfPath& id = GetId();

    ccl::thread_scoped_lock lock { scene->mutex };

    // -------------------------------------
    // -- Resolve Drawstyles

    m_refineLevel = 0;

    if (*dirtyBits & HdChangeTracker::DirtyVisibility) {
        _sharedData.visible = sceneDelegate->GetVisible(id);
        _UpdateObject(scene, param, dirtyBits, false);
        if (!_sharedData.visible) {
            return;
        }
    }

    if (*dirtyBits & HdChangeTracker::DirtyPrimvar) {
        HdPrimvarDescriptorMap primvarDescsPerInterpolation = GetPrimvarDescriptorMap(sceneDelegate);
        GetObjectPrimvars(primvarDescsPerInterpolation, sceneDelegate, dirtyBits);

        for (auto& primvarDescsEntry : primvarDescsPerInterpolation) {
            for (auto& pv : primvarDescsEntry.second) {
                if (!HdChangeTracker::IsPrimvarDirty(*dirtyBits, id, pv.name)) {
                    continue;
                }

                const std::string primvar_name = std::string { "primvars:" } + pv.name.GetString();

                if (primvar_name == usdCyclesTokens->primvarsCyclesMeshSubdivision_max_level) {
                    VtValue value = GetPrimvar(sceneDelegate, pv.name);
                    m_refineLevel = value.Get<int>();
                    continue;
                }
            }
        }
    }

    bool topologyIsDirty = false;
    if (HdChangeTracker::IsTopologyDirty(*dirtyBits, id)) {
        _PopulateTopology(sceneDelegate, id);
        topologyIsDirty = true;
    }

    if (*dirtyBits & HdChangeTracker::DirtyMaterialId) {
        _PopulateMaterials(sceneDelegate, param, scene->default_surface, id);
    }

    // For subdivided meshes, data conversion has to happen in a specific order:
    // 1) vertices - generate limit surface position and tangents
    // 2) normals - limit surface tangents are used to generate normals
    // 3) uvs - limit surface tangents
    // After conversion, class members that hold du and dv are *NOT* cleared in FinishMesh.
    // TODO: Revisit logic about keeping limit_us and limit_vs alive
    if (*dirtyBits & HdChangeTracker::DirtyPoints) {
        _PopulateVertices(sceneDelegate, id, dirtyBits);
    }

    if (m_motionBlur && m_motionDeformSteps > 0) {
        _PopulateMotion(sceneDelegate, id);
    } else {
        m_cyclesMesh->use_motion_blur = false;
        m_cyclesMesh->motion_steps = 0;
    }

    if (*dirtyBits & HdChangeTracker::DirtyNormals) {
        _PopulateNormals(sceneDelegate, id);
    }

    if (*dirtyBits & HdChangeTracker::DirtyPrimvar) {
        _PopulatePrimvars(sceneDelegate, scene, id, dirtyBits);
    }

    const ccl::Transform obj_tfm = mat4d_to_transform(sceneDelegate->GetTransform(id));
    if (*dirtyBits & HdChangeTracker::DirtyTransform) {
        auto fallback = sceneDelegate->GetTransform(id);
        HdCyclesMatrix4dTimeSampleArray xf {};

        std::shared_ptr<HdCyclesTransformSource> transform_source;
        if (m_motionBlur && m_motionTransformSteps > 1) {
            sceneDelegate->SampleTransform(id, &xf);
            transform_source = std::make_shared<HdCyclesTransformSource>(m_object_source->GetObject(), xf, fallback,
                                                                         m_motionTransformSteps);
        } else {
            transform_source = std::make_shared<HdCyclesTransformSource>(m_object_source->GetObject(), xf, fallback);
        }
        if (transform_source->IsValid()) {
            transform_source->Resolve();
        }

        m_object_source->AddObjectPropertiesSource(std::move(transform_source));
    }

    if (*dirtyBits & HdChangeTracker::DirtyPrimID) {
        // Offset of 1 added because Cycles primId pass needs to be shifted down to -1
        m_cyclesObject->pass_id = this->GetPrimId() + 1;
    }

    if (*dirtyBits & DirtyBits::DirtyTangents) {
        _PopulateTangents(sceneDelegate, id, scene);
    }

    // -------------------------------------
    // -- Handle point instances
    // -------------------------------------
    if (*dirtyBits & HdChangeTracker::DirtyInstancer) {
        const SdfPath& instancer_id = GetInstancerId();
        auto instancer = dynamic_cast<HdCyclesInstancer*>(sceneDelegate->GetRenderIndex().GetInstancer(instancer_id));
        if (instancer) {
            auto instanceTransforms = instancer->SampleInstanceTransforms(id);
            auto newNumInstances = (instanceTransforms.count > 0) ? instanceTransforms.values[0].size() : 0;

            bool reallocate_array = false;
            // Clear number of instances only if they have changed.
            if (m_cyclesInstances.size() != newNumInstances) {
                reallocate_array = true;
                if (m_cyclesInstances.size()) {
                    m_renderDelegate->GetCyclesRenderParam()->RemoveObjectArray(m_cyclesInstances);
                    std::vector<ccl::Object> empty = {};
                    m_cyclesInstances.swap(empty);
                }
            }

            // create new instances
            if (newNumInstances != 0) {
                printf("Creating instances %d transforms %d\n", newNumInstances, m_transformSamples.count);
                using size_type = typename decltype(m_transformSamples.values)::size_type;

                std::vector<TfSmallVector<GfMatrix4d, 1>> combinedTransforms;
                combinedTransforms.reserve(newNumInstances);
                for (size_t i = 0; i < newNumInstances; ++i) {
                    // Apply prototype transform (m_transformSamples) to all the instances
                    combinedTransforms.emplace_back(instanceTransforms.count);
                    auto& instanceTransform = combinedTransforms.back();

                    if (m_transformSamples.count == 0
                        || (m_transformSamples.count == 1 && (m_transformSamples.values[0] == GfMatrix4d(1)))) {
                        for (size_type j = 0; j < instanceTransforms.count; ++j) {
                            instanceTransform[j] = instanceTransforms.values[j][i];
                        }
                    } else {
                        for (size_type j = 0; j < instanceTransforms.count; ++j) {
                            GfMatrix4d xf_j = m_transformSamples.Resample(instanceTransforms.times[j]);
                            instanceTransform[j] = xf_j * instanceTransforms.values[j][i];
                        }
                    }
                }

                m_cyclesInstances.resize(newNumInstances);
                for (size_t j = 0; j < newNumInstances; ++j) {
                    ccl::Object* instanceObj = &m_cyclesInstances[j];

                    instanceObj->tfm = mat4d_to_transform(combinedTransforms[j].data()[0]) * obj_tfm;
                    instanceObj->geometry = m_cyclesMesh;
                    instanceObj->pass_id = -1;
                    instanceObj->visibility = m_visibilityFlags;

                    // TODO: Implement motion blur for point instanced objects
                    /*if (m_motionBlur) {
                        m_cyclesMesh->motion_steps    = m_motionSteps;
                        m_cyclesMesh->use_motion_blur = m_motionBlur;

                        instanceObj->motion.clear();
                        instanceObj->motion.resize(m_motionSteps);
                        for (int j = 0; j < m_motionSteps; j++) {
                            instanceObj->motion[j] = mat4d_to_transform(
                                combinedTransforms[j].data()[j]);
                        }
                    }*/
                }

                // Forward all primvars in the instancer
                auto instance_group = new ccl::InstanceGroup(m_cyclesMesh);

                std::cout << "Instancer ID " << instancer_id << std::endl;
                for (const auto& pv_desc : sceneDelegate->GetPrimvarDescriptors(instancer_id, HdInterpolationInstance)) {
                    VtValue value = sceneDelegate->Get(instancer_id, pv_desc.name);
                    if (pv_desc.name == "basecolor") {
                        std::cout << "Sourcing primvar " << value.GetTypeName() <<  std::endl;
                        auto colors = value.UncheckedGet<VtVec3fArray>();
                        std::cout << "Colors size " << colors.size() << std::endl;

#if 1
                        {
                            ccl::Attribute* attr_C = m_cyclesMesh->attributes.add(ccl::ustring("basecolor"), ccl::TypeDesc::TypeColor, ccl::ATTR_ELEMENT_MESH, newNumInstances);
                            std::cout << "Color pointer " << attr_C << std::endl;
                            ccl::float4* C = attr_C->data_float4();

                            for (size_t i = 0; i < newNumInstances; ++i) {
                                C[i].x = colors[i][0];
                                C[i].y = colors[i][1];
                                C[i].z = colors[i][2];
                                // printf("Color %f %f %f\n", C[i].x, C[i].y, C[i].z);
                            }
                        }
#endif

#if 1
                        {
                            ccl::Attribute* attr_C = instance_group->attributes.add(ccl::ustring("basecolor"), ccl::TypeDesc::TypeColor, ccl::ATTR_ELEMENT_MESH, newNumInstances);
                            std::cout << "Color pointer " << attr_C << std::endl;
                            ccl::float4* C = attr_C->data_float4();

                            for (size_t i = 0; i < newNumInstances; ++i) {
                                C[i].x = colors[i][0];
                                C[i].y = colors[i][1];
                                C[i].z = colors[i][2];
                                // printf("Color %f %f %f\n", C[i].x, C[i].y, C[i].z);
                            }
                        }
#endif
                    }
                }


                for (size_t i = 0; i < newNumInstances; ++i) {
                    m_cyclesInstances[i].particle_index = i;
                    m_cyclesInstances[i].instance_group = instance_group;
                }

                if (reallocate_array) {
                    m_renderDelegate->GetCyclesRenderParam()->AddInstanceGroup(instance_group);
                    m_renderDelegate->GetCyclesRenderParam()->AddObjectArray(m_cyclesInstances);
                }
            }

            // remove prototype from list of objects to render
            m_renderDelegate->GetCyclesRenderParam()->RemoveObject(m_cyclesObject);
        }
    }

    // update instances: steal visibility flags from the prototype
    if (*dirtyBits & HdChangeTracker::DirtyPrimvar) {
        // copy settings from the prototype
        const SdfPath& instancer_id = GetInstancerId();
        for (size_t i = 0; i < m_cyclesInstances.size(); ++i) {
            ccl::Object& instance = m_cyclesInstances[i];

            instance.visibility = m_visibilityFlags;
            instance.lightgroup = m_cyclesObject->lightgroup;
            instance.color = m_cyclesObject->color;

            std::string asset_name = instancer_id.GetString() + "/" + std::to_string(i);
            instance.asset_name = asset_name;
        }

        // basic primvars from the instances
        auto instancer = dynamic_cast<HdCyclesInstancer*>(sceneDelegate->GetRenderIndex().GetInstancer(instancer_id));
        if (instancer) {
            std::vector<HdPrimvarDescriptor> descriptors
                = sceneDelegate->GetPrimvarDescriptors(instancer_id, HdInterpolationInstance);
            for (auto& desc : descriptors) {
                if (desc.name == HdTokens->displayColor) {
                    VtValue displayColor = sceneDelegate->Get(instancer_id, HdTokens->displayColor);
                    if (displayColor.GetArraySize() != m_cyclesInstances.size()
                        || !displayColor.IsHolding<VtVec3fArray>()) {
                        continue;
                    }

                    const auto& colorValues = displayColor.Get<VtVec3fArray>();
                    for (size_t i = 0; i < m_cyclesInstances.size(); ++i) {
                        ccl::Object& instance = m_cyclesInstances[i];
                        const GfVec3f& value = colorValues[i];
                        instance.color[0] = value[0];
                        instance.color[1] = value[1];
                        instance.color[2] = value[2];
                    }
                    continue;
                }
            }
        }
    }

    _FinishMesh(scene);
    _UpdateObject(scene, param, dirtyBits, topologyIsDirty);
    *dirtyBits = HdChangeTracker::Clean;
}

void
HdCyclesMesh::_UpdateObject(ccl::Scene* scene, HdCyclesRenderParam* param, HdDirtyBits* dirtyBits, bool rebuildBvh)
{
    UpdateObject(scene, dirtyBits, rebuildBvh);
    param->Interrupt();
}

PXR_NAMESPACE_CLOSE_SCOPE
