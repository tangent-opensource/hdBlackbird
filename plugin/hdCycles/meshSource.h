//  Copyright 2021 Tangent Animation
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

#ifndef HDBB_MESHSOURCE_H
#define HDBB_MESHSOURCE_H

#include "attributeSource.h"

namespace ccl {
    class InstanceGroup;
} // namespace ccl

PXR_NAMESPACE_OPEN_SCOPE

class HdBbMeshTopology;

///
/// Blackbird Mesh attribute source
///
class HdBbMeshAttributeSource : public HdBbAttributeSource {
public:
    HdBbMeshAttributeSource(TfToken name, const TfToken& role, const VtValue& value, ccl::Mesh* mesh,
                            const HdInterpolation& interpolation, std::shared_ptr<HdBbMeshTopology> topology);

    HdBbMeshAttributeSource(TfToken name, const TfToken& role, const VtValue& value, ccl::InstanceGroup* instance_group,
                            const HdInterpolation& interpolation, std::shared_ptr<HdBbMeshTopology> topology);


    // Underlying VtValue has different size than ccl::Geometry, we have to accommodate for that.
    bool Resolve() override;
    const HdInterpolation& GetInterpolation() const { return m_interpolation; }

private:
    bool _CheckValid() const override;

    HdInterpolation m_interpolation;
    std::shared_ptr<HdBbMeshTopology> m_topology;
};

PXR_NAMESPACE_CLOSE_SCOPE

#endif  //HDCYCLES_MESHSOURCE_H
