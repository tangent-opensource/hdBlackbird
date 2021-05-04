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

#ifndef HDCYCLES_OBJECTSOURCE_H
#define HDCYCLES_OBJECTSOURCE_H

#include <pxr/base/tf/hash.h>
#include <pxr/base/tf/hashmap.h>
#include <pxr/imaging/hd/bufferSource.h>
#include <pxr/usd/sdf/path.h>

namespace ccl {
class Object;
class Scene;
}  // namespace ccl

PXR_NAMESPACE_OPEN_SCOPE

class SdfPath;

///
///
///
class HdCyclesObjectSource : public HdBufferSource {
public:
    explicit HdCyclesObjectSource(ccl::Object* object, const SdfPath& id, bool isReference = true);
    ~HdCyclesObjectSource() override;

    void AddSource(HdBufferSourceSharedPtr source);

    const ccl::Object* GetObject() const { return m_object; }
    ccl::Object* GetObject() { return m_object; }

    // TODO: Resolve binds object to the scene
    bool Resolve() override;
    size_t ResolvePendingSources();

    const TfToken& GetName() const override;
    void GetBufferSpecs(HdBufferSpecVector* specs) const override {}
    const void* GetData() const override { return nullptr; }
    HdTupleType GetTupleType() const override { return HdTupleType {}; }
    size_t GetNumElements() const override { return 0; }

protected:
    bool _CheckValid() const override { return m_object != nullptr; }

    ccl::Object* m_object;
    SdfPath m_id;
    bool m_isReference;

    TfHashMap<TfToken, HdBufferSourceSharedPtr, TfHash> m_pending_sources;
};

using HdCyclesObjectSourceSharedPtr = std::shared_ptr<HdCyclesObjectSource>;

PXR_NAMESPACE_CLOSE_SCOPE

#endif  //HDCYCLES_OBJECTSOURCE_H
