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

#include "objectSource.h"

#include <pxr/imaging/hd/tokens.h>

#include <graph/node.h>
#include <render/geometry.h>
#include <render/object.h>

PXR_NAMESPACE_USING_DIRECTIVE

HdCyclesObjectSource::HdCyclesObjectSource(ccl::Object* object, const SdfPath& id, bool isReference)
    : m_object { object }
    , m_id { id }
    , m_isReference { isReference }
{
    m_object->name = ccl::ustring { m_id.GetToken().GetText(), m_id.GetToken().size() };
}

HdCyclesObjectSource::~HdCyclesObjectSource()
{
    if (!m_isReference) {
        assert(m_object != nullptr);
        delete m_object->geometry;
        delete m_object;
    }
}

bool
HdCyclesObjectSource::Resolve()
{
    if (!_TryLock()) {
        return false;
    }

    // TODO bind ot the scene?

    // marked as finished
    _SetResolved();
    return true;
}

HdBbbObjectPropertiesSource*
HdCyclesObjectSource::AddObjectPropertiesSource(HdBbbObjectPropertiesSourceSharedPtr source)
{
    const TfToken& name = source->GetName();
    m_pending_properties[name] = std::move(source);
    return m_pending_properties[name].get();
}

HdBbAttributeSource*
HdCyclesObjectSource::AddAttributeSource(HdBbAttributeSourceSharedPtr source)
{
    const TfToken& name = source->GetName();
    m_pending_attributes[name] = std::move(source);
    return m_pending_attributes[name].get();
}

const TfToken&
HdCyclesObjectSource::GetName() const
{
    return m_id.GetToken();
}

size_t
HdCyclesObjectSource::ResolvePendingSources()
{
    size_t num_resolved_sources = 0;

    for (auto& source : m_pending_properties) {
        if (!source.second->IsValid()) {
            continue;
        }
        if(source.second->IsResolved()) {
            continue;
        }
        source.second->Resolve();
        ++num_resolved_sources;
    }

    // resolve pending sources
    for (auto& source : m_pending_attributes) {
        if (!source.second->IsValid()) {
            continue;
        }
        if(source.second->IsResolved()) {
            continue;
        }
        source.second->Resolve();
        ++num_resolved_sources;
    }

    // cleanup sources right after the resolve
    m_pending_properties.clear();
    m_pending_attributes.clear();

    return num_resolved_sources;
}
