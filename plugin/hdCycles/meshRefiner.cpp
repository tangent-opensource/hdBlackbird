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

#include <pxr/imaging/hd/tokens.h>
#include <pxr/imaging/hd/changeTracker.h>
#include <pxr/imaging/hd/bufferSource.h>
#include <pxr/imaging/hd/meshUtil.h>

#include <pxr/imaging/pxOsd/refinerFactory.h>
#include <pxr/imaging/pxOsd/tokens.h>
#include <opensubdiv/osd/cpuEvaluator.h>
#include <opensubdiv/osd/cpuVertexBuffer.h>

#include <opensubdiv/far/stencilTableFactory.h>
#include <opensubdiv/far/patchTableFactory.h>

#include <render/mesh.h>

PXR_NAMESPACE_USING_DIRECTIVE;

using namespace OpenSubdiv;

namespace {

///
/// \brief
///
class HdCyclesSimpleMeshRefiner : public HdCyclesMeshRefiner {
public:
    HdCyclesSimpleMeshRefiner(const HdMeshTopology& topology, int refine_level, const SdfPath& id)
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

    bool RefineIndex(VtVec3iArray& triangle_indices) override {
        triangle_indices.assign(m_triangle_indices.begin(), m_triangle_indices.end());
    }

    bool RefineVertex(VtVec3fArray& input_array) override {
        // for triangulated mesh input_array stay the same, do nothing
        return true;
    }

    bool RefineUniform(VtIntArray& coarse_array) const override {
        // mapping fine_face_id -> coarse_face_id
        VtIntArray fine_array(GetNumTriangles());
        for(size_t fine_id {}; fine_id < fine_array.size(); ++fine_id) {
            int coarse_id = HdMeshUtil::DecodeFaceIndexFromCoarseFaceParam(m_primitive_param[fine_id]);
            assert(coarse_id < coarse_array.size());

            fine_array[fine_id] = coarse_array[coarse_id];
        }
        coarse_array = fine_array;
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
        size_t num_patches = m_patch_table->GetNumPatchesTotal();
        size_t num_vertices = m_patch_table->GetNumControlVerticesTotal();

        VtIntArray face_vertex_count(num_patches);
        VtIntArray face_vertex_indices(num_vertices);

        const Far::PatchTable::PatchVertsTable& patch_vertices =  m_patch_table->GetPatchControlVerticesTable();
        Far::PatchDescriptor patch_desc = m_patch_table->GetPatchArrayDescriptor(0);
        int patch_num_vertices = patch_desc.GetNumControlVertices();

        assert(patch_num_vertices * num_patches == patch_vertices.size());

        // memset, memcpy ?
        for(size_t patch{}; patch < num_patches; ++patch) {
            face_vertex_count[patch] = patch_num_vertices;
        }
        for(size_t vertex{}; vertex < num_vertices; ++vertex) {
            face_vertex_indices[vertex] = patch_vertices[vertex];
        }
    }

    size_t GetNumVertices() const override {
        return m_vertex_stencils->GetNumControlVertices() + m_vertex_stencils->GetNumStencils();
    }

    size_t GetNumTriangles() const override {
        return m_patch_table->GetNumPatchesTotal();
    }

    bool RefineIndex(VtVec3iArray& triangle_indices) override {
        const Far::PatchTable::PatchVertsTable& vertices_table = m_patch_table->GetPatchControlVerticesTable();

        size_t num_vertices = vertices_table.size();
        size_t num_triangles = num_vertices / 3;
        triangle_indices.resize(num_triangles);

        memcpy(triangle_indices.data(),& vertices_table[0], num_vertices * sizeof(int));
    }

    bool RefineVertex(VtVec3fArray& vertices) override {
        // no need to flip vertex data

        size_t num_elements = vertices.size();
        if (num_elements > m_vertex_stencils->GetNumControlVertices()) {
            num_elements = m_vertex_stencils->GetNumControlVertices();
        }

        // TODO: Write OSD Vec3Array buffer wrapper to avoid memory allocation
        size_t stride = 3;
        auto vertex_buffer = OpenSubdiv::Osd::CpuVertexBuffer::Create(stride, GetNumVertices());

        vertex_buffer->UpdateData(vertices.data()->data(), 0, num_elements);
        Osd::BufferDescriptor src_descriptor(0, stride, stride);
        Osd::BufferDescriptor dst_descriptor(num_elements * stride, stride, stride);

        Osd::CpuEvaluator::EvalStencils(vertex_buffer, src_descriptor,
                                        vertex_buffer, dst_descriptor,
                                        m_vertex_stencils.get());

        // copy back, memcpy?
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

        return true;
    }

    bool RefineUniform(VtIntArray& input_array) const override {

    }

private:
    using StencilTablePtr = std::unique_ptr<const OpenSubdiv::Far::StencilTable>;
    using PatchTablePtr = std::unique_ptr<const OpenSubdiv::Far::PatchTable>;

    StencilTablePtr m_vertex_stencils;
    StencilTablePtr m_varying_stencils;
    // TODO face varying stencil table?

    PatchTablePtr m_patch_table;
};

} // namespace

std::shared_ptr<HdCyclesMeshRefiner>
HdCyclesMeshRefiner::Create(const HdMeshTopology& topology, int refine_level, const SdfPath& id) {
    if(topology.GetScheme() == PxOsdOpenSubdivTokens->none ||
        refine_level == 0) {
        return std::make_shared<HdCyclesSimpleMeshRefiner>(topology, refine_level, id);
    }

    return std::make_shared<HdCyclesOsdMeshRefiner>(topology, refine_level);
}

HdCyclesMeshRefiner::HdCyclesMeshRefiner() = default;

HdCyclesMeshRefiner::~HdCyclesMeshRefiner() = default;



