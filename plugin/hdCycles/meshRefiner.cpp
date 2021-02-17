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

#include "meshRefiner.h"

#include <pxr/imaging/hd/changeTracker.h>
#include <pxr/imaging/hd/bufferSource.h>
#include <pxr/imaging/hd/meshUtil.h>
#include <pxr/base/gf/vec2f.h>
#include <pxr/base/gf/matrix3f.h>
#include <pxr/base/gf/matrix4f.h>

#include <pxr/imaging/pxOsd/refinerFactory.h>
#include <pxr/imaging/pxOsd/tokens.h>

#include <opensubdiv/osd/cpuEvaluator.h>
#include <opensubdiv/osd/cpuVertexBuffer.h>
#include <opensubdiv/far/stencilTableFactory.h>
#include <opensubdiv/far/patchTableFactory.h>
#include <opensubdiv/far/ptexIndices.h>

PXR_NAMESPACE_USING_DIRECTIVE;

using namespace OpenSubdiv;

namespace {

///
/// \brief Simple Triangle Refiner
///
class HdCyclesTriangleRefiner : public HdCyclesMeshRefiner {
public:
    HdCyclesTriangleRefiner(const HdMeshTopology& topology, int refine_level, const SdfPath& id)
        : m_topology{&topology}
        , m_id{id} {
        HdMeshUtil mesh_util{&topology, m_id};
        mesh_util.ComputeTriangleIndices(&m_triangle_indices, &m_primitive_param);
        m_triangle_counts = VtIntArray(m_primitive_param.size(), 3);
    }

    size_t GetNumRefinedVertices() const override {
        return m_topology->GetNumPoints();
    }

    const VtVec3iArray& GetRefinedIndices() const override {
        return m_triangle_indices;
    }

    const VtIntArray& GetRefinedCounts() const override {
        return m_triangle_counts;
    }

    VtValue RefineConstantData(const TfToken& name, const TfToken& role,
                               const VtValue& data) const override {
        return data;
    }

    VtValue RefineVaryingData(const TfToken& name, const TfToken& role,
                              const VtValue& data) const override {
        if(data.GetArraySize() != m_topology->GetNumPoints()) {
            TF_WARN("Unsupported input data size for varying refinement for primvar %s at %s",
                    name.GetText(), m_id.GetPrimPath().GetString().c_str());
            return {};
        }

        return data;
    }

    template<typename T>
    VtValue uniform_refinement(const TfToken& name, const TfToken& role, const VtValue& data) const {
        if(data.GetArraySize() != m_topology->GetNumFaces()) {
            TF_WARN("Unsupported input data size for uniform refinement for primvar %s at %s",
                    name.GetText(), m_id.GetPrimPath().GetString().c_str());
            return {};
        }

        auto& input = data.UncheckedGet<VtArray<T>>();
        VtArray<T> fine_array(m_primitive_param.size());

        for(size_t fine_id {}; fine_id < fine_array.size(); ++fine_id) {
            int coarse_id = HdMeshUtil::DecodeFaceIndexFromCoarseFaceParam(m_primitive_param[fine_id]);
            assert(coarse_id < input.size());

            fine_array[fine_id] = input[coarse_id];
        }

        return VtValue{fine_array};
    }

    VtValue RefineUniformData(const TfToken& name, const TfToken& role,
                              const VtValue& data) const override {
        switch(HdGetValueTupleType(data).type) {
        case HdTypeInt32: {
            return uniform_refinement<int>(name, role, data);
        }
        case HdTypeFloat: {
            return uniform_refinement<float>(name, role, data);
        }
        case HdTypeFloatVec2: {
            return uniform_refinement<GfVec2f>(name, role, data);
        }
        case HdTypeFloatVec3: {
            return uniform_refinement<GfVec3f>(name, role, data);
        }
        case HdTypeFloatVec4: {
            return uniform_refinement<GfVec4f>(name, role, data);
        }
        default:
            TF_CODING_ERROR("Unsupported uniform refinement");
            return {};
        }
    }

    VtValue RefineVertexData(const TfToken& name, const TfToken& role,
                             const VtValue& data) const override {
        if(data.GetArraySize() != m_topology->GetNumPoints()) {
            TF_WARN("Unsupported input data size for vertex refinement for primvar %s at %s",
                    name.GetText(), m_id.GetPrimPath().GetString().c_str());
            return {};
        }

        return data;
    }

    VtValue RefineFaceVaryingData(const TfToken& name, const TfToken& role,
                                  const VtValue& data) const override {
        if(data.GetArraySize() != m_topology->GetNumFaceVaryings()) {
            TF_WARN("Unsupported input data size for face varying refinement for primvar %s at %s",
                    name.GetText(), m_id.GetPrimPath().GetString().c_str());
            return {};
        }

        // only float and double can be interpolated
        HdMeshUtil mesh_util{m_topology, m_id};
        VtValue triangulated;
        if(!mesh_util.ComputeTriangulatedFaceVaryingPrimvar(HdGetValueData(data),
                                                            data.GetArraySize(),
                                                            HdGetValueTupleType(data).type,
                                                            &triangulated)) {
            TF_CODING_ERROR("Unsupported uniform refinement");
            return {};
        }

        return triangulated;
    }

private:
    const HdMeshTopology* m_topology;
    const SdfPath& m_id;
    VtVec3iArray m_triangle_indices;
    VtIntArray m_triangle_counts; // TODO: Deprecated and has to be removed
    VtIntArray m_primitive_param;
};

} // namespace


