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

#ifndef HD_CYCLES_RENDERER_PLUGIN_H
#define HD_CYCLES_RENDERER_PLUGIN_H

#include "api.h"

#include <pxr/imaging/hd/rendererPlugin.h>
#include <pxr/pxr.h>

PXR_NAMESPACE_OPEN_SCOPE

/**
 * @brief First entry point into HdCycles.
 * Allows for the creation and deletion of the core
 * render delegate classes.
 * 
 */
class HdCyclesRendererPlugin final : public HdRendererPlugin {
public:
    /**
     * @brief Use default constructor
     * 
     */
    HdCyclesRendererPlugin();

    /**
     * @brief Use default destructor
     * 
     */
    ~HdCyclesRendererPlugin() override;

    /**
     * @brief Construct a new render delegate of type HdCyclesRenderDelegate
     * 
     * @return Created Render Delegate
     */
    HdRenderDelegate* CreateRenderDelegate() override;

    HdRenderDelegate*
    CreateRenderDelegate(HdRenderSettingsMap const& settingsMap) override;

    /**
     * @brief Destroy a render delegate created by this class
     * 
     * @param renderDelegate The render delegate to delete
     * @return 
     */
        void DeleteRenderDelegate(HdRenderDelegate* renderDelegate) override;

    /**
     * @brief Checks to see if the plugin is supported on the running system
     * 
     */
    bool IsSupported() const override;

private:
    /**
     * @brief This class does not support copying
     * 
     */
    HdCyclesRendererPlugin(const HdCyclesRendererPlugin&) = delete;
    HdCyclesRendererPlugin& operator=(const HdCyclesRendererPlugin&) = delete;
};

PXR_NAMESPACE_CLOSE_SCOPE

#endif  // HD_CYCLES_RENDERER_PLUGIN_H
