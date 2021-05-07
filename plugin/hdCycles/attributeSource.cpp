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

#include "attributeSource.h"
#include "basisCurves.h"

#include <pxr/base/gf/vec2f.h>
#include <pxr/base/gf/vec2i.h>
#include <pxr/base/gf/vec3f.h>
#include <pxr/base/gf/vec3i.h>
#include <pxr/base/gf/vec4f.h>
#include <pxr/base/gf/vec4i.h>
#include <pxr/base/tf/registryManager.h>
#include <pxr/base/vt/array.h>

#include <render/hair.h>
#include <render/mesh.h>
#include <render/pointcloud.h>

PXR_NAMESPACE_USING_DIRECTIVE

ccl::TypeDesc
HdBbAttributeSource::GetTypeDesc(const HdType& type)
{
    // Mapping from known HdType -> TypeDesc supported by Cycles.
    // Allowed types come from ccl::Attribute constructor.

    switch (type) {
    // int converted to float
    case HdTypeInt32: return ccl::TypeFloat;
    case HdTypeInt32Vec2: return ccl::TypeFloat2;
    case HdTypeInt32Vec3: return ccl::TypeVector;
    case HdTypeInt32Vec4: return ccl::TypeRGBA;

    // uint32 converted to float
    case HdTypeUInt32: return ccl::TypeFloat;
    case HdTypeUInt32Vec2: return ccl::TypeFloat2;
    case HdTypeUInt32Vec3: return ccl::TypeVector;
    case HdTypeUInt32Vec4: return ccl::TypeRGBA;

    // float
    case HdTypeFloat: return ccl::TypeFloat;
    case HdTypeFloatVec2: return ccl::TypeFloat2;
    case HdTypeFloatVec3: return ccl::TypeVector;
    case HdTypeFloatVec4: return ccl::TypeRGBA;
    case HdTypeFloatMat4:
        return ccl::TypeUnknown;  // unsupported, cycles uses Matrix43

    // double converted to float
    case HdTypeDouble: return ccl::TypeFloat;
    case HdTypeDoubleVec2: return ccl::TypeFloat2;
    case HdTypeDoubleVec3: return ccl::TypeVector;
    case HdTypeDoubleVec4: return ccl::TypeRGBA;
    case HdTypeDoubleMat4:
        return ccl::TypeUnknown;  // unsupported, cycles uses Matrix43

    // half converted to float
    case HdTypeHalfFloat: return ccl::TypeFloat;
    case HdTypeHalfFloatVec2: return ccl::TypeFloat2;
    case HdTypeHalfFloatVec3: return ccl::TypeVector;
    case HdTypeHalfFloatVec4: return ccl::TypeRGBA;

    // default
    default: return ccl::TypeUnknown;
    }
}

ccl::TypeDesc
HdBbAttributeSource::GetTypeDesc(const TfToken& role)
{
    if (role == HdPrimvarRoleTokens->normal)
        return ccl::TypeNormal;
    if (role == HdPrimvarRoleTokens->point)
        return ccl::TypePoint;
    if (role == HdPrimvarRoleTokens->vector)
        return ccl::TypeVector;
    if (role == HdPrimvarRoleTokens->color)
        return ccl::TypeColor;
    if (role == HdPrimvarRoleTokens->textureCoordinate)
        return ccl::TypeFloat2;
    return ccl::TypeUnknown;
}

const TfToken&
HdBbAttributeSource::GetRole(const ccl::TypeDesc& type_desc)
{
    if (type_desc == ccl::TypeNormal)
        return HdPrimvarRoleTokens->normal;
    if (type_desc == ccl::TypePoint)
        return HdPrimvarRoleTokens->point;
    if (type_desc == ccl::TypeVector)
        return HdPrimvarRoleTokens->vector;
    if (type_desc == ccl::TypeColor)
        return HdPrimvarRoleTokens->color;
    return HdPrimvarRoleTokens->none;
}

