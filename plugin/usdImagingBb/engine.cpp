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
#include <tbb/task_scheduler_init.h>

#ifdef USE_HBOOST
#    include <hboost/program_options.hpp>
#else
#    include <boost/program_options.hpp>
#endif

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

UsdImagingBbEngine::~UsdImagingBbEngine() {}

bool
UsdImagingBbEngine::CreateRenderDelegate(const std::string& delegateName)
{
    PlugRegistry& plug_registry = PlugRegistry::GetInstance();
    TF_UNUSED(plug_registry);

    renderDelegateId = TfToken { delegateName };
    HdRendererPluginRegistry& plugin_registry = HdRendererPluginRegistry::GetInstance();
    HdRendererPlugin* plugin = plugin_registry.GetRendererPlugin(renderDelegateId);
    if (!plugin) {
        return false;
    }

    _renderDelegate = plugin->CreateRenderDelegate();
    if (!_renderDelegate) {
        return false;
    }

    // create render index
    _renderIndex = std::unique_ptr<HdRenderIndex>(HdRenderIndex::New(_renderDelegate, HdDriverVector {}));
    if (!_renderIndex) {
        return false;
    }

    // create engine
    _engine = std::make_unique<HdEngine>();

    return true;
}

bool
UsdImagingBbEngine::OpenScene(const std::string& filename)
{
    // open usd scene
    UsdStageRefPtr usdStage = UsdStage::Open(filename);
    if (!usdStage) {
        return false;
    }

    const SdfPath sceneDelegateId = SdfPath::AbsoluteRootPath();
    _sceneDelegate = std::make_unique<UsdImagingDelegate>(_renderIndex.get(), sceneDelegateId);

    //
    _paramsDelegate = std::make_unique<ParamsDelegate>(_renderIndex.get(), SdfPath { "/task_controller" });

    // Populate usd stage
    _stage = usdStage;
    _sceneDelegate->Populate(_stage->GetPseudoRoot());


    //
    // Render Buffers
    //
    _renderBufferId = SdfPath { "/task_controller/render_buffer" };
    {
        _bufferIds.emplace_back("/task_controller/render_buffer");
        _renderIndex->InsertBprim(HdPrimTypeTokens->renderBuffer, _paramsDelegate.get(), _renderBufferId);
        _renderBuffer = dynamic_cast<HdRenderBuffer*>(
            _renderIndex->GetBprim(HdPrimTypeTokens->renderBuffer, _renderBufferId));

        HdRenderBufferDescriptor desc {};
        desc.multiSampled = false;
        desc.format = HdFormatFloat32Vec4;
        _paramsDelegate->SetParameter(_renderBufferId, _tokens->renderBufferDescriptor, desc);
    }

    //
    // Tasks
    //
    HdRprimCollection collection = HdRprimCollection(HdTokens->geometry, HdReprSelector(HdReprTokens->hull));
    collection.SetRootPath(SdfPath::AbsoluteRootPath());

    _renderTaskId = SdfPath { "/task_controller/render_task" };

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

    return true;
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

void
UsdImagingBbEngine::SetResolution(int x, int y)
{
    // iterate over buffers
    for (auto& bufferId : _bufferIds) {
        const HdBprim* buffer_prim = _renderIndex->GetBprim(HdPrimTypeTokens->renderBuffer, bufferId);
        if (!buffer_prim) {
            continue;
        }

        auto descr = _paramsDelegate->GetParameter<HdRenderBufferDescriptor>(bufferId, _tokens->renderBufferDescriptor);
        descr.dimensions = GfVec3i { x, y, 1 };
        _paramsDelegate->SetParameter(bufferId, _tokens->renderBufferDescriptor, descr);
    }

    // iterate over tasks
    auto tasks = GetTasks(_renderIndex.get(), _taskIds);
    for (auto& taskId : _taskIds) {
        const HdTaskSharedPtr& task = _renderIndex->GetTask(taskId);

        auto renderTask = dynamic_cast<const HdxRenderTask*>(task.get());
        if (!renderTask) {
            continue;
        }

        auto params = _paramsDelegate->GetParameter<HdxRenderTaskParams>(taskId, HdTokens->params);
        params.viewport = GfVec4d(0, 0, x, y);
        _paramsDelegate->SetParameter(taskId, HdTokens->params, params);
        _renderIndex->GetChangeTracker().MarkTaskDirty(taskId, HdChangeTracker::DirtyParams);
    }
}

PXR_NAMESPACE_CLOSE_SCOPE

#include <iostream>

PXR_NAMESPACE_USING_DIRECTIVE

namespace po = BOOST_NS::program_options;
using BOOST_NS::array;
using Resolution = std::vector<int>;


int
main(int argc, char** argv)
{
    // clang goes nuts here
    // clang-format off
    po::options_description desc {};
    desc.add_options()("help", "Produce help message")
        ("usd-input", po::value<std::string>(), "The USD file for the scene")
        ("camera,c", po::value<std::string>()->default_value("/cameras/camera1"), "Render from the specified camera")
        ("output,o", po::value<std::string>(), "Output image")
        ("res,r", po::value<Resolution>()->multitoken()->default_value(Resolution{1280, 720}, "1280 720"), "Image resolution (e.g. '--res 1280 720')")
        ("renderer,R", po::value<std::string>()->default_value("HdCyclesRendererPlugin"), "Choose a specific delegate. Default is Blackbird")
        ("threads,j",po::value<int>()->default_value(-1),"Choose an specific delegate. Default is Blackbird");
    // clang-format on

    po::variables_map vm;
    po::store(po::command_line_parser(argc, argv).options(desc).run(), vm);
    po::notify(vm);

    if (vm.count("help")) {
        std::cout << desc << '\n';
        return EXIT_FAILURE;
    }

    if (!vm.count("usd-input")) {
        std::cout << "Missing 'usd-input' argument!" << '\n';
        std::cout << desc << '\n';
        return EXIT_FAILURE;
    }

    if (!vm.count("output")) {
        std::cout << "Missing 'output' argument!" << '\n';
        std::cout << desc << '\n';
        return EXIT_FAILURE;
    }

    //
    // initialize thread count
    //
    tbb::task_scheduler_init scheduler_init { vm["threads"].as<int>() };

    // create engine
    UsdImagingBbEngine engine;

    // create delegate
    {
        auto renderer = vm["renderer"].as<std::string>();
        if (!engine.CreateRenderDelegate(renderer)) {
            std::cout << "Unable to create delegate with name: " << renderer << '\n';
            return EXIT_FAILURE;
        }
    }

    // open usd scene
    {
        auto scene = vm["usd-input"].as<std::string>();
        if (!engine.OpenScene(scene)) {
            std::cout << "Unable to open scene: " << scene << '\n';
            return EXIT_FAILURE;
        }
    }

    // set properties
    {
        auto camera = vm["camera"].as<std::string>();
        engine.SetCamera(camera);

        auto res = vm["res"].as<Resolution>();
        engine.SetResolution(res[0], res[1]);
    }

    engine.Render();

    {
        auto output = vm["output"].as<std::string>();
        engine.WriteToFile(output);
    }

    return EXIT_SUCCESS;
}
