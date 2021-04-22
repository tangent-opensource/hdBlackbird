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
#include <render/object.h>

PXR_NAMESPACE_USING_DIRECTIVE

HdCyclesObjectSource::HdCyclesObjectSource(ccl::Object* object, const SdfPath& id)
    : m_object { new ccl::Object() }
    , m_id { id }
{
    m_object->name = ccl::ustring { m_id.GetToken().GetText(), m_id.GetToken().size() };
}

HdCyclesObjectSource::~HdCyclesObjectSource()
{
    // TODO unbind from scene?
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

void
HdCyclesObjectSource::AddSource(HdBufferSourceSharedPtr source)
{
    const TfToken& name     = source->GetName();
    m_pending_sources[name] = std::move(source);
}

const TfToken&
HdCyclesObjectSource::GetName() const
{
    return m_id.GetToken();
}

void
HdCyclesObjectSource::ResolvePendingSources()
{
    // resolve pending sources
    for (auto& source : m_pending_sources) {
        if (!source.second->IsValid()) {
            continue;
        }
        source.second->Resolve();
    }

    // cleanup sources right after the resolve
    m_pending_sources.clear();
}