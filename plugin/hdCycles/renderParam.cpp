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

#include "renderParam.h"

#include "config.h"
#include "renderDelegate.h"
#include "utils.h"

#include <memory>

#include <device/device.h>
#include <render/background.h>
#include <render/buffers.h>
#include <render/camera.h>
#include <render/curves.h>
#include <render/hair.h>
#include <render/integrator.h>
#include <render/light.h>
#include <render/mesh.h>
#include <render/nodes.h>
#include <render/object.h>
#include <render/scene.h>
#include <render/session.h>
#include <render/stats.h>

PXR_NAMESPACE_OPEN_SCOPE

double
clamp(double d, double min, double max)
{
    const double t = d < min ? min : d;
    return t > max ? max : t;
}

HdCyclesRenderParam::HdCyclesRenderParam()
    : m_shouldUpdate(false)
    , m_renderProgress(0)
{
    _InitializeDefaults();
}

void
HdCyclesRenderParam::_InitializeDefaults()
{
    static const HdCyclesConfig& config = HdCyclesConfig::GetInstance();
    m_deviceName                        = config.device_name;
    m_useMotionBlur                     = config.enable_motion_blur;
}

float
HdCyclesRenderParam::GetProgress()
{
    return m_cyclesSession->progress.get_progress();
}

void
HdCyclesRenderParam::_SessionPrintStatus()
{
    std::string status, substatus;

    /* get status */
    float progress = m_cyclesSession->progress.get_progress();
    m_cyclesSession->progress.get_status(status, substatus);

    int newProgress = (int)(round(progress * 100));
    if (newProgress != m_renderProgress) {
        m_renderProgress = newProgress;

        if (HdCyclesConfig::GetInstance().enable_progress) {
            std::cout << "Progress: " << m_renderProgress << "%\n";
        }
    }

    if (HdCyclesConfig::GetInstance().enable_logging) {
        if (substatus != "")
            status += ": " + substatus;

        std::cout << "cycles: " << progress << " : " << status << '\n';
    }
}

bool
HdCyclesRenderParam::Initialize()
{
    return _CyclesInitialize();
}

void
HdCyclesRenderParam::StartRender()
{
    CyclesStart();
}

void
HdCyclesRenderParam::StopRender()
{
    _CyclesExit();
}

void
HdCyclesRenderParam::RestartRender()
{
    StopRender();
    Initialize();
    StartRender();
}

void
HdCyclesRenderParam::PauseRender()
{
    m_cyclesSession->set_pause(true);
}

void
HdCyclesRenderParam::ResumeRender()
{
    m_cyclesSession->set_pause(false);
}

void
HdCyclesRenderParam::Interrupt(bool a_forceUpdate)
{
    m_shouldUpdate = true;
    PauseRender();
}

void
HdCyclesRenderParam::CommitResources()
{
    if (m_shouldUpdate) {
        if (m_cyclesScene->lights.size() > 0) {
            if (!m_hasDomeLight)
                SetBackgroundShader(nullptr, false);
        } else {
            SetBackgroundShader(nullptr, true);
        }

        CyclesReset(false);
        m_shouldUpdate = false;
        ResumeRender();
    }
}

void
HdCyclesRenderParam::SetBackgroundShader(ccl::Shader* a_shader, bool a_emissive)
{
    if (a_shader)
        m_cyclesScene->default_background = a_shader;
    else {
        // TODO: These aren't properly destroyed from memory

        // Create empty background shader
        m_cyclesScene->default_background        = new ccl::Shader();
        m_cyclesScene->default_background->name  = "default_background";
        m_cyclesScene->default_background->graph = new ccl::ShaderGraph();
        if (a_emissive) {
            ccl::BackgroundNode* bgNode = new ccl::BackgroundNode();
            bgNode->color               = ccl::make_float3(0.6f, 0.6f, 0.6f);

            m_cyclesScene->default_background->graph->add(bgNode);

            ccl::ShaderNode* out
                = m_cyclesScene->default_background->graph->output();
            m_cyclesScene->default_background->graph->connect(
                bgNode->output("Background"), out->input("Surface"));
        }

        m_cyclesScene->default_background->tag_update(m_cyclesScene);

        m_cyclesScene->shaders.push_back(m_cyclesScene->default_background);
    }
    m_cyclesScene->background->tag_update(m_cyclesScene);
}

/* ======= Cycles Settings ======= */

