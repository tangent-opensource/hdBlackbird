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

#include "engine.h"

#include <pxr/pxr.h>
#include <pxr/usdImaging/usdImaging/delegate.h>

#include <pxr/imaging/hd/engine.h>
#include <pxr/imaging/hd/renderBuffer.h>
#include <pxr/imaging/hd/renderIndex.h>
#include <pxr/imaging/hd/rendererPlugin.h>
#include <pxr/imaging/hd/rendererPluginRegistry.h>

#include <pxr/imaging/hdx/renderSetupTask.h>
#include <pxr/imaging/hdx/renderTask.h>

#include <pxr/imaging/hgi/hgi.h>
#include <pxr/imaging/hgi/tokens.h>

#include <pxr/base/arch/systemInfo.h>
#include <pxr/base/plug/plugin.h>
#include <pxr/base/plug/registry.h>

#include <OpenImageIO/imageio.h>

PXR_NAMESPACE_OPEN_SCOPE

namespace {

HdTaskSharedPtrVector
GetTasks(HdRenderIndex* renderIndex, const std::vector<SdfPath>& taskIds)
{
    HdTaskSharedPtrVector tasks;
    tasks.reserve(taskIds.size());
    for (const auto& taskId : taskIds) {
        const auto& task = renderIndex->GetTask(taskId);
        tasks.push_back(task);
    }
    return tasks;
}

bool
IsConverged(const HdTaskSharedPtrVector& tasks)
{
    bool converged = true;
    for (auto const& task : tasks) {
        std::shared_ptr<HdxTask> progressiveTask = std::dynamic_pointer_cast<HdxTask>(task);
        if (progressiveTask) {
            converged = converged && progressiveTask->IsConverged();
            if (!converged) {
                break;
            }
        }
    }
    return converged;
}

}  //namespace

TF_DEFINE_PRIVATE_TOKENS(_tokens, (renderBufferDescriptor));

///
/// HdSceneDelegate provides Get only interface, but not Set. For tasks and render buffers we need to set parameters
/// ParamsDelegate mimics setting behaviour. Tasks and Buffers are added to it and it's parameters are kept in
/// the maps.
///
class ParamsDelegate final : public HdSceneDelegate {
public:
    ParamsDelegate(HdRenderIndex* parentIndex, SdfPath const& delegateID)
        : HdSceneDelegate(parentIndex, delegateID)
    {
    }

    template<typename T> void SetParameter(SdfPath const& id, TfToken const& key, T const& value)
    {
        _valueCacheMap[id][key] = value;
    }

    template<typename T> T GetParameter(SdfPath const& id, TfToken const& key) const
    {
        VtValue vParams;
        _ValueCache vCache;
        TF_VERIFY(TfMapLookup(_valueCacheMap, id, &vCache) && TfMapLookup(vCache, key, &vParams)
                  && vParams.IsHolding<T>());
        return vParams.Get<T>();
    }

    VtValue Get(SdfPath const& id, TfToken const& key) override
    {
        _ValueCache* vcache = TfMapLookupPtr(_valueCacheMap, id);
        VtValue ret;
        if (vcache && TfMapLookup(*vcache, key, &ret)) {
            return ret;
        }
        TF_CODING_ERROR("%s:%s doesn't exist in the value cache\n", id.GetText(), key.GetText());
        return VtValue();
    }

    HdRenderBufferDescriptor GetRenderBufferDescriptor(SdfPath const& id) override
    {
        return GetParameter<HdRenderBufferDescriptor>(id, _tokens->renderBufferDescriptor);
    }

private:
    using _ValueCache = TfHashMap<TfToken, VtValue, TfToken::HashFunctor>;
    using _ValueCacheMap = TfHashMap<SdfPath, _ValueCache, SdfPath::Hash>;
    _ValueCacheMap _valueCacheMap;
};

UsdImagingBbEngine::UsdImagingBbEngine(const std::string& usdFilePath)
{
    HdRprimCollection collection = HdRprimCollection(HdTokens->geometry, HdReprSelector(HdReprTokens->hull));
    collection.SetRootPath(SdfPath::AbsoluteRootPath());

    TfTokenVector renderTags;
    renderTags.push_back(HdRenderTagTokens->geometry);

    _Init(UsdStage::Open(usdFilePath), collection, SdfPath::AbsoluteRootPath(), renderTags);
}

bool
UsdImagingBbEngine::CreateRenderDelegate(const std::string& delegateName)
{
    PlugRegistry& plug_registry = PlugRegistry::GetInstance();

    renderDelegateId = TfToken { delegateName };
    HdRendererPluginRegistry& plugin_registry = HdRendererPluginRegistry::GetInstance();
    HdRendererPlugin* plugin = plugin_registry.GetRendererPlugin(renderDelegateId);
    _renderDelegate = plugin->CreateRenderDelegate();
    return true;
}

UsdImagingBbEngine::~UsdImagingBbEngine() {}

