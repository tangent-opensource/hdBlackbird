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

#include "meshSource.h"
#include "meshRefiner.h"

#include <render/mesh.h>
#include <render/instance_group.h>

PXR_NAMESPACE_USING_DIRECTIVE

namespace {

ccl::AttributeElement
interpolation_to_mesh_element(const HdInterpolation& interpolation)
{
    switch (interpolation) {
    case HdInterpolationConstant: return ccl::AttributeElement::ATTR_ELEMENT_OBJECT;
    case HdInterpolationUniform: return ccl::AttributeElement::ATTR_ELEMENT_FACE;
    case HdInterpolationVarying: return ccl::AttributeElement::ATTR_ELEMENT_VERTEX;
    case HdInterpolationVertex: return ccl::AttributeElement::ATTR_ELEMENT_VERTEX;
    case HdInterpolationFaceVarying: return ccl::AttributeElement::ATTR_ELEMENT_CORNER;
    case HdInterpolationInstance: return ccl::AttributeElement::ATTR_ELEMENT_OBJECT;
    default: return ccl::AttributeElement::ATTR_ELEMENT_NONE;
    }
}

}  // namespace

HdBbMeshAttributeSource::HdBbMeshAttributeSource(TfToken name, const TfToken& role, const VtValue& value,
                                                 ccl::Mesh* mesh, const HdInterpolation& interpolation,
                                                 std::shared_ptr<HdBbMeshTopology> topology)
    : HdBbAttributeSource(std::move(name), role, value, &mesh->attributes, interpolation_to_mesh_element(interpolation),
                          GetTypeDesc(HdGetValueTupleType(value).type, role))
    , m_interpolation { interpolation }
    , m_topology { std::move(topology) }
{
}


HdBbMeshAttributeSource::HdBbMeshAttributeSource(TfToken name, const TfToken& role, const VtValue& value,
                                                 ccl::InstanceGroup* instance_group, const HdInterpolation& interpolation,
                                                 std::shared_ptr<HdBbMeshTopology> topology)
    : HdBbAttributeSource(std::move(name), role, value, &instance_group->attributes, interpolation_to_mesh_element(interpolation),
                          GetTypeDesc(HdGetValueTupleType(value).type, role))
    , m_interpolation { interpolation }
    , m_topology { std::move(topology) }
{
}

bool
HdBbMeshAttributeSource::Resolve()
{
    if (!_TryLock()) {
        return false;
    }

    // refine attribute
    const ccl::TypeDesc& source_type_desc = GetSourceTypeDesc();
    const VtValue source_value = m_value;
    m_value = m_topology->GetRefiner()->Refine(GetName(), GetRole(source_type_desc), source_value,
                                               GetInterpolation());

    // late size check, since it is only known after refining
    if (!_CheckBuffersSize()) {
        _SetResolveError();
        return true;
    }

    bool resolved = HdBbAttributeSource::ResolveUnlocked();

    // marked as finished
    _SetResolved();
    return resolved;
}

bool
HdBbMeshAttributeSource::_CheckValid() const
{
    // size might be different because attribute could be refined

    if (!_CheckBuffersValid()) {
        return false;
    }

    // early exit on correct types
    if (_CheckBuffersType()) {
        return true;
    }

    TF_CODING_ERROR(
        "Attribute:%s is not going to be committed. Attribute has unknown type or can not be converted to known type!",
        m_name.data());
    return false;  // unsupported type
}
