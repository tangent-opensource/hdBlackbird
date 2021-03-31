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

#ifndef HDCYCLES_MESHTOPOLOGY_H
#define HDCYCLES_MESHTOPOLOGY_H

#include "meshRefiner.h"

#include <pxr/imaging/hd/meshTopology.h>

PXR_NAMESPACE_OPEN_SCOPE

using HdCyclesMeshTopologySharedPtr = std::shared_ptr<class HdCyclesMeshTopology>;

class HdCyclesMeshTopology : public HdMeshTopology {
public:
    HdCyclesMeshTopology() {}
    HdCyclesMeshTopology(const HdMeshTopology& src, int refineLevel, const SdfPath& id);
    static HdCyclesMeshTopologySharedPtr New(const HdMeshTopology& topology, int refineLevel, const SdfPath& id);

    const HdCyclesMeshRefiner* GetRefiner() const { return m_refiner.get(); }

private:
    std::shared_ptr<const HdCyclesMeshRefiner> m_refiner;
};

PXR_NAMESPACE_CLOSE_SCOPE

#endif  //HDCYCLES_MESHTOPOLOGY_H