// -- Use Experimental Cycles rendering

const bool&
HdCyclesRenderParam::GetUseExperimental()
{
    return m_cyclesSession->params.experimental;
}

void
HdCyclesRenderParam::SetUseExperimental(const bool& a_value)
{
    m_cyclesSession->params.experimental = a_value;
}

// -- Maximum samples used in render

const int&
HdCyclesRenderParam::GetMaxSamples()
{
    return m_cyclesSession->params.samples;
}

void
HdCyclesRenderParam::SetMaxSamples(const int& a_value)
{
    m_cyclesSession->params.samples = a_value;
}

// -- Number of threads used to render

const int&
HdCyclesRenderParam::GetNumThreads()
{
    return m_cyclesSession->params.threads;
}

void
HdCyclesRenderParam::SetNumThreads(const int& a_value)
{
    m_cyclesSession->params.threads = a_value;
}

// -- Size of individual pixel

const int&
HdCyclesRenderParam::GetPixelSize()
{
    return m_cyclesSession->params.pixel_size;
}

void
HdCyclesRenderParam::SetPixelSize(const int& a_value)
{
    m_cyclesSession->params.pixel_size = a_value;
}

// -- Size of render tile

const pxr::GfVec2i
HdCyclesRenderParam::GetTileSize()
{
    return pxr::GfVec2i(m_cyclesSession->params.tile_size.x,
                        m_cyclesSession->params.tile_size.y);
}

void
HdCyclesRenderParam::SetTileSize(const pxr::GfVec2i& a_value)
{
    m_cyclesSession->params.tile_size.x = a_value[0];
    m_cyclesSession->params.tile_size.y = a_value[1];
}

void
HdCyclesRenderParam::SetTileSize(int a_x, int a_y)
{
    m_cyclesSession->params.tile_size.x = a_x;
    m_cyclesSession->params.tile_size.y = a_y;
}

// -- Resolution of initial progressive render

const int&
HdCyclesRenderParam::GetStartResolution()
{
    return m_cyclesSession->params.start_resolution;
}

void
HdCyclesRenderParam::SetStartResolution(const int& a_value)
{
    m_cyclesSession->params.start_resolution = a_value;
}

// -- Exposure of film

const float&
HdCyclesRenderParam::GetExposure()
{
    return m_cyclesScene->film->exposure;
}

void
HdCyclesRenderParam::SetExposure(float a_exposure)
{
    m_cyclesScene->film->exposure = a_exposure;
    m_cyclesScene->film->tag_update(m_cyclesScene);
}

// -- Cycles render device

const ccl::DeviceType&
HdCyclesRenderParam::GetDeviceType()
{
    return m_deviceType;
}

const std::string&
HdCyclesRenderParam::GetDeviceTypeName()
{
    return m_deviceName;
}

bool
HdCyclesRenderParam::SetDeviceType(ccl::DeviceType a_deviceType,
                                   ccl::SessionParams& params)
{
    if (a_deviceType == ccl::DeviceType::DEVICE_NONE) {
        TF_WARN("Attempted to set device of type DEVICE_NONE.");
        return false;
    }

    m_deviceType = a_deviceType;
    m_deviceName = ccl::Device::string_from_type(a_deviceType);

    return _SetDevice(m_deviceType, params);
}

bool
HdCyclesRenderParam::SetDeviceType(const std::string& a_deviceType,
                                   ccl::SessionParams& params)
{
    return SetDeviceType(ccl::Device::type_from_string(a_deviceType.c_str()),
                         params);
}

bool
HdCyclesRenderParam::SetDeviceType(const std::string& a_deviceType)
{
    return SetDeviceType(a_deviceType, m_cyclesSession->params);
}

bool
HdCyclesRenderParam::_SetDevice(const ccl::DeviceType& a_deviceType,
                                ccl::SessionParams& params)
{
    std::vector<ccl::DeviceInfo> devices = ccl::Device::available_devices(
        (ccl::DeviceTypeMask)(1 << a_deviceType));

    bool device_available = false;

    if (!devices.empty()) {
        params.device    = devices.front();
        device_available = true;
    }

    if (params.device.type == ccl::DEVICE_NONE || !device_available) {
        TF_RUNTIME_ERROR("No device available exiting.");
    }

    return device_available;
}

// -- Shutter motion position

