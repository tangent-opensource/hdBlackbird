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

#include <pxr/imaging/pxOsd/refinerFactory.h>
#include <pxr/imaging/pxOsd/tokens.h>

#include <render/mesh.h>

#include <opensubdiv/osd/cpuEvaluator.h>
#include <opensubdiv/osd/cpuVertexBuffer.h>
#include <opensubdiv/far/stencilTableFactory.h>
#include <opensubdiv/far/patchTableFactory.h>


PXR_NAMESPACE_USING_DIRECTIVE;

using namespace OpenSubdiv;

namespace {

template<typename T>
VtValue
triangle_uniform_refinement(const VtValue& data, const VtIntArray& primitive_param) {
    auto& input = data.UncheckedGet<VtArray<T>>();
    VtArray<T> fine_array(primitive_param.size());

    for(size_t fine_id {}; fine_id < fine_array.size(); ++fine_id) {
        int coarse_id = HdMeshUtil::DecodeFaceIndexFromCoarseFaceParam(primitive_param[fine_id]);
        assert(coarse_id < input.size());

        fine_array[fine_id] = input[coarse_id];
    }

    return VtValue{fine_array};
}

///
/// \brief
///
class HdCyclesTriangleRefiner : public HdCyclesMeshRefiner {
public:
    HdCyclesTriangleRefiner(const HdMeshTopology& topology, int refine_level, const SdfPath& id)
        : m_topology{&topology}
        , m_id{id} {
        HdMeshUtil mesh_util{&topology, m_id};
        mesh_util.ComputeTriangleIndices(&m_triangle_indices, &m_primitive_param);
    }

    size_t GetNumVertices() const override {
        return m_topology->GetNumPoints();
    }

    size_t GetNumTriangles() const override {
        return m_primitive_param.size();
    }

    const VtVec3iArray& RefinedIndices() const override {
        return m_triangle_indices;
    }

    VtValue RefineVertexData(const VtValue& data) const override {
        // vertex data has not changed, pass through
        return VtValue{data};
    }

    VtValue RefineVaryingData(const VtValue& data) const override {
        // vertex data has not changed, pass through
        return VtValue{data};
    }

    VtValue RefineUniformData(const VtValue& data) const override {
        if(data.GetArraySize() != m_topology->GetNumFaces()) {
            TF_CODING_ERROR("Unsupported input data size for uniform refinement");
            return {};
        }

        switch(HdGetValueTupleType(data).type) {
        case HdTypeInt32: {
            return triangle_uniform_refinement<int>(data, m_primitive_param);
        }
        case HdTypeFloat: {
            return triangle_uniform_refinement<float>(data, m_primitive_param);
        }
        default:
            TF_CODING_ERROR("Unsupported uniform refinement");
            return {};
        }
    }

    VtValue RefineFaceVaryingData(const VtValue& data)const override {
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
    VtIntArray m_primitive_param;
};

///
/// \brief
///
class HdCyclesOsdMeshRefiner final : public HdCyclesMeshRefiner {
public:
    HdCyclesOsdMeshRefiner(const HdMeshTopology& topology, int refine_level) {
        HD_TRACE_FUNCTION();

        // passing topology through refiner converts cw to ccw
        PxOsdTopologyRefinerSharedPtr refiner;
        {
            HD_TRACE_SCOPE("create refiner")

            refiner = PxOsdRefinerFactory::Create(topology.GetPxOsdMeshTopology());
            Far::TopologyRefiner::UniformOptions refiner_options { refine_level };
            refiner->RefineUniform(refiner_options);
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

            // TODO investigate segfault, it isn't okay to use stencils for face varying?
//            options.interpolationMode = Far::StencilTableFactory::INTERPOLATE_FACE_VARYING;
//            m_facevarying_stencils = StencilTablePtr{Far::StencilTableFactory::Create(*refiner, options)};
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
    }

    size_t GetNumVertices() const override {
        return m_vertex_stencils->GetNumControlVertices() + m_vertex_stencils->GetNumStencils();
    }

    size_t GetNumTriangles() const override {
        return m_patch_table->GetNumPatchesTotal();
    }

    const VtVec3iArray& RefinedIndices() const override {
        return m_triangle_indices;
    }

    VtValue RefineUniformData(const VtValue& data) const override {
        return {};
    }

    VtValue RefineVertexData(const VtValue& data) const override {
        // no need to flip vertex data

        size_t num_elements = data.GetArraySize();
        if (num_elements > m_vertex_stencils->GetNumControlVertices()) {
            num_elements = m_vertex_stencils->GetNumControlVertices();
        }

        // TODO: Write OSD Vec3Array buffer wrapper to avoid memory allocation
        size_t stride = 3;
        auto vertex_buffer = OpenSubdiv::Osd::CpuVertexBuffer::Create(stride, GetNumVertices());

        auto input = data.Get<VtVec3fArray>();
        vertex_buffer->UpdateData(input.data()->data(), 0, num_elements);
        Osd::BufferDescriptor src_descriptor(0, stride, stride);
        Osd::BufferDescriptor dst_descriptor(num_elements * stride, stride, stride);

        Osd::CpuEvaluator::EvalStencils(vertex_buffer, src_descriptor,
                                        vertex_buffer, dst_descriptor,
                                        m_vertex_stencils.get());

        // copy back, memcpy?
        VtVec3fArray vertices;
        vertices.resize(GetNumVertices());
        for(size_t i{}, offset{}; i < GetNumVertices(); ++i, offset += stride) {
            assert(offset + 0 < stride * GetNumVertices());
            assert(offset + 1 < stride * GetNumVertices());
            assert(offset + 2 < stride * GetNumVertices());

            vertices[i][0] = vertex_buffer->BindCpuBuffer()[offset + 0];
            vertices[i][1] = vertex_buffer->BindCpuBuffer()[offset + 1];
            vertices[i][2] = vertex_buffer->BindCpuBuffer()[offset + 2];
        }

        delete vertex_buffer;

        return VtValue{vertices};
    }

    VtValue RefineVaryingData(const VtValue& data) const override {
        return {};
    }

    VtValue RefineFaceVaryingData(const VtValue& data) const override {
        return {};
    }

private:
    using StencilTablePtr = std::unique_ptr<const OpenSubdiv::Far::StencilTable>;
    using PatchTablePtr = std::unique_ptr<const OpenSubdiv::Far::PatchTable>;

    VtVec3iArray m_triangle_indices;

    StencilTablePtr m_vertex_stencils;
    StencilTablePtr m_varying_stencils;
    StencilTablePtr m_facevarying_stencils;

    PatchTablePtr m_patch_table;
};

} // namespace

std::shared_ptr<HdCyclesMeshRefiner>
HdCyclesMeshRefiner::Create(const HdMeshTopology& topology, int refine_level, const SdfPath& id) {
    if(topology.GetScheme() == PxOsdOpenSubdivTokens->none ||
        refine_level == 0) {
        return std::make_shared<HdCyclesTriangleRefiner>(topology, refine_level, id);
    }

    return std::make_shared<HdCyclesOsdMeshRefiner>(topology, refine_level);
}

HdCyclesMeshRefiner::HdCyclesMeshRefiner() = default;

HdCyclesMeshRefiner::~HdCyclesMeshRefiner() = default;



