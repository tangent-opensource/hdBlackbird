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

#ifndef HDBLACKBIRD_ENGINE_H
#define HDBLACKBIRD_ENGINE_H

#include <pxr/pxr.h>
#include <pxr/usd/sdf/path.h>
#include <pxr/usd/usd/common.h>
#include <pxr/imaging/hd/renderIndex.h>

#include <string>
#include <memory>

PXR_NAMESPACE_OPEN_SCOPE

class HdEngine;
class ParamsDelegate;
class UsdImagingDelegate;
class HdRprimCollection;
class HdRenderIndex;
class HdRenderDelegate;
class HdxTask;
class HdRenderBuffer;

class UsdImagingBbEngine final {
public:
    explicit UsdImagingBbEngine(std::string const& usdFilePath);
    ~UsdImagingBbEngine();

    void Render();
    bool IsConverged() const;
    bool WriteToFile(std::string const& filename) const;

public:
    void _Init(UsdStageRefPtr const& usdStage, HdRprimCollection const& collection, SdfPath const& delegateId,
               TfTokenVector const& renderTags);

    std::unique_ptr<HdEngine> _engine;
    std::unique_ptr<HdRenderIndex> _renderIndex;
    std::unique_ptr<UsdImagingDelegate> _sceneDelegate;
    std::unique_ptr<ParamsDelegate> _paramsDelegate;

    HdRenderDelegate* _renderDelegate;
    HdTaskSharedPtrVector _renderTasks;

    UsdStageRefPtr _stage;

    HdRenderBuffer* _renderBuffer;
    SdfPath _cameraId;
    SdfPath _renderBufferId;
    SdfPath _renderSetupTaskId;
    SdfPath _renderTaskId;
};

PXR_NAMESPACE_CLOSE_SCOPE

#endif  //HDBLACKBIRD_ENGINE_H
