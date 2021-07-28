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

#ifndef BLACKBIRD_ENGINE_H
#define BLACKBIRD_ENGINE_H

#include <pxr/pxr.h>
#include <pxr/usd/sdf/path.h>
#include <pxr/usd/usd/common.h>

#include <memory>
#include <pxr/imaging/hd/renderDelegate.h>
#include <string>

PXR_NAMESPACE_OPEN_SCOPE

class HdEngine;
class ParamsDelegate;
class UsdImagingDelegate;
class HdRprimCollection;
class HdRenderIndex;
class HdRenderDelegate;
class HdxTask;
class HdRenderBuffer;
class HdRendererPlugin;

class UsdImagingBbEngine final {
public:
    UsdImagingBbEngine() = default;
    ~UsdImagingBbEngine();

    HdRendererPlugin* FindPlugin(std::string const& pluginName);
    bool OpenUsdScene(std::string const& filename);
    bool ReadRenderSettings(const std::string& path, HdRenderSettingsMap& render_settings_map);

    bool CreateDelegates(HdRendererPlugin* plugin, const HdRenderSettingsMap& render_settings);

    void SetCamera(std::string const& camera);
    void SetResolution(int x, int y);

    void Render();
    bool WriteToFile(std::string const& filename) const;

private:
    TfToken renderDelegateId;
    HdRenderDelegate* _renderDelegate;

    std::unique_ptr<HdRenderIndex> _renderIndex;
    std::unique_ptr<UsdImagingDelegate> _sceneDelegate;
    std::unique_ptr<ParamsDelegate> _paramsDelegate;
    std::unique_ptr<HdEngine> _engine;

    UsdStageRefPtr _stage;

    std::vector<SdfPath> _taskIds;
    std::vector<SdfPath> _bufferIds;

    HdRenderBuffer* _renderBuffer;
    SdfPath _renderBufferId;
    SdfPath _renderTaskId;
};

PXR_NAMESPACE_CLOSE_SCOPE

#endif  //BLACKBIRD_ENGINE_H