///
/// \brief Open Subdivision refiner implementation
///
class HdCyclesSubdivisionRefiner final : public HdCyclesMeshRefiner {
public:
    HdCyclesSubdivisionRefiner(const HdMeshTopology& topology, int refine_level, const SdfPath& id)
        : m_topology{&topology}
        , m_id{id}
    {
        HD_TRACE_FUNCTION();

        // passing topology through refiner converts cw to ccw
        PxOsdTopologyRefinerSharedPtr refiner;
        {
            HD_TRACE_SCOPE("create refiner")

            std::vector<VtIntArray> face_varying_topologies{m_topology->GetFaceVertexIndices()};
            refiner = PxOsdRefinerFactory::Create(topology.GetPxOsdMeshTopology(), face_varying_topologies);
            Far::TopologyRefiner::UniformOptions refiner_options { refine_level };
            refiner->RefineUniform(refiner_options);

            m_ptex_indices = std::make_unique<Far::PtexIndices>(*refiner);
        }

        // stencils required for primvar refinement
        {
            HD_TRACE_SCOPE("create stencil table")

            Far::StencilTableFactory::Options options;
            options.generateIntermediateLevels = false;
            options.generateOffsets            = true;

            options.interpolationMode          = Far::StencilTableFactory::INTERPOLATE_VERTEX;
            m_vertex_stencils = StencilTablePtr{Far::StencilTableFactory::Create(*refiner, options)};

            options.interpolationMode = Far::StencilTableFactory::INTERPOLATE_VARYING;
            m_varying_stencils = StencilTablePtr{Far::StencilTableFactory::Create(*refiner, options)};

            options.interpolationMode = Far::StencilTableFactory::INTERPOLATE_FACE_VARYING;
            m_face_varying_stencils = StencilTablePtr{Far::StencilTableFactory::Create(*refiner, options)};
        }

        // patches for face and materials lookup
        {
            HD_TRACE_SCOPE("create patch table")

            // by default Far will not generate patches for all levels, triangulate quads option works for uniform subdivision only
            Far::PatchTableFactory::Options options(refine_level);
            options.triangulateQuads = true; // !! Works only for uniform, if we switch to adaptive, we have to handle triangulation
            m_patch_table = PatchTablePtr{Far::PatchTableFactory::Create(*refiner, options)};
        }

        // populate topology
        const Far::PatchTable::PatchVertsTable& vertices_table = m_patch_table->GetPatchControlVerticesTable();

        size_t num_vertices = vertices_table.size();
        size_t num_triangles = num_vertices / 3;
        m_triangle_indices.resize(num_triangles);

        memcpy(m_triangle_indices.data(),& vertices_table[0], num_vertices * sizeof(int));
        m_triangle_counts = VtIntArray(num_triangles, 3);
    }

    size_t GetNumRefinedVertices() const override {
        return m_vertex_stencils->GetNumControlVertices() + m_vertex_stencils->GetNumStencils();
    }

    const VtVec3iArray& GetRefinedIndices() const override {
        return m_triangle_indices;
    }

    const VtIntArray& GetRefinedCounts() const override {
        return m_triangle_counts;
    }

    VtValue RefineConstantData(const TfToken& name, const TfToken& role,
                               const VtValue& data) const override {
        return {data};
    }

    template<typename T>
    VtValue uniform_refinement(const TfToken& name, const TfToken& role, const VtValue& data) const {
        if(data.GetArraySize() != m_topology->GetNumFaces()) {
            TF_WARN("Unsupported input data size for uniform refinement for primvar %s at %s",
                    name.GetText(), m_id.GetPrimPath().GetString().c_str());
            return {};
        }

        auto& input = data.UncheckedGet<VtArray<T>>();
        const Far::PatchParamTable& patch_param_table = m_patch_table->GetPatchParamTable();
        VtArray<T> fine_array(patch_param_table.size());

        for(size_t fine_id {}; fine_id < fine_array.size(); ++fine_id) {
            const Far::PatchParam& patch_param = patch_param_table[fine_id];
            const int coarse_id = m_ptex_indices->GetFaceId(patch_param.GetFaceId());
            assert(coarse_id < input.size());

            fine_array[fine_id] = input[coarse_id];
        }

        return VtValue{fine_array};
    }

