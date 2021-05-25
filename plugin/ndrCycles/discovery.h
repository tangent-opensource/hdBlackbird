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

#ifndef NDR_CYCLES_DISCOVERY_H
#define NDR_CYCLES_DISCOVERY_H

#include "api.h"

#include <pxr/pxr.h>
#include <pxr/usd/ndr/discoveryPlugin.h>

PXR_NAMESPACE_OPEN_SCOPE

/**
 * @brief Ndr Discovery for cycles shader nodes
 * 
 */
class NdrCyclesDiscoveryPlugin : public NdrDiscoveryPlugin {
public:
    using Context = NdrDiscoveryPluginContext;

    /**
     * @brief Creates an instance of NdrCyclesDiscoveryPlugin
     * 
     */
    NdrCyclesDiscoveryPlugin();

    /**
     * @brief Destructor of NdrCyclesDiscoveryPlugin
     * 
     */
    ~NdrCyclesDiscoveryPlugin() override;

    /**
     * @brief Discovers the cycles shaders
     * 
     * @param context NdrDiscoveryPluginContext of the discovery process
     * @return List of the discovered cycles nodes
     */
    NdrNodeDiscoveryResultVec DiscoverNodes(const Context& context) override;

    /**
     * @brief Returns the URIs used to search for cycles shader nodes.
     * 
     * @return All the paths from CYCLES_PLUGIN_PATH
     */
    const NdrStringVec& GetSearchURIs() const override;
};

PXR_NAMESPACE_CLOSE_SCOPE

#endif  // NDR_CYCLES_DISCOVERY_H