HdType
HdBbAttributeSource::GetType(const ccl::TypeDesc& type_desc)
{
    // mapping from Cycles supported TypeDesc -> HdType
    // we don't need to cover all types

    // basic
    if (type_desc == ccl::TypeFloat)
        return HdTypeFloat;
    if (type_desc == ccl::TypeFloat2)
        return HdTypeFloatVec2;
    if (type_desc == ccl::TypeVector)
        return HdTypeFloatVec4;
    if (type_desc == ccl::TypeRGBA)
        return HdTypeFloatVec4;

    // unsupported, cycles uses Matrix43
    if (type_desc == ccl::TypeMatrix)
        return HdTypeInvalid;

    // role
    if (type_desc == ccl::TypeColor)
        return HdTypeFloatVec4;
    if (type_desc == ccl::TypePoint)
        return HdTypeFloatVec4;
    if (type_desc == ccl::TypeVector)
        return HdTypeFloatVec4;
    if (type_desc == ccl::TypeNormal)
        return HdTypeFloatVec4;

    return HdTypeInvalid;
}

ccl::TypeDesc
HdBbAttributeSource::GetTypeDesc(const HdType& type, const TfToken& role)
{
    // if role exists then role takes the precedence
    ccl::TypeDesc type_desc = ccl::TypeUnknown;
    if (role != HdPrimvarRoleTokens->none) {
        type_desc = GetTypeDesc(role);
    }

    // fallback to tuple type from VtValue
    if (type_desc == ccl::TypeUnknown) {
        type_desc = GetTypeDesc(type);
    }

    return type_desc;
}

bool
HdBbAttributeSource::IsHoldingFloat(const VtValue& value)
{
    HdTupleType value_tuple_type = HdGetValueTupleType(value);
    HdType component_type = HdGetComponentType(value_tuple_type.type);
    return component_type == HdTypeFloat;
}

bool
HdBbAttributeSource::CanCastToFloat(const VtValue& value)
{
    // unsupported Matrix3 and Matrix4
    return value.CanCast<float>() || value.CanCast<GfVec2f>() || value.CanCast<GfVec3f>() || value.CanCast<GfVec4f>()
           || value.CanCast<VtFloatArray>() || value.CanCast<VtVec2fArray>() || value.CanCast<VtVec3fArray>()
           || value.CanCast<VtVec4fArray>();
}

bool
HdBbAttributeSource::_CheckBuffersValid() const
{
    const VtValue& value = m_value;

    if (!m_attributes) {
        return false;
    }

    // check if source data is valid data
    if (!TF_VERIFY(!value.IsEmpty(), "ValueData for the source buffer is empty! Attribute:%s can not be committed!",
                   m_name.data())) {
        return false;
    }

    // check element type
    const ccl::AttributeElement& element = GetAttributeElement();
    if (!TF_VERIFY(element != ccl::AttributeElement::ATTR_ELEMENT_NONE,
                   "AttributeElement for the source value is NONE! Attribute:%s can not be committed!",
                   m_name.data())) {
        return false;
    }

    // source buffer type
    const ccl::TypeDesc& type_desc = GetSourceTypeDesc();
    if (!TF_VERIFY(type_desc != ccl::TypeUnknown,
                   "TypeDesc for the source buffer is Unknown! Attribute:%s can not be committed!", m_name.data())) {
        return false;
    }

    // destination buffer type
    const HdType type = GetType(type_desc);
    if (!TF_VERIFY(type != HdTypeInvalid,
                   "HdType for the destination buffer is Invalid! Attribute:%s can not be committed!", m_name.data())) {
        return false;
    }

    return true;
}

bool
HdBbAttributeSource::_CheckBuffersSize() const
{
    const VtValue& value = m_value;
    const ccl::AttributeElement& element = GetAttributeElement();

    // ELEMENT_OBJECT accepts only a value(array size == 0) or array(array size == 1)
    // For any other ELEMENT type array data is required
    auto get_source_size = [&element, &value]() -> size_t {
        if (element == ccl::ATTR_ELEMENT_OBJECT) {
            return value.IsArrayValued() ? value.GetArraySize() : 1;
        } else {
            return value.GetArraySize();
        }
    };

    const size_t source_size = get_source_size();
    const size_t element_size = GetGeometry()->element_size(element, m_attributes->prim);
    if (!TF_VERIFY(source_size == element_size,
                   "SourceSize:%lu is not the same as ElementSize:%lu ! Attribute:%s can not be committed!",
                   source_size, element_size, m_name.data())) {
        return false;
    }

    return true;
}

