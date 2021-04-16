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

#ifndef NDR_CYCLES_PARSER_H
#define NDR_CYCLES_PARSER_H

#include "api.h"

#include <pxr/pxr.h>
#include <pxr/usd/ndr/parserPlugin.h>

PXR_NAMESPACE_OPEN_SCOPE

/// Ndr Parser for cycles shader nodes.
class NdrCyclesParserPlugin : public NdrParserPlugin {
public:
    /**
     * @brief Creates an instance of NdrCyclesParserPlugin.
     * 
     */
    NdrCyclesParserPlugin();

    /**
     * @brief Destructor for NdrCyclesParserPlugin.
     * 
     */
    ~NdrCyclesParserPlugin() override;

    /**
     * @brief Parses a node discovery result to a NdrNode.
     * 
     * @param discoveryResult NdrNodeDiscoveryResult returned by the discovery plugin.
     * @return The parsed Ndr Node.
     */
    NdrNodeUniquePtr Parse(const NdrNodeDiscoveryResult& discoveryResult) override;

    /**
     * @brief Returns all the supported discovery types.
     * 
     * @return Returns "cycles" as the only supported discovery type.
     */
    const NdrTokenVec& GetDiscoveryTypes() const override;

    /**
     * @brief Returns all the supported source types.
     * 
     * @return Returns "cycles" as the only supported source type.
     */
    const TfToken& GetSourceType() const override;
};

PXR_NAMESPACE_CLOSE_SCOPE

#endif  // NDR_CYCLES_PARSER_H