void
HdCyclesRenderParam::SetShutterMotionPosition(const int& a_value)
{
    SetShutterMotionPosition((ccl::Camera::MotionPosition)a_value);
}

void
HdCyclesRenderParam::SetShutterMotionPosition(
    const ccl::Camera::MotionPosition& a_value)
{
    m_cyclesScene->camera->motion_position = a_value;
}

const ccl::Camera::MotionPosition&
HdCyclesRenderParam::GetShutterMotionPosition()
{
    return m_cyclesScene->camera->motion_position;
}

/* ====== HdCycles Settings ====== */

/* ====== Cycles Lifecycle ====== */

bool
HdCyclesRenderParam::_CyclesInitialize()
{
    static const HdCyclesConfig& config = HdCyclesConfig::GetInstance();

    ccl::SessionParams params;
    params.display_buffer_linear = config.display_buffer_linear;

    params.shadingsystem = ccl::SHADINGSYSTEM_SVM;
    if (config.shading_system == "OSL"
        || config.shading_system == "SHADINGSYSTEM_OSL")
        params.shadingsystem = ccl::SHADINGSYSTEM_OSL;

    params.background = false;

    /* Use progressive rendering */

    params.progressive = true;

    params.start_resolution = config.start_resolution;

    params.progressive_refine         = false;
    params.progressive_update_timeout = 0.1;
    params.pixel_size                 = config.pixel_size;
    params.tile_size.x                = config.tile_size[0];
    params.tile_size.y                = config.tile_size[1];
    params.samples                    = config.max_samples;

    /* find matching device */

    bool foundDevice = SetDeviceType(m_deviceName, params);

    if (!foundDevice)
        return false;

    m_cyclesSession = new ccl::Session(params);

    if (HdCyclesConfig::GetInstance().enable_logging
        || HdCyclesConfig::GetInstance().enable_progress)
        m_cyclesSession->progress.set_update_callback(
            std::bind(&HdCyclesRenderParam::_SessionPrintStatus, this));

    // -- Scene init
    ccl::SceneParams sceneParams;
    sceneParams.shadingsystem = params.shadingsystem;

    sceneParams.bvh_type = ccl::SceneParams::BVH_DYNAMIC;
    if (config.bvh_type == "STATIC")
        sceneParams.bvh_type = ccl::SceneParams::BVH_STATIC;

    sceneParams.persistent_data = true;

    m_cyclesScene = new ccl::Scene(sceneParams, m_cyclesSession->device);

    m_width  = config.render_width;
    m_height = config.render_height;

    m_cyclesScene->camera->width  = m_width;
    m_cyclesScene->camera->height = m_height;

    m_cyclesScene->camera->compute_auto_viewplane();

    m_cyclesSession->scene = m_cyclesScene;

    m_bufferParams.width       = m_width;
    m_bufferParams.height      = m_height;
    m_bufferParams.full_width  = m_width;
    m_bufferParams.full_height = m_height;

    m_cyclesScene->film->exposure = config.exposure;

    if (config.enable_transparent_background)
        m_cyclesScene->background->transparent = true;

    if (m_useMotionBlur) {
        SetShutterMotionPosition(config.shutter_motion_position);

        m_cyclesScene->camera->shuttertime = 0.5f;
        m_cyclesScene->camera->motion.clear();
        m_cyclesScene->camera->motion.resize(m_motionSteps,
                                             m_cyclesScene->camera->matrix);

        m_cyclesScene->integrator->motion_blur = true;
        m_cyclesScene->integrator->tag_update(m_cyclesScene);
    }

    default_vcol_surface = HdCyclesCreateDefaultShader();

    default_vcol_surface->tag_update(m_cyclesScene);
    m_cyclesScene->shaders.push_back(default_vcol_surface);

    SetBackgroundShader(nullptr);

    m_cyclesSession->reset(m_bufferParams, params.samples);


    return true;
}

void
HdCyclesRenderParam::CyclesStart()
{
    m_cyclesSession->start();
}

void
HdCyclesRenderParam::_CyclesExit()
{
    m_cyclesSession->set_pause(true);

    m_cyclesScene->mutex.lock();

    m_cyclesScene->shaders.clear();
    m_cyclesScene->geometry.clear();
    m_cyclesScene->objects.clear();
    m_cyclesScene->lights.clear();
    m_cyclesScene->particle_systems.clear();

    m_cyclesScene->mutex.unlock();

    if (m_cyclesSession) {
        delete m_cyclesSession;
        m_cyclesSession = nullptr;
    }
}