bool
HdBbAttributeSource::_CheckBuffersType() const
{
    const VtValue& value = m_value;

    // check if value holds expected array type
    if (IsHoldingFloat(value)) {
        return true;
    }

    // check if vt value can be converted
    if (CanCastToFloat(value)) {
        return true;
    }

    return false;
}

bool
HdBbAttributeSource::_CheckValid() const
{
    // Details about how to map between source and destination buffers must be known before Resolve.
    // Following checks ensure that no unknown or invalid buffers will be resolved.
    // Appropriate notification will be issued about incompatible buffers.

    if (!_CheckBuffersValid()) {
        return false;
    }

    if (!_CheckBuffersSize()) {
        return false;
    }

    // early exit on correct types
    if(_CheckBuffersType()) {
        return true;
    }

    TF_CODING_ERROR(
        "Attribute:%s is not going to be committed. Attribute has unknown type or can not be converted to known type!",
        m_name.data());
    return false;  // unsupported type
}

VtValue
HdBbAttributeSource::UncheckedCastToFloat(const VtValue& input_value)
{
    VtValue value = input_value;

    HdTupleType tuple_type = HdGetValueTupleType(value);
    size_t count = HdGetComponentCount(tuple_type.type);

    // Casting Matrix3 and Matrix4 is disabled.
    if (value.IsArrayValued()) {
        if (count == 1)
            value.Cast<VtArray<float>>();
        else if (count == 2)
            value.Cast<VtVec2fArray>();
        else if (count == 3)
            value.Cast<VtVec3fArray>();
        else if (count == 4)
            value.Cast<VtVec4fArray>();
    } else {
        if (count == 1)
            value.Cast<float>();
        else if (count == 2)
            value.Cast<GfVec2f>();
        else if (count == 3)
            value.Cast<GfVec3f>();
        else if (count == 4)
            value.Cast<GfVec4f>();
    }

    return value;
}

bool
HdBbAttributeSource::ResolveAsValue()
{
    // cast to float
    if (!IsHoldingFloat(m_value)) {
        m_value = UncheckedCastToFloat(m_value);
    }

    // create attribute
    const ccl::ustring name { m_name.data(), m_name.size() };
    const ccl::AttributeElement& attrib_element = GetAttributeElement();
    const ccl::TypeDesc& type_desc = GetSourceTypeDesc();
    m_attribute = m_attributes->add(name, type_desc, attrib_element);

    // copy the data, source's stride is always <= than cycles'
    size_t num_src_comp = HdGetComponentCount(HdGetValueTupleType(m_value).type);

    auto src_data = reinterpret_cast<const float*>(HdGetValueData(m_value));
    auto dst_data = reinterpret_cast<float*>(m_attribute->data());

    // if Cast fails we must recover
    if (!src_data || !dst_data) {
        return false;
    }

    // copy source to destination with respecting stride for both buffers
    for (size_t comp = 0; comp < num_src_comp; ++comp) {
        dst_data[comp] = src_data[comp];
    }

    return true;
}

