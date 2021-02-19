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
#include <opensubdiv/osd/cpuPatchTable.h>
#include <opensubdiv/far/stencilTableFactory.h>
#include <opensubdiv/far/patchTableFactory.h>
#include <opensubdiv/far/ptexIndices.h>
#include <opensubdiv/far/patchMap.h>

#include <numeric>

PXR_NAMESPACE_USING_DIRECTIVE;

using namespace OpenSubdiv;

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

    const VtIntArray& GetRefinedCounts() const override {
        return m_triangle_counts;
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
class RawCpuBufferWrapper {
public:
    explicit RawCpuBufferWrapper(T* cpuBuffer) noexcept
        : _data {cpuBuffer} {
    }

    // required interface for CpuEvaluator
    T* BindCpuBuffer() {
        return _data;
    }

private:
    T* _data;
};

class SubdUniformRefiner {
public:
    explicit SubdUniformRefiner(const Far::TopologyRefiner& refiner)
        : m_ptex_indices(refiner)
    {
    }

    template<typename T>
    VtValue RefineArray(const VtValue& input,
                        const Far::PatchTable& patch_table,
                        const VtIntArray& prim_param) const {
        auto refined = RefineArray(input.UncheckedGet<VtArray<T>>(), patch_table, prim_param);
        return VtValue{refined};
    }

private:
    template<typename T>
    VtArray<T> RefineArray(const VtArray<T>& input,
                           const Far::PatchTable& patch_table,
                           const VtIntArray& prim_param) const {
        VtArray<T> refined_data(prim_param.size());

        const Far::PatchParamTable& patch_param_table = patch_table.GetPatchParamTable();
        for(size_t fine_id {}; fine_id < refined_data.size(); ++fine_id) {
            // triangulated patch id -> patch id
            const int patch_id = HdMeshUtil::DecodeFaceIndexFromCoarseFaceParam(prim_param[fine_id]);
            assert(patch_id < patch_param_table.size());

            // patch id -> coarse id
            const Far::PatchParam& patch_param = patch_param_table[patch_id];
            const int coarse_id = m_ptex_indices.GetFaceId(patch_param.GetFaceId());
            assert(coarse_id < input.size());

            // lookup the data from coarse id
            refined_data[fine_id] = input[coarse_id];
        }

        return refined_data;
    }


    Far::PtexIndices m_ptex_indices;
};


class HdCyclesSubdVertexRefiner {
public:
private:
};

class HdCyclesSubdVaryingRefiner {
public:
private:
};

class hdCyclesSubdFaceVaryingRefiner {

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
        PxOsdTopologyRefinerSharedPtr refiner;
        {
            HD_TRACE_SCOPE("create refiner")

            VtIntArray fvar_indices(m_topology->GetFaceVertexIndices().size());
            std::iota(fvar_indices.begin(), fvar_indices.end(), 0);
            std::vector<VtIntArray> fvar_topologies {fvar_indices};
            refiner = PxOsdRefinerFactory::Create(topology.GetPxOsdMeshTopology(), fvar_topologies);

            Far::TopologyRefiner::UniformOptions uniform_options { refine_level };
            uniform_options.fullTopologyInLastLevel = true;
            refiner->RefineUniform(uniform_options);

            m_ptex_indices = std::make_unique<Far::PtexIndices>(*refiner);
        }

        // patches for face and materials lookup
        {
            HD_TRACE_SCOPE("create patch table")

            // by default Far will not generate patches for all levels, triangulate quads option works for uniform subdivision only
            Far::PatchTableFactory::Options patch_options(refine_level);
            patch_options.generateAllLevels = false;
            patch_options.generateFVarTables = true;
            patch_options.numFVarChannels = refiner->GetNumFVarChannels();

            int channel = 0;
            patch_options.fvarChannelIndices = &channel;

            m_patch_table = PatchTablePtr{Far::PatchTableFactory::Create(*refiner, patch_options)};
            m_patch_map = std::make_unique<Far::PatchMap>(*m_patch_table);
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

        // create Osd topology
        {
            HD_TRACE_SCOPE("create osd topology")

            const Far::TopologyLevel& last_level = refiner->GetLevel(refiner->GetMaxLevel());

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

        m_uniform = std::make_unique<SubdUniformRefiner>(*refiner);
    }

    size_t GetNumRefinedVertices() const override {
        return m_vertex_stencils->GetNumStencils();
    }

    const VtVec3iArray& GetRefinedVertexIndices() const override {
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
        VtArray<T> fine_array(m_prim_param.size());

        const Far::PatchParamTable& patch_param_table = m_patch_table->GetPatchParamTable();
        for(size_t fine_id {}; fine_id < fine_array.size(); ++fine_id) {
            // triangulated patch id -> patch id
            const int patch_id = HdMeshUtil::DecodeFaceIndexFromCoarseFaceParam(m_prim_param[fine_id]);
            assert(patch_id < patch_param_table.size());

            // patch id -> coarse id
            const Far::PatchParam& patch_param = patch_param_table[patch_id];
            const int coarse_id = m_ptex_indices->GetFaceId(patch_param.GetFaceId());
            assert(coarse_id < input.size());

            // lookup the data from coarse id
            fine_array[fine_id] = input[coarse_id];
        }

        return VtValue{fine_array};
    }

    VtValue RefineUniformData(const TfToken& name, const TfToken& role,
                              const VtValue& data) const override {
        if(data.GetArraySize() != m_topology->GetNumFaces()) {
            TF_WARN("Unsupported input data size for uniform refinement for primvar %s at %s",
                    name.GetText(), m_id.GetPrimPath().GetString().c_str());
            return {};
        }

        switch(HdGetValueTupleType(data).type) {
        case HdTypeInt32: {
            return m_uniform->RefineArray<int>(data, *m_patch_table, m_prim_param);
//            return uniform_refinement<int>(name, role, data);
        }
        case HdTypeFloat: {
            return m_uniform->RefineArray<float>(data, *m_patch_table, m_prim_param);
            //return uniform_refinement<float>(name, role, data);
        }
        case HdTypeFloatVec2: {
            return m_uniform->RefineArray<GfVec2f>(data, *m_patch_table, m_prim_param);
//            return uniform_refinement<GfVec2f>(name, role, data);
        }
        case HdTypeFloatVec3: {
            return m_uniform->RefineArray<GfVec3f>(data, *m_patch_table, m_prim_param);
//            return uniform_refinement<GfVec3f>(name, role, data);
        }
        case HdTypeFloatVec4: {
            return m_uniform->RefineArray<GfVec4f>(data, *m_patch_table, m_prim_param);
//            return uniform_refinement<GfVec4f>(name, role, data);
        }
        default:
            TF_CODING_ERROR("Unsupported uniform refinement");
            return {};
        }
    }

    template<typename T>
    VtValue eval_stencils(const VtValue& data, const Far::StencilTable* stencil_table) const {
        HdTupleType value_tuple_type = HdGetValueTupleType(data);
        const size_t stride = HdGetComponentCount(value_tuple_type.type);

        VtArray<T> refined_array(stencil_table->GetNumStencils());
        Osd::BufferDescriptor src_descriptor(0, stride, stride);
        Osd::BufferDescriptor dst_descriptor(0, stride, stride);

        RawCpuBufferWrapper<const float> src_buffer(reinterpret_cast<const float*>(HdGetValueData(data)));
        RawCpuBufferWrapper<float> dst_buffer(reinterpret_cast<float*>(refined_array.data()));

        Osd::CpuEvaluator::EvalStencils(&src_buffer, src_descriptor,
                                        &dst_buffer, dst_descriptor,
                                        stencil_table);
        return VtValue{ refined_array };
    }

    VtValue refine(const VtValue& data, const Far::StencilTable* stencil_table) const {
        switch(HdGetValueTupleType(data).type) {
        case HdTypeFloat: {
            return eval_stencils<float>(data, stencil_table);
        }
        case HdTypeFloatVec2: {
            return eval_stencils<GfVec2f>(data, stencil_table);
        }
        case HdTypeFloatVec3: {
            return eval_stencils<GfVec3f>(data, stencil_table);
        }
        case HdTypeFloatVec4: {
            return eval_stencils<GfVec4f>(data, stencil_table);
        }
        default:
            TF_CODING_ERROR("Unsupported osd refinement");
            return {};
        }
    }

    VtValue RefineVaryingData(const TfToken& name, const TfToken& role, const VtValue& data) const override {
        if(data.GetArraySize() != m_topology->GetNumPoints()) {
            TF_WARN("Unsupported input data size for varying refinement for primvar %s at %s",
                    name.GetText(), m_id.GetPrimPath().GetString().c_str());
            return {};
        }

        return refine(data, m_varying_stencils.get());
    }

    VtValue RefineVertexData(const TfToken& name, const TfToken& role, const VtValue& data) const override {
        if(data.GetArraySize() != m_topology->GetNumPoints()) {
            TF_WARN("Unsupported input data size for vertex refinement for primvar %s at %s",
                    name.GetText(), m_id.GetPrimPath().GetString().c_str());
            return {};
        }

        return refine(data, m_vertex_stencils.get());
    }

    VtValue RefineFaceVaryingData(const TfToken& name, const TfToken& role, const VtValue& source) const override {
        if(source.GetArraySize() != m_topology->GetNumFaceVaryings()) {
            TF_WARN("Unsupported input source size for face varying refinement for primvar %s at %s",
                    name.GetText(), m_id.GetPrimPath().GetString().c_str());
            return {};
        }

        //
        VtArray<GfVec3f> eval_data(m_face_varying_stencils->GetNumStencils());
        {
            Osd::BufferDescriptor src_descriptor(0, 3, 3);
            Osd::BufferDescriptor dst_descriptor(0, 3, 3);

            RawCpuBufferWrapper<const float> src_buffer(reinterpret_cast<const float*>(HdGetValueData(source)));
            RawCpuBufferWrapper<float> dst_buffer(reinterpret_cast<float*>(eval_data.data()));

            Osd::CpuEvaluator::EvalStencils(&src_buffer, src_descriptor,
                                            &dst_buffer, dst_descriptor,
                                            m_face_varying_stencils.get());
        }

        // build patch coord for evaluating patch
        VtVec3fArray refined_data(m_patch_table->GetNumControlVerticesTotal(), {1,0,0});
        {
            VtArray<Osd::PatchCoord> patch_coords;
            for (int array=0; array < m_patch_table->GetNumPatchArrays(); ++array) {
                for (int patch=0; patch < m_patch_table->GetNumPatches(array); ++patch) {
                    Far::PatchParam patch_param = m_patch_table->GetPatchParam(array, patch);

                    float u,v;
                    const Far::PatchTable::PatchHandle* handle{};

                    u = 0.0f; v = 0.0f;
                    patch_param.Unnormalize(u, v);
                    handle = m_patch_map->FindPatch(patch_param.GetFaceId(), u, v);
                    patch_coords.push_back(Osd::PatchCoord{*handle, u, v});


                    u = 1.0f; v = 0.0f;
                    patch_param.Unnormalize(u, v);
                    handle = m_patch_map->FindPatch(patch_param.GetFaceId(), u, v);
                    patch_coords.push_back(Osd::PatchCoord{*handle, u, v});

                    u = 1.0f; v = 1.0f;
                    patch_param.Unnormalize(u, v);
                    handle = m_patch_map->FindPatch(patch_param.GetFaceId(), u, v);
                    patch_coords.push_back(Osd::PatchCoord{*handle, u, v});


                    u = 0.0f; v = 1.0f;
                    patch_param.Unnormalize(u, v);
                    handle = m_patch_map->FindPatch(patch_param.GetFaceId(), u, v);
                    patch_coords.push_back(Osd::PatchCoord{*handle, u, v});
                }
            }

            Osd::BufferDescriptor src_descriptor(0, 3, 3);
            Osd::BufferDescriptor dst_descriptor(0, 3, 3);

            RawCpuBufferWrapper<const float> src_buffer(reinterpret_cast<const float*>(eval_data.data()));
            RawCpuBufferWrapper<float> dst_buffer(reinterpret_cast<float*>(refined_data.data()));

            Osd::CpuPatchTable * evalPatchTable = Osd::CpuPatchTable::Create(m_patch_table.get());
            RawCpuBufferWrapper<Osd::PatchCoord> patch_coord_buffer{patch_coords.data()};

            Osd::CpuEvaluator::EvalPatchesFaceVarying(&src_buffer, src_descriptor,
                                                      &dst_buffer, dst_descriptor,
                                                      patch_coords.size(), &patch_coord_buffer,
                                                      evalPatchTable, 0);
        }

        // triangulate refinement
        VtValue refined_value { refined_data };
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
    VtIntArray m_triangle_counts; // TODO: Deprecated and has to be removed

    // Osd

    std::unique_ptr<SubdUniformRefiner> m_uniform;

    std::unique_ptr<const Far::PtexIndices> m_ptex_indices;

    // patch helpers
    using PatchTablePtr = std::unique_ptr<const Far::PatchTable>;
    PatchTablePtr m_patch_table;

    std::unique_ptr<const Far::PatchMap> m_patch_map;

    // stencils
    using StencilTablePtr = std::unique_ptr<const Far::StencilTable>;
    StencilTablePtr m_vertex_stencils;
    StencilTablePtr m_varying_stencils;
    StencilTablePtr m_face_varying_stencils;
    std::vector<Osd::PatchCoord> m_face_varying_patch_coords;
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
