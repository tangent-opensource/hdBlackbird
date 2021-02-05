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

#include <pxr/imaging/hd/meshTopology.h>
#include <pxr/imaging/hd/enums.h>
#include <pxr/imaging/hd/bufferSource.h>

#include <memory>

PXR_NAMESPACE_OPEN_SCOPE

class HdMeshTopology;

///
///
///
class HdCyclesMeshRefiner {
public:
    static std::shared_ptr<HdCyclesMeshRefiner> Create(const HdMeshTopology& topology, int refine_level, const SdfPath& id);

    virtual ~HdCyclesMeshRefiner();

    /// @{ Refined topology information
    virtual size_t GetNumVertices() const = 0;
    virtual size_t GetNumTriangles() const = 0;
    /// @}

    /// \brief Refines arbitrary topology to triangles
    virtual const VtVec3iArray& RefinedIndices() const = 0;

    /// \brief Refined counts for backward compatibility
    virtual const VtIntArray& RefinedCounts() const = 0;

    /// @{ \brief Refine primvar data
    virtual VtValue RefineUniformData(const VtValue& data) const = 0;
    virtual VtValue RefineVertexData(const VtValue& data) const = 0;
    virtual VtValue RefineVaryingData(const VtValue& data) const = 0;
    virtual VtValue RefineFaceVaryingData(const VtValue& data) const = 0;
    /// @}

    /// \brief Refine primvar with given interpolation
    VtValue RefineData(const VtValue& data, const HdInterpolation& interpolation) const;

    HdCyclesMeshRefiner(const HdCyclesMeshRefiner&) = delete;
    HdCyclesMeshRefiner(HdCyclesMeshRefiner&&) noexcept = delete;

    HdCyclesMeshRefiner& operator=(const HdCyclesMeshRefiner&) = delete;
    HdCyclesMeshRefiner& operator=(HdCyclesMeshRefiner&&) noexcept = delete;

protected:
    HdCyclesMeshRefiner();
};

PXR_NAMESPACE_CLOSE_SCOPE

#endif  //HDCYCLES_MESHREFINER_H