bool
HdBbAttributeSource::ResolveAsArray()
{
    // cast to float
    if (!IsHoldingFloat(m_value)) {
        m_value = UncheckedCastToFloat(m_value);
    }

    // create attribute
    const ccl::ustring name { m_name.data(), m_name.size() };
    const ccl::AttributeElement& attrib_element = GetAttributeElement();
    const ccl::TypeDesc& type_desc = GetSourceTypeDesc();
    m_attribute = m_attributes->add(name, type_desc, attrib_element);

    // copy the data, source's stride is always <= than cycles'
    size_t num_src_comp = HdGetComponentCount(HdGetValueTupleType(m_value).type);
    size_t src_size = m_value.GetArraySize() * num_src_comp;
    auto src_data = reinterpret_cast<const float*>(HdGetValueData(m_value));
    assert(src_data != nullptr);

    size_t num_dst_comp = GetTupleType(type_desc).count;
    auto dst_data = reinterpret_cast<float*>(m_attribute->data());
    assert(dst_data != nullptr);

    assert(num_src_comp <= num_dst_comp);

    // if Cast fails we must recover
    if (!src_data || !dst_data) {
        return false;
    }

    // copy source to destination with respecting stride for both buffers
    for (size_t src_off = 0, dst_off = 0; src_off < src_size; src_off += num_src_comp, dst_off += num_dst_comp) {
        for (size_t comp = 0; comp < num_src_comp; ++comp) {
            dst_data[dst_off + comp] = src_data[src_off + comp];
        }
    }

    return true;
}

bool
HdBbAttributeSource::ResolveUnlocked()
{
    // resolving might fail, because of conversion
    if (m_value.GetArraySize()) {
        return ResolveAsArray();
    }

    return ResolveAsValue();
}

bool
HdBbAttributeSource::Resolve()
{
    if (!_TryLock()) {
        return false;
    }

    // resolving might fail, because of conversion
    bool resolved = ResolveUnlocked();

    // marked as finished
    _SetResolved();
    return resolved;
}

void
HdBbAttributeSource::GetBufferSpecs(HdBufferSpecVector* specs) const
{
    if (specs)
        specs->emplace_back(GetRole(m_type_desc), GetTupleType());
}

const void*
HdBbAttributeSource::GetData() const
{
    return m_attribute ? static_cast<const void*>(m_attribute->data()) : nullptr;
}

size_t
HdBbAttributeSource::GetNumElements() const
{
    if (GetGeometry()) {
        return GetGeometry()->element_size(GetAttributeElement(), m_attributes->prim);
    }

    return 0;
}

HdTupleType
HdBbAttributeSource::GetTupleType() const
{
    return GetTupleType(GetSourceTypeDesc());
}

HdTupleType
HdBbAttributeSource::GetTupleType(const ccl::TypeDesc& type_desc)
{
    HdType type = GetType(type_desc);
    return { type, HdGetComponentCount(type) };
}

namespace {

ccl::AttributeElement
interpolation_to_pointcloud_element(const HdInterpolation& interpolation)
{
    switch (interpolation) {
    case HdInterpolationConstant: return ccl::AttributeElement::ATTR_ELEMENT_OBJECT;
    case HdInterpolationUniform: return ccl::AttributeElement::ATTR_ELEMENT_VERTEX;
    case HdInterpolationVarying: return ccl::AttributeElement::ATTR_ELEMENT_VERTEX;
    case HdInterpolationVertex: return ccl::AttributeElement::ATTR_ELEMENT_VERTEX;
    case HdInterpolationFaceVarying: return ccl::AttributeElement::ATTR_ELEMENT_NONE;  // not supported
    case HdInterpolationInstance: return ccl::AttributeElement::ATTR_ELEMENT_NONE;     // not supported
    default: return ccl::AttributeElement::ATTR_ELEMENT_NONE;
    }
}

}  // namespace

HdBbAttributeSource::HdBbAttributeSource(TfToken name, const TfToken& role, const VtValue& value,
                                         ccl::AttributeSet* attributes, ccl::AttributeElement element,
                                         const ccl::TypeDesc& type_desc)
    : m_name { std::move(name) }
    , m_value { value }
    , m_attributes { attributes }
    , m_element { element }
    , m_type_desc { type_desc }
    , m_attribute { nullptr }
{
}

HdBbAttributeSource::HdBbAttributeSource(const VtValue& value, ccl::AttributeSet* attribs, ccl::AttributeStandard std)
    : HdBbAttributeSource(TfToken { ccl::Attribute::standard_name(std) },
                          GetRole(attribs->geometry->standard_type(std)), value, attribs,
                          attribs->geometry->standard_element(std), attribs->geometry->standard_type(std))
{
}

