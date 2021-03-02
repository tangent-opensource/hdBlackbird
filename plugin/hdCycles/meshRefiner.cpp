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

#include <opensubdiv/osd/tbbEvaluator.h>
#include <opensubdiv/osd/cpuVertexBuffer.h>
#include <opensubdiv/osd/cpuPatchTable.h>
#include <opensubdiv/far/stencilTableFactory.h>
#include <opensubdiv/far/patchTableFactory.h>
#include <opensubdiv/far/ptexIndices.h>
#include <opensubdiv/far/patchMap.h>

#include <numeric>

PXR_NAMESPACE_USING_DIRECTIVE;

using namespace OpenSubdiv;

#define EVALUATOR Osd::TbbEvaluator

namespace {

///
/// \brief Simple Triangle Refiner
///
class HdCyclesTriangleRefiner final : public HdCyclesMeshRefiner {
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

    const VtVec3iArray& GetRefinedVertexIndices() const override {
        return m_triangle_indices;
    }

    VtValue RefineConstantData(const TfToken& name, const TfToken& role,
                               const VtValue& data) const override {
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

    VtValue RefineVaryingData(const TfToken& name, const TfToken& role,
                              const VtValue& data) const override {
        if(data.GetArraySize() != m_topology->GetNumPoints()) {
            TF_WARN("Unsupported input data size for varying refinement for primvar %s at %s",
                    name.GetText(), m_id.GetPrimPath().GetString().c_str());
            return {};
        }

        return data;
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


/// Cpu buffer binder that satisfy CpuEvaluator requirements
template<typename T>
class RawCpuBuffer {
public:
    explicit RawCpuBuffer(T* cpuBuffer) noexcept
        : _data {cpuBuffer} {
    }

    // required interface for CpuEvaluator
    T* BindCpuBuffer() {
        return _data;
    }

private:
    T* _data;
};

///
/// \brief Refines uniform data
///
class SubdUniformRefiner {
public:
    explicit SubdUniformRefiner(const Far::TopologyRefiner& refiner, const Osd::CpuPatchTable* patch_table)
        : m_patch_table{patch_table}
        , m_ptex_indices{refiner} {
    }

    VtValue RefineArray(const VtValue& input, const VtIntArray& prim_param) const {

        switch(HdGetValueTupleType(input).type) {
        case HdTypeInt32: {
            return VtValue{RefineArray(input.UncheckedGet<VtArray<int>>(), prim_param)};
        }
        case HdTypeFloat: {
            return VtValue{RefineArray(input.UncheckedGet<VtArray<float>>(), prim_param)};
        }
        case HdTypeFloatVec2: {
            return VtValue{RefineArray(input.UncheckedGet<VtArray<GfVec2f>>(), prim_param)};
        }
        case HdTypeFloatVec3: {
            return VtValue{RefineArray(input.UncheckedGet<VtArray<GfVec3f>>(), prim_param)};
        }
        case HdTypeFloatVec4: {
            return VtValue{RefineArray(input.UncheckedGet<VtArray<GfVec4f>>(), prim_param)};
        }
        default:
            TF_CODING_ERROR("Unsupported uniform refinement");
            return {};
        }
    }

private:
    template<typename T>
    VtArray<T> RefineArray(const VtArray<T>& input, const VtIntArray& prim_param) const {
        VtArray<T> refined_data(prim_param.size());

        const Osd::PatchParam* patch_param_table = m_patch_table->GetPatchParamBuffer();
        for(size_t fine_id {}; fine_id < refined_data.size(); ++fine_id) {
            // triangulated patch id -> patch id
            const int patch_id = HdMeshUtil::DecodeFaceIndexFromCoarseFaceParam(prim_param[fine_id]);
            assert(patch_id < patch_param_table.size());

            // patch id -> coarse id
            auto& patch_param = patch_param_table[patch_id];
            const int coarse_id = m_ptex_indices.GetFaceId(patch_param.GetFaceId());
            assert(coarse_id < input.size());

            // lookup the data from coarse id
            refined_data[fine_id] = input[coarse_id];
        }

        return refined_data;
    }

    const Osd::CpuPatchTable* m_patch_table;
    Far::PtexIndices m_ptex_indices;
};

template<typename T>
VtArray<T>
RefineArrayWithStencils(const VtArray<T>& input, const Far::StencilTable* stencil_table, size_t stride) {
    VtArray<T> refined_array(stencil_table->GetNumStencils());
    Osd::BufferDescriptor src_descriptor(0, stride, stride);
    Osd::BufferDescriptor dst_descriptor(0, stride, stride);

    RawCpuBuffer<const float> src_buffer(reinterpret_cast<const float*>(input.data()));
    RawCpuBuffer<float> dst_buffer(reinterpret_cast<float*>(refined_array.data()));

    EVALUATOR::EvalStencils(&src_buffer, src_descriptor, &dst_buffer, dst_descriptor, stencil_table);
    return refined_array;
}

VtValue
RefineWithStencils(const VtValue& input, const Far::StencilTable* stencil_table) {
    HdTupleType value_tuple_type = HdGetValueTupleType(input);
    size_t stride = HdGetComponentCount(value_tuple_type.type);

    switch(value_tuple_type.type) {
    case HdTypeFloat: {
        return VtValue{RefineArrayWithStencils(input.UncheckedGet<VtArray<float>>(), stencil_table, stride)};
    }
    case HdTypeFloatVec2: {
        return VtValue{RefineArrayWithStencils(input.UncheckedGet<VtArray<GfVec2f>>(), stencil_table, stride)};
    }
    case HdTypeFloatVec3: {
        return VtValue{RefineArrayWithStencils(input.UncheckedGet<VtArray<GfVec3f>>(), stencil_table, stride)};
    }
    case HdTypeFloatVec4: {
        return VtValue{RefineArrayWithStencils(input.UncheckedGet<VtArray<GfVec4f>>(), stencil_table, stride)};
    }
    default:
        TF_CODING_ERROR("Unsupported osd refinement");
        return {};
    }
}

///
///
///
class SubdVertexRefiner {
public:
    SubdVertexRefiner(const Far::TopologyRefiner& refiner, Far::StencilTableFactory::Options options) {
        options.interpolationMode = Far::StencilTableFactory::INTERPOLATE_VERTEX;
        auto table = Far::StencilTableFactory::Create(refiner, options);
        m_stencils                = std::unique_ptr<const Far::StencilTable>(table);
    }

    VtValue RefineArray(const VtValue& input) const {
        return RefineWithStencils(input, m_stencils.get());
    }

    size_t Size() const {
        return m_stencils->GetNumStencils();
    }

private:
    std::unique_ptr<const Far::StencilTable> m_stencils;
};

///
///
///
class SubdVaryingRefiner {
public:
    SubdVaryingRefiner(const Far::TopologyRefiner& refiner, Far::StencilTableFactory::Options options) {
        options.interpolationMode = Far::StencilTableFactory::INTERPOLATE_VARYING;
        auto table = Far::StencilTableFactory::Create(refiner, options);
        m_stencils                = std::unique_ptr<const Far::StencilTable>{table};
    }

    VtValue RefineArray(const VtValue& input) const {
        return RefineWithStencils(input, m_stencils.get());
    }

private:
    std::unique_ptr<const Far::StencilTable> m_stencils;
};

///
///
///
class SubdFVarRefiner {
public:
    SubdFVarRefiner(const Far::TopologyRefiner& refiner,
                    const Osd::CpuPatchTable* patch_table,
                    Far::StencilTableFactory::Options options)
        : m_patch_table{patch_table}
    {
        options.interpolationMode = Far::StencilTableFactory::INTERPOLATE_FACE_VARYING;
        auto table = Far::StencilTableFactory::Create(refiner, options);
        m_stencils = std::unique_ptr<const Far::StencilTable>(table);
    }

    VtValue RefineArray(const VtValue& input) const {
        HdTupleType value_tuple_type = HdGetValueTupleType(input);
        size_t stride = HdGetComponentCount(value_tuple_type.type);

        switch(value_tuple_type.type) {
        case HdTypeFloat: {
            return VtValue{RefineArray(input.UncheckedGet<VtArray<float>>(), stride)};
        }
        case HdTypeFloatVec2: {
            return VtValue{RefineArray(input.UncheckedGet<VtArray<GfVec2f>>(), stride)};
        }
        case HdTypeFloatVec3: {
            return VtValue{RefineArray(input.UncheckedGet<VtArray<GfVec3f>>(), stride)};
        }
        case HdTypeFloatVec4: {
            return VtValue{RefineArray(input.UncheckedGet<VtArray<GfVec4f>>(), stride)};
        }
        default:
            TF_CODING_ERROR("Unsupported uniform refinement");
            return {};
        }
    }

private:
    template<typename T>
    VtArray<T> RefineArray(const VtArray<T>& input, size_t stride) const {
        //
        VtArray<T> refined_data(m_stencils->GetNumStencils());
        {
            Osd::BufferDescriptor src_descriptor(0, stride, stride);
            Osd::BufferDescriptor dst_descriptor(0, stride, stride);

            RawCpuBuffer<const float> src_buffer(reinterpret_cast<const float*>(input.data()));
            RawCpuBuffer<float> dst_buffer(reinterpret_cast<float*>(refined_data.data()));

            EVALUATOR::EvalStencils(&src_buffer, src_descriptor,
                                    &dst_buffer, dst_descriptor,
                                    m_stencils.get());
        }

        // TODO: Data evaluation should happen through EvalPatchesPrimVar
        VtArray<T> eval_data(m_patch_table->GetPatchIndexSize());
        {
            for (int fvert = 0; fvert < m_patch_table->GetFVarPatchIndexSize(); ++fvert) {
                int index = m_patch_table->GetFVarPatchIndexBuffer()[fvert];
                eval_data[fvert] = refined_data[index];
            }
        }

        return eval_data;
    }

    const Osd::CpuPatchTable* m_patch_table;
    std::unique_ptr<const Far::StencilTable> m_stencils;
};

///
/// \brief limit refiner computes surface normal
///
class SubdLimitRefiner {
public:
    SubdLimitRefiner(const Far::TopologyRefiner& refiner,
                     const Osd::CpuPatchTable* patch_table,
                     const Far::PatchMap& patch_map)
        : m_patch_table{ patch_table }
    {
        assert(patch_table->GetPatchParamSize() == last_level.GetNumFaces());

        auto& last_level = refiner.GetLevel(refiner.GetMaxLevel());
        m_patch_coords.reserve(last_level.GetNumVertices());

        const Osd::PatchParam* patch_param_table = patch_table->GetPatchParamBuffer();
        for(size_t vert{}; vert < last_level.GetNumVertices(); ++vert) {
            auto indices = last_level.GetVertexFaceLocalIndices(vert);
            auto faces = last_level.GetVertexFaces(vert);

            int local = indices[0];
            int face = faces[0];

            auto& patch_param = patch_param_table[face];

            constexpr std::array<float, 2> patch_corner_uvs[4] = {
                { 0.0f, 0.0f },
                { 1.0f, 0.0f },
                { 1.0f, 1.0f },
                { 0.0f, 1.0f },
            };

            float uu = patch_corner_uvs[local][0];
            float uv = patch_corner_uvs[local][1];
            patch_param.Unnormalize(uu, uv);

            m_patch_coords.emplace_back();
            m_patch_coords.back().handle = *patch_map.FindPatch(patch_param.GetFaceId(), uu, uv);
            m_patch_coords.back().s = uu;
            m_patch_coords.back().s = uv;
        }
    }

    VtVec3fArray RefineArray(const VtVec3fArray& vertices) const {
        VtVec3fArray ps_limit(m_patch_coords.size());
        VtVec3fArray du_limit(m_patch_coords.size());
        VtVec3fArray dv_limit(m_patch_coords.size());

        Osd::BufferDescriptor base_desc(0, 3, 3);
        RawCpuBuffer<const float> ps_base_buffer(vertices.data()->data());

        Osd::BufferDescriptor limit_desc {0, 3, 3};
        RawCpuBuffer<float> ps_limit_buffer{ps_limit.data()->data()};
        RawCpuBuffer<float> us_limit_buffer{du_limit.data()->data()};
        RawCpuBuffer<float> vs_limit_buffer{dv_limit.data()->data()};

        EvalPatches(base_desc, ps_base_buffer, // input
                    limit_desc, ps_limit_buffer, // position
                    limit_desc, us_limit_buffer, // us
                    limit_desc, vs_limit_buffer);

        VtVec3fArray normals(ps_limit.size());
        for(size_t i{}; i< ps_limit.size(); ++i) {
            normals[i] = GfCross(du_limit[i], dv_limit[i]);
        }

        // cleanup, we need to compute normals only once
        std::vector<Osd::PatchCoord> empty_patch_coords;
        m_patch_coords.swap(empty_patch_coords);

        return normals;
    }

    VtVec3fArray RefineArray(const VtVec4fArray& vertices) const {
        VtVec3fArray ps_limit(m_patch_coords.size());
        VtVec3fArray du_limit(m_patch_coords.size());
        VtVec3fArray dv_limit(m_patch_coords.size());

        Osd::BufferDescriptor base_desc(0, 3, 4);
        RawCpuBuffer<const float> ps_base_buffer(vertices.data()->data());

        Osd::BufferDescriptor limit_desc {0, 3, 3};
        RawCpuBuffer<float> ps_limit_buffer{ps_limit.data()->data()};
        RawCpuBuffer<float> us_limit_buffer{du_limit.data()->data()};
        RawCpuBuffer<float> vs_limit_buffer{dv_limit.data()->data()};

        EvalPatches(base_desc, ps_base_buffer, // input
                    limit_desc, ps_limit_buffer, // position
                    limit_desc, us_limit_buffer, // us
                    limit_desc, vs_limit_buffer);

        VtVec3fArray normals(ps_limit.size());
        for(size_t i{}; i< ps_limit.size(); ++i) {
            normals[i] = GfCross(du_limit[i], dv_limit[i]);
        }

        // cleanup, we need to compute normals only once
        std::vector<Osd::PatchCoord> empty_patch_coords;
        m_patch_coords.swap(empty_patch_coords);

        return normals;
    }

private:

    void EvalPatches(Osd::BufferDescriptor ps_base_desc, RawCpuBuffer<const float> ps_base_buffer,
                     Osd::BufferDescriptor ps_limit_desc, RawCpuBuffer<float> ps_limit_buffer,
                     Osd::BufferDescriptor us_limit_desc, RawCpuBuffer<float> us_limit_buffer,
                     Osd::BufferDescriptor vs_limit_desc, RawCpuBuffer<float> vs_limit_buffer) const {
        RawCpuBuffer<const Osd::PatchCoord> patch_coords_buffer{m_patch_coords.data()};
        EVALUATOR::EvalPatches(&ps_base_buffer, ps_base_desc,
                               &ps_limit_buffer, ps_limit_desc,
                               &us_limit_buffer, us_limit_desc,
                               &vs_limit_buffer, vs_limit_desc,
                               m_patch_coords.size(), &patch_coords_buffer,
                               m_patch_table);
    }

    mutable std::vector<Osd::PatchCoord> m_patch_coords;
    const Osd::CpuPatchTable* m_patch_table;
};

///
/// \brief Open Subdivision refiner implementation
///
class HdCyclesSubdRefiner final : public HdCyclesMeshRefiner {
public:
    HdCyclesSubdRefiner(const HdMeshTopology& topology, int refine_level, const SdfPath& id)
        : m_topology{&topology}
        , m_id{id}
    {
        HD_TRACE_FUNCTION();

        // passing topology through refiner converts cw to ccw
        {
            HD_TRACE_SCOPE("create refiner")

            std::vector<VtIntArray> fvar_topologies;

            // Hd does not offer custom topologies to be passed to the refiner.
            // Before data reaches to the Hd, every face varying data is un-indexed and flattened into one long array.
            // This makes custom fvar topology gone and each patch becomes independent, discontinuous piece of mesh.
            // Here we create custom topology with increasing indices. Depending on polygon orientation this topology
            // can be reversed by the PxOsdRefinerFactory, and converted to ccw(right hand) if necessary.
            // If Hd gets implementation to support custom face varying topologies, then we should pass each channel to
            // the refiner.
            VtIntArray fvar_indices(m_topology->GetFaceVertexIndices().size());
            std::iota(fvar_indices.begin(), fvar_indices.end(), 0);
            fvar_topologies.push_back(fvar_indices);

            m_refiner = PxOsdRefinerFactory::Create(topology.GetPxOsdMeshTopology(), fvar_topologies);

            Far::TopologyRefiner::UniformOptions uniform_options { refine_level };
            uniform_options.fullTopologyInLastLevel = true;
            m_refiner->RefineUniform(uniform_options);
        }

        // patches for face and materials lookup
        {
            HD_TRACE_SCOPE("create patch table")

            // by default Far will not generate patches for all levels, triangulate quads option works for uniform subdivision only
            Far::PatchTableFactory::Options patch_options(refine_level);
            patch_options.generateAllLevels = false;
            patch_options.useInfSharpPatch = true;

            // only if face varying is present
            patch_options.generateFVarTables = true;
            patch_options.numFVarChannels = m_refiner->GetNumFVarChannels();
            int channel = 0;
            patch_options.fvarChannelIndices = &channel;

            std::unique_ptr<Far::PatchTable> far_patch_table{Far::PatchTableFactory::Create(*m_refiner, patch_options)};
            m_patch_map = std::make_unique<Far::PatchMap>(*far_patch_table);
            m_patch_table = std::make_unique<Osd::CpuPatchTable>(far_patch_table.get());
        }

        // stencils required for primvar refinement
        {
            HD_TRACE_SCOPE("create stencil table")

            // Shared options for all stencils
            Far::StencilTableFactory::Options stencil_options;
            stencil_options.generateIntermediateLevels = false;
            stencil_options.generateOffsets            = true;

            // required stencils for vertex and normal computation
            m_vertex = std::make_unique<SubdVertexRefiner>(*m_refiner, stencil_options);
            m_uniform = std::make_unique<SubdUniformRefiner>(*m_refiner, m_patch_table.get());

            // optional refiners depending on presence of PrimVars
            m_limit = std::make_unique<SubdLimitRefiner>(*m_refiner);
            m_varying = std::make_unique<SubdVaryingRefiner>(*m_refiner, stencil_options);
            m_fvar = std::make_unique<SubdFVarRefiner>(*m_refiner, m_patch_table.get(), stencil_options);
        }

        // create Osd topology
        {
            HD_TRACE_SCOPE("create osd topology")

            const Far::TopologyLevel& last_level = m_refiner->GetLevel(m_refiner->GetMaxLevel());

            VtIntArray patch_vertex_count;
            patch_vertex_count.reserve(last_level.GetNumFaces());
            VtIntArray patch_vertex_indices;
            patch_vertex_indices.reserve(last_level.GetNumFaceVertices());

            for(Far::Index face{}; face < last_level.GetNumFaces(); ++face) {
                Far::ConstIndexArray face_vertices = last_level.GetFaceVertices(face);
                patch_vertex_count.push_back(face_vertices.size());
                std::copy(face_vertices.begin(), face_vertices.end(), std::back_inserter(patch_vertex_indices));
            }

            m_osd_topology = HdMeshTopology{PxOsdOpenSubdivTokens->none, PxOsdOpenSubdivTokens->rightHanded,
                                            patch_vertex_count, patch_vertex_indices };

            HdMeshUtil mesh_util{&m_osd_topology, m_id};
            mesh_util.ComputeTriangleIndices(&m_triangle_indices, &m_prim_param);
            m_triangle_counts = VtIntArray(m_triangle_indices.size(), 3);
        }
    }


    }

    }

    size_t GetNumRefinedVertices() const override {
        return m_vertex->Size();
    }

    const VtVec3iArray& GetRefinedVertexIndices() const override {
        return m_triangle_indices;
    }

    VtValue RefineConstantData(const TfToken& name, const TfToken& role,
                               const VtValue& data) const override {
        return {data};
    }

    VtValue RefineUniformData(const TfToken& name, const TfToken& role,
                              const VtValue& data) const override {
        if(data.GetArraySize() != m_topology->GetNumFaces()) {
            TF_WARN("Unsupported input data size for uniform refinement for primvar %s at %s",
                    name.GetText(), m_id.GetPrimPath().GetString().c_str());
            return {};
        }

        return m_uniform->RefineArray(data, m_prim_param);
    }

    VtValue RefineVertexData(const TfToken& name, const TfToken& role, const VtValue& data) const override {
        if(data.GetArraySize() != m_topology->GetNumPoints()) {
            TF_WARN("Unsupported input data size for vertex refinement for primvar %s at %s",
                    name.GetText(), m_id.GetPrimPath().GetString().c_str());
            return {};
        }

        return m_vertex->RefineArray(data);
    }

    VtValue RefineVaryingData(const TfToken& name, const TfToken& role, const VtValue& data) const override {
        if(data.GetArraySize() != m_topology->GetNumPoints()) {
            TF_WARN("Unsupported input data size for varying refinement for primvar %s at %s",
                    name.GetText(), m_id.GetPrimPath().GetString().c_str());
            return {};
        }

        return m_varying->RefineArray(data);
    }

    VtValue RefineFaceVaryingData(const TfToken& name, const TfToken& role, const VtValue& source) const override {
        if(source.GetArraySize() != m_topology->GetNumFaceVaryings()) {
            TF_WARN("Unsupported input source size for face varying refinement for primvar %s at %s",
                    name.GetText(), m_id.GetPrimPath().GetString().c_str());
            return {};
        }

        // No reverse is needed, since custom topology is reversed
        auto refined_value = m_fvar->RefineArray(source);

        // triangulate refinement for Cycles
        HdMeshUtil mesh_util{&m_osd_topology, m_id};
        VtValue triangulated;
        if(!mesh_util.ComputeTriangulatedFaceVaryingPrimvar(HdGetValueData(refined_value),
                                                            refined_value.GetArraySize(),
                                                            HdGetValueTupleType(refined_value).type,
                                                            &triangulated)) {
            TF_CODING_ERROR("Unsupported uniform refinement");
            return {};
        }

        return triangulated;
    }

private:
    const HdMeshTopology* m_topology;
    const SdfPath& m_id;

    HdMeshTopology m_osd_topology;

    VtVec3iArray m_triangle_indices;
    VtIntArray m_prim_param;

    // necessary osd structures
    PxOsdTopologyRefinerSharedPtr m_refiner;
    std::unique_ptr<const Far::PatchMap> m_patch_map;
    std::unique_ptr<const Osd::CpuPatchTable> m_patch_table;

    // Required
    std::unique_ptr<SubdVertexRefiner> m_vertex; // vertices
    std::unique_ptr<SubdUniformRefiner> m_uniform; // materials

    // Optional refiners depending on presence of PrimVars
    std::unique_ptr<SubdLimitRefiner> m_limit; // normals
    std::unique_ptr<SubdVaryingRefiner> m_varying;
    std::unique_ptr<SubdFVarRefiner> m_fvar;
};

} // namespace

std::shared_ptr<HdCyclesMeshRefiner>
HdCyclesMeshRefiner::Create(const HdMeshTopology& topology, int refine_level, const SdfPath& id) {
    if(topology.GetScheme() == PxOsdOpenSubdivTokens->none ||
        refine_level == 0) {
        return std::make_shared<HdCyclesTriangleRefiner>(topology, refine_level, id);
    }

    return std::make_shared<HdCyclesSubdRefiner>(topology, refine_level, id);
}

HdCyclesMeshRefiner::HdCyclesMeshRefiner() = default;

HdCyclesMeshRefiner::~HdCyclesMeshRefiner() = default;

size_t HdCyclesMeshRefiner::GetNumRefinedTriangles() const
{
    return GetRefinedVertexIndices().size();
}