// TODO: Refactor these two resets
void
HdCyclesRenderParam::CyclesReset(bool a_forceUpdate)
{
    m_cyclesScene->mutex.lock();

    m_cyclesSession->progress.reset();

    if (m_curveUpdated || m_meshUpdated || m_geometryUpdated
        || m_shadersUpdated) {
        m_cyclesScene->geometry_manager->tag_update(m_cyclesScene);
        m_geometryUpdated = false;
        m_meshUpdated     = false;
    }

    if (m_curveUpdated) {
        m_curveUpdated = false;
    }

    if (m_objectsUpdated || m_shadersUpdated) {
        m_cyclesScene->object_manager->tag_update(m_cyclesScene);
        m_objectsUpdated = false;
    }
    if (m_lightsUpdated) {
        m_cyclesScene->light_manager->tag_update(m_cyclesScene);
        m_lightsUpdated = false;
    }

    if (a_forceUpdate) {
        m_cyclesScene->integrator->tag_update(m_cyclesScene);
        m_cyclesScene->background->tag_update(m_cyclesScene);
        m_cyclesScene->film->tag_update(m_cyclesScene);
    }


    //m_cyclesScene->shaders->tag_update( m_cyclesScene );
    m_cyclesSession->reset(m_bufferParams, m_cyclesSession->params.samples);
    m_cyclesScene->mutex.unlock();
}

void
HdCyclesRenderParam::CyclesReset(int w, int h)
{
    m_width                       = w;
    m_height                      = h;
    m_bufferParams.width          = w;
    m_bufferParams.height         = h;
    m_bufferParams.full_width     = w;
    m_bufferParams.full_height    = h;
    m_cyclesScene->camera->width  = w;
    m_cyclesScene->camera->height = h;
    m_cyclesScene->camera->compute_auto_viewplane();
    m_cyclesScene->camera->need_update        = true;
    m_cyclesScene->camera->need_device_update = true;
    m_cyclesSession->reset(m_bufferParams, m_cyclesSession->params.samples);
}

void
HdCyclesRenderParam::DirectReset()
{
    m_cyclesSession->reset(m_bufferParams, m_cyclesSession->params.samples);
}

void
HdCyclesRenderParam::AddLight(ccl::Light* a_light)
{
    if (!m_cyclesScene) {
        TF_WARN("Couldn't add light to scene. Scene is null.");
        return;
    }

    m_lightsUpdated = true;

    m_cyclesScene->mutex.lock();
    m_cyclesScene->lights.push_back(a_light);
    m_cyclesScene->mutex.unlock();

    if (a_light->type == ccl::LIGHT_BACKGROUND) {
        m_hasDomeLight = true;
    }
}

void
HdCyclesRenderParam::AddObject(ccl::Object* a_object)
{
    if (!m_cyclesScene) {
        TF_WARN("Couldn't add object to scene. Scene is null.");
        return;
    }

    m_objectsUpdated = true;

    m_cyclesScene->mutex.lock();
    m_cyclesScene->objects.push_back(a_object);
    m_cyclesScene->mutex.unlock();

    Interrupt();
}

void
HdCyclesRenderParam::AddGeometry(ccl::Geometry* a_geometry)
{
    if (!m_cyclesScene) {
        TF_WARN("Couldn't add geometry to scene. Scene is null.");
        return;
    }

    m_geometryUpdated = true;

    m_cyclesScene->mutex.lock();
    m_cyclesScene->geometry.push_back(a_geometry);
    m_cyclesScene->mutex.unlock();

    Interrupt();
}

void
HdCyclesRenderParam::AddMesh(ccl::Mesh* a_mesh)
{
    if (!m_cyclesScene) {
        TF_WARN("Couldn't add geometry to scene. Scene is null.");
        return;
    }

    m_meshUpdated = true;

    m_cyclesScene->mutex.lock();
    m_cyclesScene->geometry.push_back(a_mesh);
    m_cyclesScene->mutex.unlock();

    Interrupt();
}

void
HdCyclesRenderParam::AddCurve(ccl::Geometry* a_curve)
{
    if (!m_cyclesScene) {
        TF_WARN("Couldn't add geometry to scene. Scene is null.");
        return;
    }

    m_curveUpdated = true;

    m_cyclesScene->mutex.lock();
    m_cyclesScene->geometry.push_back(a_curve);
    m_cyclesScene->mutex.unlock();

    Interrupt();
}

