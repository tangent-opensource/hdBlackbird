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
//  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
//  See the License for the specific language governing permissions and
//  limitations under the License.

#include "discovery.h"

#include <pxr/base/arch/fileSystem.h>
#include <pxr/base/tf/envSetting.h>
#include <pxr/base/tf/getenv.h>
#include <pxr/base/tf/staticTokens.h>
#include <pxr/base/tf/stringUtils.h>
#include <pxr/usd/usd/prim.h>
#include <pxr/usd/usd/primRange.h>

PXR_NAMESPACE_OPEN_SCOPE

// clang-format off
TF_DEFINE_PRIVATE_TOKENS(_tokens,
    (shader)
    (cycles)
    (filename)
);
// clang-format on

NDR_REGISTER_DISCOVERY_PLUGIN(NdrCyclesDiscoveryPlugin);

NdrCyclesDiscoveryPlugin::NdrCyclesDiscoveryPlugin() {}

NdrCyclesDiscoveryPlugin::~NdrCyclesDiscoveryPlugin() {}

NdrNodeDiscoveryResultVec
NdrCyclesDiscoveryPlugin::DiscoverNodes(const Context& context)
{
    TfToken filename("<built-in>");
    NdrNodeDiscoveryResultVec ret;

    // TODO: Store these in proper USD Schema and read at runtime...
    std::vector<std::string> temp_nodes;
    temp_nodes.push_back("output");
    temp_nodes.push_back("diffuse_bsdf");
    temp_nodes.push_back("principled_bsdf");
    temp_nodes.push_back("glossy_bsdf");
    temp_nodes.push_back("principled_hair_bsdf");
    temp_nodes.push_back("anisotropic_bsdf");
    temp_nodes.push_back("glass_bsdf");
    temp_nodes.push_back("refraction_bsdf");
    temp_nodes.push_back("toon_bsdf");
    temp_nodes.push_back("velvet_bsdf");
    temp_nodes.push_back("translucent_bsdf");
    temp_nodes.push_back("transparent_bsdf");
    temp_nodes.push_back("subsurface_scattering");
    temp_nodes.push_back("mix_closure");
    temp_nodes.push_back("add_closure");
    temp_nodes.push_back("hair_bsdf");

    for (const std::string& n : temp_nodes) {
        std::string cycles_id = std::string("cycles:" + n);
        ret.emplace_back(NdrIdentifier(
                             TfStringPrintf(cycles_id.c_str())),  // identifier
                         NdrVersion(1, 0),                        // version
                         n.c_str(),                               // name
                         _tokens->shader,                         // family
                         _tokens->cycles,  // discoveryType
                         _tokens->cycles,  // sourceType
                         filename,         // uri
                         filename          // resolvedUri
        );
    }

    return ret;
}

const NdrStringVec&
NdrCyclesDiscoveryPlugin::GetSearchURIs() const
{
    static const auto result = []() -> NdrStringVec {
        NdrStringVec ret = TfStringSplit(TfGetenv("CYCLES_PLUGIN_PATH"),
                                         ARCH_PATH_LIST_SEP);
        ret.push_back("<built-in>");
        return ret;
    }();
    return result;
}

PXR_NAMESPACE_CLOSE_SCOPE