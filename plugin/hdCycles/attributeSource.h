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


#ifndef HDCYCLES_ATTRIBUTESOURCE_H
#define HDCYCLES_ATTRIBUTESOURCE_H

#include <pxr/imaging/hd/bufferSource.h>
#include <pxr/imaging/hd/enums.h>

#include <render/attribute.h>

namespace ccl {
    class PointCloud;
}

PXR_NAMESPACE_OPEN_SCOPE

///
/// Cycles Attribute to be resolved
///
class HdCyclesAttributeSource : public HdBufferSource {
public:
    // immutable data accessors
    const TfToken& GetName() const override { return m_name; }
    const ccl::AttributeElement& GetAttributeElement() const { return m_element; }
    const ccl::TypeDesc& GetSourceTypeDesc() const { return m_type_desc; }
    const ccl::Attribute* GetAttribute() const { return m_attribute; }
    const ccl::Geometry* GetGeometry() const { return m_attributes->geometry; }

    // creates attribute for geometry
    bool Resolve() override;

    // accessors for underlying type
    HdTupleType GetTupleType() const override;
    void GetBufferSpecs(HdBufferSpecVector* specs) const override;
    const void* GetData() const override;
    size_t GetNumElements() const override;

    // Conversion from HdType and Hd Role to TypeDesc
    static ccl::TypeDesc GetTypeDesc(const HdType& type);
    static ccl::TypeDesc GetTypeDesc(const TfToken& role);
    static ccl::TypeDesc GetTypeDesc(const HdType& type, const TfToken& role);

    // Conversion from TypeDesc to Hd Role
    static const TfToken& GetRole(const ccl::TypeDesc& type_desc);

    // Conversion from TypeDesc to HdType
    static HdType GetType(const ccl::TypeDesc& type_desc);
    static HdTupleType GetTupleType(const ccl::TypeDesc& type_desc);

    // Conversion from any type to float with respecting HdTupleType
    static bool IsHoldingFloat(const VtValue& value);
    static bool CanCastToFloat(const VtValue& value);
    static VtValue UncheckedCastToFloat(const VtValue& value);

protected:
    TfToken m_name;   // attribute name
    VtValue m_value;  // source data to be committed

    ccl::AttributeSet* m_attributes;  // required for element size lookup
    ccl::AttributeElement m_element;  // element
    ccl::TypeDesc m_type_desc;        // type desc
    ccl::Attribute* m_attribute;      // attribute to be created

    // unfortunately AttributeSet has to be passed to support Geometry::attributes and Mesh::subd_attributes

    HdCyclesAttributeSource(TfToken name, const TfToken& role, const VtValue& value, ccl::AttributeSet* attributes,
                            ccl::AttributeElement element, const ccl::TypeDesc& type_desc);

    bool _CheckValid() const override;

    virtual bool ResolveAsValue();
    virtual bool ResolveAsArray();
};

///
/// Cycles Standard Attribute
///
class HdCyclesAttributeStandardSource : public HdCyclesAttributeSource {
public:
    HdCyclesAttributeStandardSource(const VtValue& value, ccl::AttributeSet* attribs, ccl::AttributeStandard std);
};


///
/// Cycles Hair
///
class HdCyclesHairAttributeSource : public HdCyclesAttributeSource {
public:
    HdCyclesHairAttributeSource(TfToken name, const TfToken& role, const VtValue& value, ccl::Hair* hair,
                                const HdInterpolation& interpolation);
};

///
/// Cycles Mesh
///
class HdCyclesMeshAttributeSource : public HdCyclesAttributeSource {
public:
    HdCyclesMeshAttributeSource(TfToken name, const TfToken& role, const VtValue& value, ccl::Mesh* mesh,
                                const HdInterpolation& interpolation);
};

///
/// Cycles PointCloud
///
class HdCyclesPointCloudAttributeSource : public HdCyclesAttributeSource {
public:
    HdCyclesPointCloudAttributeSource(const TfToken& name, const TfToken& role, const VtValue& value, ccl::PointCloud* pc,
                                const HdInterpolation& interpolation);
};


PXR_NAMESPACE_CLOSE_SCOPE

#endif  //HDCYCLES_ATTRIBUTESOURCE_H