    VtValue RefineUniformData(const TfToken& name, const TfToken& role,
                              const VtValue& data) const override {
        switch(HdGetValueTupleType(data).type) {
        case HdTypeInt32: {
            return uniform_refinement<int>(name, role, data);
        }
        case HdTypeFloat: {
            return uniform_refinement<float>(name, role, data);
        }
        case HdTypeFloatVec2: {
            return uniform_refinement<GfVec2f>(name, role, data);
        }
        case HdTypeFloatVec3: {
            return uniform_refinement<GfVec3f>(name, role, data);
        }
        case HdTypeFloatVec4: {
            return uniform_refinement<GfVec4f>(name, role, data);
        }
        default:
            TF_CODING_ERROR("Unsupported uniform refinement");
            return {};
        }
    }

    template<typename T>
    VtValue osd_refinement(const VtValue& data, const Far::StencilTable* stencil_table) const {
        size_t input_size = data.GetArraySize();
        if (input_size > stencil_table->GetNumControlVertices()) {
            input_size = stencil_table->GetNumControlVertices();
        }

        HdTupleType value_tuple_type = HdGetValueTupleType(data);
        const size_t stride = HdGetComponentCount(value_tuple_type.type);

        // TODO: this allocation can be avoided if we make custom(non-owning) buffer
        const size_t output_size = stencil_table->GetNumControlVertices() + stencil_table->GetNumStencils();
        auto vertex_buffer = Osd::CpuVertexBuffer::Create(stride, output_size);

        auto input = data.Get<VtArray<T>>();
        vertex_buffer->UpdateData(input.data()->data(), 0, input_size);

        Osd::BufferDescriptor src_descriptor(0, stride, stride);
        Osd::BufferDescriptor dst_descriptor(input_size * stride, stride, stride);

        Osd::CpuEvaluator::EvalStencils(vertex_buffer, src_descriptor,
                                        vertex_buffer, dst_descriptor,
                                        stencil_table);

        // copy back, memcpy?
        VtArray<T> refined_data;
        refined_data.resize(output_size);

        for(size_t i{}, offset{}; i < output_size; ++i, offset += stride) {
            for(size_t j{}; j < stride; ++j) {
                assert(offset + j < stride * output_size);

                refined_data[i][j] = vertex_buffer->BindCpuBuffer()[offset + j];
            }
        }

        delete vertex_buffer;

        return VtValue{refined_data};
    }

    VtValue osd_refine(const VtValue& data, const Far::StencilTable* stencil_table) const {
        switch(HdGetValueTupleType(data).type) {
        case HdTypeFloatVec2: {
            return osd_refinement<GfVec2f>(data, stencil_table);
        }
        case HdTypeFloatVec3: {
            return osd_refinement<GfVec3f>(data, stencil_table);
        }
        case HdTypeFloatVec4: {
            return osd_refinement<GfVec4f>(data, stencil_table);
        }
        default:
            TF_CODING_ERROR("Unsupported osd refinement");
            return {};
        }
    }

    VtValue RefineVaryingData(const TfToken& name, const TfToken& role,
                              const VtValue& data) const override {
        if(data.GetArraySize() != m_topology->GetNumPoints()) {
            TF_WARN("Unsupported input data size for varying refinement for primvar %s at %s",
                    name.GetText(), m_id.GetPrimPath().GetString().c_str());
            return {};
        }

        return osd_refine(data, m_varying_stencils.get());
    }

    VtValue RefineVertexData(const TfToken& name, const TfToken& role,
                             const VtValue& data) const override {
        if(data.GetArraySize() != m_topology->GetNumPoints()) {
            TF_WARN("Unsupported input data size for vertex refinement for primvar %s at %s",
                    name.GetText(), m_id.GetPrimPath().GetString().c_str());
            return {};
        }

        return osd_refine(data, m_vertex_stencils.get());
    }

    VtValue RefineFaceVaryingData(const TfToken& name, const TfToken& role,
                                  const VtValue& data) const override {
        return {};
    }

private:
    const HdMeshTopology* m_topology;
    const SdfPath& m_id;

    using StencilTablePtr = std::unique_ptr<const Far::StencilTable>;
    using PatchTablePtr = std::unique_ptr<const Far::PatchTable>;

    VtVec3iArray m_triangle_indices;
    VtIntArray m_triangle_counts; // TODO: Deprecated and has to be removed

    std::unique_ptr<const Far::PtexIndices> m_ptex_indices;
    StencilTablePtr m_vertex_stencils;
    StencilTablePtr m_varying_stencils;
    StencilTablePtr m_face_varying_stencils;

    PatchTablePtr m_patch_table;
};

std::shared_ptr<HdCyclesMeshRefiner>
HdCyclesMeshRefiner::Create(const HdMeshTopology& topology, int refine_level, const SdfPath& id) {
    if(topology.GetScheme() == PxOsdOpenSubdivTokens->none ||
        refine_level == 0) {
        return std::make_shared<HdCyclesTriangleRefiner>(topology, refine_level, id);
    }

    return std::make_shared<HdCyclesSubdivisionRefiner>(topology, refine_level, id);
}

HdCyclesMeshRefiner::HdCyclesMeshRefiner() = default;

HdCyclesMeshRefiner::~HdCyclesMeshRefiner() = default;

size_t HdCyclesMeshRefiner::GetNumRefinedTriangles() const
{
    return GetRefinedIndices().size();
}
