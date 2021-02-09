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

#include "parser.h"

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wparentheses"
#include <pxr/base/tf/staticTokens.h>
#include <pxr/usd/ndr/node.h>
#include <pxr/usd/sdf/propertySpec.h>
#include <pxr/usd/sdr/shaderNode.h>
#include <pxr/usd/sdr/shaderProperty.h>
#include <pxr/usd/usd/attribute.h>
#include <pxr/usd/usd/property.h>
#pragma GCC diagnostic pop

PXR_NAMESPACE_OPEN_SCOPE

NDR_REGISTER_PARSER_PLUGIN(NdrCyclesParserPlugin);

// clang-format off
TF_DEFINE_PRIVATE_TOKENS(_tokens,
    (cycles)
    (binary)
);
// clang-format on

namespace {
// We have to subclass SdrShaderProperty, because it tries to read the SdfType
// from a token, and it doesn't support all the parameter types cycles does,
// like the 4 component color. Besides this, we also guarantee that the default
// value will match the SdfType, as the SdfType comes from the default value.
class CyclesShaderProperty : public SdrShaderProperty {
public:
    CyclesShaderProperty(const TfToken& name, const SdfValueTypeName& typeName,
                         const VtValue& defaultValue, bool isOutput,
                         size_t arraySize, const NdrTokenMap& metadata,
                         const NdrTokenMap& hints, const NdrOptionVec& options)
        : SdrShaderProperty(name, typeName.GetAsToken(), defaultValue, isOutput,
                            arraySize, metadata, hints, options)
        , _typeName(typeName)
    {
    }

    const SdfTypeIndicator GetTypeAsSdfType() const override
    {
        return { _typeName, _typeName.GetAsToken() };
    }

private:
    SdfValueTypeName _typeName;
};

}  // namespace

NdrCyclesParserPlugin::NdrCyclesParserPlugin() {}

NdrCyclesParserPlugin::~NdrCyclesParserPlugin() {}

NdrNodeUniquePtr
NdrCyclesParserPlugin::Parse(const NdrNodeDiscoveryResult& discoveryResult)
{
    NdrPropertyUniquePtrVec properties;
#if PXR_MINOR_VERSION >= 20 && PXR_PATCH_VERSION >= 5
    return NdrNodeUniquePtr(
        new SdrShaderNode(discoveryResult.identifier,     // identifier
                            discoveryResult.version,        // version
                            discoveryResult.name,           // name
                            discoveryResult.family,         // family
                            discoveryResult.discoveryType,  // context
                            discoveryResult.sourceType,     // sourceType
                            discoveryResult.uri,            // uri
                            discoveryResult.uri,            // resolvedUri
                            std::move(properties)));
#else
    return NdrNodeUniquePtr(
        new SdrShaderNode(discoveryResult.identifier,     // identifier
                          discoveryResult.version,        // version
                          discoveryResult.name,           // name
                          discoveryResult.family,         // family
                          discoveryResult.discoveryType,  // context
                          discoveryResult.sourceType,     // sourceType
                          discoveryResult.uri,            // uri
                          std::move(properties)));
#endif
}

const NdrTokenVec&
NdrCyclesParserPlugin::GetDiscoveryTypes() const
{
    static const NdrTokenVec ret = { _tokens->cycles };
    return ret;
}

const TfToken&
NdrCyclesParserPlugin::GetSourceType() const
{
    return _tokens->cycles;
}

PXR_NAMESPACE_CLOSE_SCOPE