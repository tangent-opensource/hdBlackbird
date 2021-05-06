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

#ifndef HDCYCLES_MESHREFINER_H
#define HDCYCLES_MESHREFINER_H

#include <pxr/base/vt/array.h>
#include <pxr/imaging/hd/enums.h>
#include <pxr/imaging/hd/meshTopology.h>

#include <memory>

#include <util/util_types.h>

namespace ccl {
class Mesh;
}

PXR_NAMESPACE_OPEN_SCOPE

using VtFloat3Array = VtArray<ccl::float3>;

class HdDisplayStyle;
class HdMeshTopology;
class VtValue;
class TfToken;
class SdfPath;

///
/// \brief Refines mesh to triangles
///
/// Refiner's job is to prepare geometry for Cycles. That includes following requirements
///  * topology refinement - triangulation
//// * primvar refinement - data conversion to float and refinement
///
class HdCyclesMeshRefiner {
public:
    static std::shared_ptr<HdCyclesMeshRefiner> Create(const HdMeshTopology& topology, const SdfPath& id);

    virtual ~HdCyclesMeshRefiner();

    /// @{ TODO: Those methods belong to HdCyclesMeshTopology
    virtual size_t GetNumRefinedVertices() const = 0;
    virtual const VtVec3iArray& GetRefinedVertexIndices() const = 0;
    size_t GetNumRefinedTriangles() const;
    /// @}

    /// @{ \brief Refine/approximate primvar data.
    virtual VtValue RefineConstantData(const TfToken& name, const TfToken& role, const VtValue& data) const = 0;
    virtual VtValue RefineUniformData(const TfToken& name, const TfToken& role, const VtValue& data) const = 0;
    virtual VtValue RefineVaryingData(const TfToken& name, const TfToken& role, const VtValue& data) const = 0;
    virtual VtValue RefineVertexData(const TfToken& name, const TfToken& role, const VtValue& data) const = 0;
    virtual VtValue RefineFaceVaryingData(const TfToken& name, const TfToken& role, const VtValue& data) const = 0;
    /// @}

    virtual bool IsSubdivided() const = 0;

    virtual void EvaluateLimit(const VtFloat3Array& refined_vertices, VtFloat3Array& limit_ps, VtFloat3Array& limit_du,
                               VtFloat3Array& limit_dv) const = 0;

    HdCyclesMeshRefiner(const HdCyclesMeshRefiner&) = delete;
    HdCyclesMeshRefiner(HdCyclesMeshRefiner&&) noexcept = delete;

    HdCyclesMeshRefiner& operator=(const HdCyclesMeshRefiner&) = delete;
    HdCyclesMeshRefiner& operator=(HdCyclesMeshRefiner&&) noexcept = delete;

protected:
    HdCyclesMeshRefiner();
};

///
/// Hd Blackbird topology
///
class HdBbMeshTopology : public HdMeshTopology {
public:

    HdBbMeshTopology(const SdfPath& id, const HdMeshTopology& src, int refine_level);

    const HdCyclesMeshRefiner* GetRefiner() const { return m_refiner.get(); }

private:
    std::unique_ptr<HdCyclesMeshRefiner> m_refiner;
};

PXR_NAMESPACE_CLOSE_SCOPE

#endif  //HDCYCLES_MESHREFINER_H