HdCyclesPointCloudAttributeSource::HdCyclesPointCloudAttributeSource(TfToken name, const TfToken& role, const VtValue& value,
                                                         ccl::PointCloud* pc, const HdInterpolation& interpolation)
    : HdBbAttributeSource(std::move(name), role, value, &pc->attributes, interpolation_to_pointcloud_element(interpolation), GetTypeDesc(HdGetValueTupleType(value).type, role))
{
}

/*
 * To Cycles attribute VtArray conversion
 */

namespace {

template<typename Dst, typename Src>
Dst
cast_vec_to_vec(const Src& src)
{
    static_assert(GfIsGfVec<Src>::value, "Src must be GfVec");
    static_assert(GfIsGfVec<Dst>::value, "Dst must be GfVec");

    constexpr size_t src_size = Src::dimension;
    constexpr size_t dst_size = Dst::dimension;
    static_assert(src_size == dst_size, "Only GfVec with same size can be converted");  // can be reduced

    constexpr size_t size = std::min(src_size, dst_size);
    Dst res {};
    for (size_t i = 0; i < size; ++i) {
        res[i] = static_cast<typename Dst::ScalarType>(src[i]);
    }
    return res;
}

template<typename Dst, typename Src>
VtValue
cast_arr_vec_to_arr_vec(const VtValue& input)
{
    auto& array = input.UncheckedGet<VtArray<Src>>();

    VtArray<Dst> output(array.size());
    std::transform(array.begin(), array.end(), output.begin(),
                   [](const Src& val) -> Dst { return cast_vec_to_vec<Dst>(val); });
    return VtValue { output };
}

template<typename Dst, typename Src>
VtValue
cast_arr_to_arr(const VtValue& input)
{
    auto& array = input.UncheckedGet<VtArray<Src>>();

    VtArray<Dst> output(array.size());
    std::transform(array.begin(), array.end(), output.begin(),
                   [](const Src& val) -> Dst { return static_cast<Dst>(val); });
    return VtValue { output };
}


template<typename Dst, typename Src>
bool
CanCast()
{
    return VtValue::CanCastFromTypeidToTypeid(typeid(Src), typeid(Dst));
}

template<typename Dst, typename Src, typename Fn>
void
TryRegisterCast(Fn fn)
{
    if (!CanCast<Dst, Src>()) {
        VtValue::RegisterCast<Src, Dst>(fn);
    }
}

template<typename Dst, typename Src>
void
TryRegisterValCast()
{
    TryRegisterCast<Dst, Src>(&cast_vec_to_vec<Dst, Src>);
}

template<typename Dst, typename Src>
void
TryRegisterValArrayCast()
{
    TryRegisterCast<VtArray<Dst>, VtArray<Src>>(&cast_arr_to_arr<float, Src>);
}

template<typename Dst, typename Src>
void
TryRegisterVecArrayCast()
{
    TryRegisterCast<VtArray<Dst>, VtArray<Src>>(&cast_arr_vec_to_arr_vec<Dst, Src>);
}

#if 0
template<typename Dst, typename Src>
void
TryRegisterMatArrayCast()
{
    TryRegisterCast<VtArray<Dst>, VtArray<Src>>(&cast_arr_to_arr<Dst, Src>);
}
#endif

}  // namespace

TF_REGISTRY_FUNCTION_WITH_TAG(VtValue, HdCyclesMesh)
{
    // no need to register converter from double to float/half, it's already present in the registry

    // to float array
    TryRegisterValArrayCast<float, bool>();
    TryRegisterValArrayCast<float, int>();
    TryRegisterValArrayCast<float, double>();

    // to float vec array
    TryRegisterVecArrayCast<GfVec2f, GfVec2i>();
    TryRegisterVecArrayCast<GfVec3f, GfVec3i>();
    TryRegisterVecArrayCast<GfVec4f, GfVec4i>();
}