void
HdCyclesRenderParam::AddShader(ccl::Shader* a_shader)
{
    if (!m_cyclesScene) {
        TF_WARN("Couldn't add geometry to scene. Scene is null.");
        return;
    }

    m_shadersUpdated = true;

    m_cyclesScene->mutex.lock();
    m_cyclesScene->shaders.push_back(a_shader);
    m_cyclesScene->mutex.unlock();
}

void
HdCyclesRenderParam::RemoveObject(ccl::Object* a_object)
{
    for (ccl::vector<ccl::Object*>::iterator it = m_cyclesScene->objects.begin();
         it != m_cyclesScene->objects.end();) {
        if (a_object == *it) {
            m_cyclesScene->mutex.lock();
            it = m_cyclesScene->objects.erase(it);

            m_objectsUpdated = true;
            m_cyclesScene->mutex.unlock();
            break;
        } else {
            ++it;
        }
    }

    if (m_objectsUpdated)
        Interrupt();
}

void
HdCyclesRenderParam::RemoveLight(ccl::Light* a_light)
{
    if (a_light->type == ccl::LIGHT_BACKGROUND) {
        m_hasDomeLight = false;
    }

    for (ccl::vector<ccl::Light*>::iterator it = m_cyclesScene->lights.begin();
         it != m_cyclesScene->lights.end();) {
        if (a_light == *it) {
            m_cyclesScene->mutex.lock();

            it = m_cyclesScene->lights.erase(it);

            m_lightsUpdated = true;

            m_cyclesScene->mutex.unlock();

            break;
        } else {
            ++it;
        }
    }

    if (m_lightsUpdated)
        Interrupt();
}

void
HdCyclesRenderParam::RemoveMesh(ccl::Mesh* a_mesh)
{
    for (ccl::vector<ccl::Geometry*>::iterator it
         = m_cyclesScene->geometry.begin();
         it != m_cyclesScene->geometry.end();) {
        if (a_mesh == *it) {
            m_cyclesScene->mutex.lock();

            it = m_cyclesScene->geometry.erase(it);

            m_meshUpdated = true;

            m_cyclesScene->mutex.unlock();

            break;
        } else {
            ++it;
        }
    }

    if (m_geometryUpdated)
        Interrupt();
}

void
HdCyclesRenderParam::RemoveCurve(ccl::Hair* a_hair)
{
    for (ccl::vector<ccl::Geometry*>::iterator it
         = m_cyclesScene->geometry.begin();
         it != m_cyclesScene->geometry.end();) {
        if (a_hair == *it) {
            m_cyclesScene->mutex.lock();

            it = m_cyclesScene->geometry.erase(it);

            m_curveUpdated = true;

            m_cyclesScene->mutex.unlock();

            break;
        } else {
            ++it;
        }
    }

    if (m_geometryUpdated)
        Interrupt();
}

void
HdCyclesRenderParam::RemoveShader(ccl::Shader* a_shader)
{
    for (ccl::vector<ccl::Shader*>::iterator it = m_cyclesScene->shaders.begin();
         it != m_cyclesScene->shaders.end();) {
        if (a_shader == *it) {
            m_cyclesScene->mutex.lock();

            it = m_cyclesScene->shaders.erase(it);

            m_shadersUpdated = true;

            m_cyclesScene->mutex.unlock();

            break;
        } else {
            ++it;
        }
    }

    if (m_shadersUpdated)
        Interrupt();
}

VtDictionary
HdCyclesRenderParam::GetRenderStats() const
{
    ccl::RenderStats stats;
    m_cyclesSession->collect_statistics(&stats);

    return {
        { "hdcycles:version", VtValue(HD_CYCLES_VERSION) },
        { "hdcycles:geometry:total_memory",
          VtValue(ccl::string_human_readable_size(stats.mesh.geometry.total_size)
                      .c_str()) },
        /*{ "hdcycles:textures:total_memory",
          VtValue(
              ccl::string_human_readable_size(stats.image.textures.total_size)
                  .c_str()) },*/
        { "hdcycles:scene:num_objects", VtValue(m_cyclesScene->objects.size()) },
        { "hdcycles:scene:num_shaders", VtValue(m_cyclesScene->shaders.size()) },
    };
}

PXR_NAMESPACE_CLOSE_SCOPE