void
UsdImagingBbEngine::_Init(const UsdStageRefPtr& usdStage, const HdRprimCollection& collection,
                          const SdfPath& delegateId, const TfTokenVector& renderTags)
{
    _renderBufferId = SdfPath { "/task_controller/render_buffer" };
    _renderTaskId = SdfPath { "/task_controller/render_task" };

    CreateRenderDelegate("HdCyclesRendererPlugin");

    // create render index
    _renderIndex = std::unique_ptr<HdRenderIndex>(HdRenderIndex::New(_renderDelegate, HdDriverVector {}));
    TF_VERIFY(_renderIndex != nullptr);

    const SdfPath sceneDelegateId = SdfPath::AbsoluteRootPath();
    _sceneDelegate = std::make_unique<UsdImagingDelegate>(_renderIndex.get(), sceneDelegateId);

    //
    _paramsDelegate = std::make_unique<ParamsDelegate>(_renderIndex.get(), SdfPath { "/task_controller" });

    //
    _engine = std::make_unique<HdEngine>();

    // Populate usd stage
    _stage = usdStage;
    _sceneDelegate->Populate(_stage->GetPseudoRoot());

    //
    // Render Buffers
    //

    {
        _renderIndex->InsertBprim(HdPrimTypeTokens->renderBuffer, _paramsDelegate.get(), _renderBufferId);
        _renderBuffer = dynamic_cast<HdRenderBuffer*>(
            _renderIndex->GetBprim(HdPrimTypeTokens->renderBuffer, _renderBufferId));

        HdRenderBufferDescriptor desc {};
        desc.multiSampled = false;
        desc.dimensions = GfVec3i { 1200, 700, 1 };
        desc.format = HdFormatFloat32Vec4;
        _paramsDelegate->SetParameter(_renderBufferId, _tokens->renderBufferDescriptor, desc);
    }

    //
    // Tasks
    //

    {
        _renderIndex->InsertTask<HdxRenderTask>(_paramsDelegate.get(), _renderTaskId);

        HdxRenderTaskParams params {};
        params.viewport = GfVec4d(0, 0, 1200, 700);

        // AOV binding must not be empty, empty is assumed to be GL
        HdRenderPassAovBindingVector aov_binding;
        aov_binding.emplace_back();
        aov_binding.back().aovName = HdAovTokens->color;
        aov_binding.back().renderBufferId = _renderBufferId;
        aov_binding.back().renderBuffer = _renderBuffer;

        params.aovBindings = aov_binding;
        _paramsDelegate->SetParameter(_renderTaskId, HdTokens->params, params);

        _paramsDelegate->SetParameter(_renderTaskId, HdTokens->collection, collection);
        _taskIds.push_back(_renderTaskId);
    }
}

void
UsdImagingBbEngine::Render()
{
    auto tasks = GetTasks(_renderIndex.get(), _taskIds);

    do {
        TF_PY_ALLOW_THREADS_IN_SCOPE();
        _engine->Execute(&_sceneDelegate->GetRenderIndex(), &tasks);
    } while (!IsConverged(tasks));
}

bool
UsdImagingBbEngine::WriteToFile(const std::string& filename) const
{
    using namespace OIIO;
    std::unique_ptr<ImageOutput> out = ImageOutput::create(filename);
    if (!out) {
        return false;
    }

    HdFormat format = _renderBuffer->GetFormat();
    if (format != HdFormatFloat32Vec4) {
        return false;
    }

    void* data = _renderBuffer->Map();

    unsigned int xres = _renderBuffer->GetWidth();
    unsigned int yres = _renderBuffer->GetHeight();

    ImageSpec spec(xres, yres, 4, TypeDesc::TypeFloat4);
    out->open(filename, spec);
    out->write_image(TypeDesc::FLOAT, data);
    out->close();

    _renderBuffer->Unmap();
    return true;
}

void
UsdImagingBbEngine::SetCamera(const std::string& camera)
{
    // confirm that camera exists
    SdfPath cameraId { camera };
    HdSprim* cameraPrim = _renderIndex->GetSprim(HdPrimTypeTokens->camera, cameraId);
    if (!cameraPrim) {
        // TODO camera not found
        return;
    }

    auto tasks = GetTasks(_renderIndex.get(), _taskIds);
    for (auto& taskId : _taskIds) {
        const HdTaskSharedPtr& task = _renderIndex->GetTask(taskId);

        auto renderTask = dynamic_cast<const HdxRenderTask*>(task.get());
        if (!renderTask) {
            continue;
        }

        // get existing params and update camera
        auto params = _paramsDelegate->GetParameter<HdxRenderTaskParams>(taskId, HdTokens->params);
        params.camera = SdfPath { camera };
        _paramsDelegate->SetParameter(taskId, HdTokens->params, params);
        _renderIndex->GetChangeTracker().MarkTaskDirty(taskId, HdChangeTracker::DirtyParams);
    }
}

PXR_NAMESPACE_CLOSE_SCOPE

PXR_NAMESPACE_USING_DIRECTIVE

int
main()
{
    // cre
    UsdImagingBbEngine engine { "/home/bareya/Downloads/toy.usda" };
    engine.SetCamera("/cameras/camera1");

    engine.Render();
    engine.WriteToFile("/tmp/render.jpeg");

    return 0;
}
