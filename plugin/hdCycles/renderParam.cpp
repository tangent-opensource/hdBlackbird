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
#include "renderBuffer.h"
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
#include <util/util_murmurhash.h>

#ifdef WITH_CYCLES_LOGGING
#    include <util/util_logging.h>
#endif

#include <pxr/usd/usdRender/tokens.h>
#ifdef USE_USD_CYCLES_SCHEMA
#    include <usdCycles/tokens.h>
#endif

PXR_NAMESPACE_OPEN_SCOPE

namespace {
struct HdCyclesAov {
    std::string name;
    ccl::PassType type;
    TfToken token;
    HdFormat format;
    bool filter;
};

std::array<HdCyclesAov, 26> DefaultAovs = {{
    { "Combined", ccl::PASS_COMBINED, HdAovTokens->color, HdFormatFloat32Vec4, true },
    { "Depth", ccl::PASS_DEPTH, HdAovTokens->cameraDepth, HdFormatFloat32, false },
    { "Normal", ccl::PASS_NORMAL, HdAovTokens->normal, HdFormatFloat32Vec3, false },
    { "IndexOB", ccl::PASS_OBJECT_ID, HdAovTokens->primId, HdFormatFloat32, false },
    { "IndexMA", ccl::PASS_MATERIAL_ID, HdCyclesAovTokens->IndexMA, HdFormatFloat32, false },
    { "Mist", ccl::PASS_MIST, HdAovTokens->depth, HdFormatFloat32, true },
    { "Emission", ccl::PASS_EMISSION, HdCyclesAovTokens->Emit, HdFormatFloat32Vec3, true },
    { "Shadow", ccl::PASS_SHADOW, HdCyclesAovTokens->Shadow, HdFormatFloat32Vec3, true },

    { "UV", ccl::PASS_UV, HdCyclesAovTokens->UV, HdFormatFloat32Vec3, true },
    { "Vector", ccl::PASS_MOTION, HdCyclesAovTokens->Vector, HdFormatFloat32Vec4, true },

    { "DiffDir", ccl::PASS_DIFFUSE_DIRECT, HdCyclesAovTokens->DiffDir, HdFormatFloat32Vec3, true },
    { "DiffInd", ccl::PASS_DIFFUSE_INDIRECT, HdCyclesAovTokens->DiffInd, HdFormatFloat32Vec3, true },
    { "DiffCol", ccl::PASS_DIFFUSE_COLOR, HdCyclesAovTokens->DiffCol, HdFormatFloat32Vec3, true },

    { "GlossDir", ccl::PASS_GLOSSY_DIRECT, HdCyclesAovTokens->GlossDir, HdFormatFloat32Vec3, true },
    { "GlossInd", ccl::PASS_GLOSSY_INDIRECT, HdCyclesAovTokens->GlossInd, HdFormatFloat32Vec3, true },
    { "GlossCol", ccl::PASS_GLOSSY_COLOR, HdCyclesAovTokens->GlossCol, HdFormatFloat32Vec3, true },

    { "TransDir", ccl::PASS_TRANSMISSION_DIRECT, HdCyclesAovTokens->TransDir, HdFormatFloat32Vec3, true },
    { "TransInd", ccl::PASS_TRANSMISSION_INDIRECT, HdCyclesAovTokens->TransInd, HdFormatFloat32Vec3, true },
    { "TransCol", ccl::PASS_TRANSMISSION_COLOR, HdCyclesAovTokens->TransCol, HdFormatFloat32Vec3, true },

    { "VolumeDir", ccl::PASS_VOLUME_DIRECT, HdCyclesAovTokens->VolumeDir, HdFormatFloat32Vec3, true },
    { "VolumeInd", ccl::PASS_VOLUME_INDIRECT, HdCyclesAovTokens->VolumeInd, HdFormatFloat32Vec3, true },

    { "RenderTime", ccl::PASS_RENDER_TIME, HdCyclesAovTokens->RenderTime, HdFormatFloat32, false },
    { "SampleCount", ccl::PASS_SAMPLE_COUNT, HdCyclesAovTokens->SampleCount, HdFormatFloat32, false },

    { "P", ccl::PASS_AOV_COLOR, HdCyclesAovTokens->P, HdFormatFloat32Vec3, false },
    { "Pref", ccl::PASS_AOV_COLOR, HdCyclesAovTokens->Pref, HdFormatFloat32Vec3, false },
    { "Ngn", ccl::PASS_AOV_COLOR, HdCyclesAovTokens->Ngn, HdFormatFloat32Vec3, false },
}};

std::array<HdCyclesAov, 2> CustomAovs = {{
    { "AOVC", ccl::PASS_AOV_COLOR, HdCyclesAovTokens->AOVC, HdFormatFloat32Vec3, true },
    { "AOVV", ccl::PASS_AOV_VALUE, HdCyclesAovTokens->AOVV, HdFormatFloat32, true },
}};

std::array<HdCyclesAov, 3> CryptomatteAovs = {{
    { "CryptoObject", ccl::PASS_CRYPTOMATTE, HdCyclesAovTokens->CryptoObject, HdFormatFloat32Vec4, true },
    { "CryptoMaterial", ccl::PASS_CRYPTOMATTE, HdCyclesAovTokens->CryptoMaterial, HdFormatFloat32Vec4, true },
    { "CryptoAsset", ccl::PASS_CRYPTOMATTE, HdCyclesAovTokens->CryptoAsset, HdFormatFloat32Vec4, true },
}};

// Workaround for Houdini's default color buffer naming convention (not using HdAovTokens->color)
const TfToken defaultHoudiniColor = TfToken("C.*");

TfToken GetSourceName(const HdRenderPassAovBinding &aov) {
    const auto &it = aov.aovSettings.find(UsdRenderTokens->sourceName);
    if (it != aov.aovSettings.end()) {
        if (it->second.IsHolding<std::string>()) {
            TfToken token = TfToken(it->second.UncheckedGet<std::string>());
            if (token == defaultHoudiniColor) {
                return HdAovTokens->color;
            }
            else {
                return token;
            }
        }
    }

    return TfToken();
}

bool GetCyclesAov(const HdRenderPassAovBinding &aov, HdCyclesAov &cyclesAov) {

    TfToken sourceName = GetSourceName(aov);

    for (HdCyclesAov& _cyclesAov : DefaultAovs) {
        if (sourceName == _cyclesAov.token) {
            cyclesAov = _cyclesAov;
            return true;
        }
    }
    for (HdCyclesAov& _cyclesAov : CustomAovs) {
        if (sourceName == _cyclesAov.token) {
            cyclesAov = _cyclesAov;
            return true;
        }
    }
    for (HdCyclesAov& _cyclesAov : CryptomatteAovs) {
        if (sourceName == _cyclesAov.token) {
            cyclesAov = _cyclesAov;
            return true;
        }
    }

    return false;
}

} // namespace

HdCyclesRenderParam::HdCyclesRenderParam()
    : m_renderPercent(0)
    , m_renderProgress(0.0f)
    , m_useTiledRendering(false)
    , m_objectsUpdated(false)
    , m_geometryUpdated(false)
    , m_curveUpdated(false)
    , m_meshUpdated(false)
    , m_lightsUpdated(false)
    , m_shadersUpdated(false)
    , m_shouldUpdate(false)
    , m_numDomeLights(0)
    , m_useSquareSamples(false)
    , m_cyclesSession(nullptr)
    , m_cyclesScene(nullptr)
    , m_displayAovToken(HdAovTokens->color)
{
    _InitializeDefaults();
}

void
HdCyclesRenderParam::_InitializeDefaults()
{
    // These aren't directly cycles settings, but inform the creation and behaviour
    // of a render. These should be will need to be set by schema too...
    static const HdCyclesConfig& config = HdCyclesConfig::GetInstance();
    m_deviceName                        = config.device_name.value;
    m_useSquareSamples                  = config.use_square_samples.value;
    m_useTiledRendering                 = config.use_tiled_rendering;

    m_upAxis = UpAxis::Z;
    if (config.up_axis == "Z") {
        m_upAxis = UpAxis::Z;
    } else if (config.up_axis == "Y") {
        m_upAxis = UpAxis::Y;
    }

#ifdef WITH_CYCLES_LOGGING
    if (config.cycles_enable_logging) {
        ccl::util_logging_start();
        ccl::util_logging_verbosity_set(config.cycles_logging_severity);
    }
#endif
}

float
HdCyclesRenderParam::GetProgress()
{
    return m_renderProgress;
}

bool
HdCyclesRenderParam::IsConverged()
{
    return GetProgress() >= 1.0f;
}

void
HdCyclesRenderParam::_SessionUpdateCallback()
{
    // - Get Session progress integer amount

    m_renderProgress = m_cyclesSession->progress.get_progress();

    int newPercent = (int)(floor(m_renderProgress * 100));
    if (newPercent != m_renderPercent) {
        m_renderPercent = newPercent;


        if (HdCyclesConfig::GetInstance().enable_progress) {
            std::cout << "Progress: " << m_renderPercent << "%" << std::endl
                      << std::flush;
        }
    }

    // - Get Render time

    m_cyclesSession->progress.get_time(m_totalTime, m_renderTime);

    // - Handle Session status logging

    if (HdCyclesConfig::GetInstance().enable_logging) {
        std::string status, substatus;
        m_cyclesSession->progress.get_status(status, substatus);
        if (substatus != "")
            status += ": " + substatus;

        std::cout << "cycles: " << m_renderProgress << " : " << status << '\n';
    }
}

/*
    This paradigm does cause unecessary loops through settingsMap for each feature. 
    This should be addressed in the future. For the moment, the flexibility of setting
    order of operations is more important.
*/
bool
HdCyclesRenderParam::Initialize(HdRenderSettingsMap const& settingsMap)
{
    // -- Delegate
    _UpdateDelegateFromConfig(true);
    _UpdateDelegateFromRenderSettings(settingsMap);
    _UpdateDelegateFromConfig();

    // -- Session
    _UpdateSessionFromConfig(true);
    _UpdateSessionFromRenderSettings(settingsMap);
    _UpdateSessionFromConfig();

    if (!_CreateSession()) {
        std::cout << "COULD NOT CREATE CYCLES SESSION\n";
        // Couldn't create session, big issue
        return false;
    }

    // -- Scene
    _UpdateSceneFromConfig(true);
    _UpdateSceneFromRenderSettings(settingsMap);
    _UpdateSceneFromConfig();

    if (!_CreateScene()) {
        std::cout << "COULD NOT CREATE CYCLES SCENE\n";
        // Couldn't create scene, big issue
        return false;
    }

    // -- Film
    _UpdateFilmFromConfig(true);
    _UpdateFilmFromRenderSettings(settingsMap);
    _UpdateFilmFromConfig();

    // -- Integrator
    _UpdateIntegratorFromConfig(true);
    _UpdateIntegratorFromRenderSettings(settingsMap);
    _UpdateIntegratorFromConfig();

    // -- Background
    _UpdateBackgroundFromConfig(true);
    _UpdateBackgroundFromRenderSettings(settingsMap);
    _UpdateBackgroundFromConfig();

    _HandlePasses();

    return true;
}


// -- HdCycles Misc Delegate Settings

void
HdCyclesRenderParam::_UpdateDelegateFromConfig(bool a_forceInit)
{
    static const HdCyclesConfig& config = HdCyclesConfig::GetInstance();
    (void) config;
}

void
HdCyclesRenderParam::_UpdateDelegateFromRenderSettings(
    HdRenderSettingsMap const& settingsMap)
{
    for (auto& entry : settingsMap) {
        TfToken key   = entry.first;
        VtValue value = entry.second;
        _HandleDelegateRenderSetting(key, value);
    }
}

bool
HdCyclesRenderParam::_HandleDelegateRenderSetting(const TfToken& key,
                                                  const VtValue& value)
{
#ifdef USE_USD_CYCLES_SCHEMA

    bool delegate_updated = false;

    if (key == usdCyclesTokens->cyclesUse_square_samples) {
        m_useSquareSamples = _HdCyclesGetVtValue<bool>(value,
                                                       m_useSquareSamples,
                                                       &delegate_updated);
    }

    if (delegate_updated) {
        // Although this is called, it does not correctly reset session in IPR
        //Interrupt();
        return true;
    }

#endif

    return false;
}

// -- Session

void
HdCyclesRenderParam::_UpdateSessionFromConfig(bool a_forceInit)
{
    static const HdCyclesConfig& config = HdCyclesConfig::GetInstance();

    ccl::SessionParams* sessionParams = &m_sessionParams;

    if (m_cyclesSession)
        sessionParams = &m_cyclesSession->params;

    config.enable_experimental.eval(sessionParams->experimental, a_forceInit);

    config.display_buffer_linear.eval(sessionParams->display_buffer_linear,
                                      a_forceInit);

    sessionParams->shadingsystem = ccl::SHADINGSYSTEM_SVM;
    if (config.shading_system.value == "OSL"
        || config.shading_system.value == "SHADINGSYSTEM_OSL")
        sessionParams->shadingsystem = ccl::SHADINGSYSTEM_OSL;

    sessionParams->background = false;

    config.start_resolution.eval(sessionParams->start_resolution, a_forceInit);

    sessionParams->progressive                = true;
    sessionParams->progressive_refine         = false;
    sessionParams->progressive_update_timeout = 0.1;

    config.pixel_size.eval(sessionParams->pixel_size, a_forceInit);
    config.tile_size_x.eval(sessionParams->tile_size.x, a_forceInit);
    config.tile_size_y.eval(sessionParams->tile_size.y, a_forceInit);

    // Tiled rendering requires some settings to be forced on...
    // This requires some more thought and testing in regards
    // to the usdCycles schema...
    if (m_useTiledRendering) {
        sessionParams->background         = true;
        sessionParams->start_resolution   = INT_MAX;
        sessionParams->progressive        = false;
        sessionParams->progressive_refine = false;
    }

    config.max_samples.eval(sessionParams->samples, a_forceInit);
}

void
HdCyclesRenderParam::_UpdateSessionFromRenderSettings(
    HdRenderSettingsMap const& settingsMap)
{
    for (auto& entry : settingsMap) {
        TfToken key   = entry.first;
        VtValue value = entry.second;
        _HandleSessionRenderSetting(key, value);
    }
}

bool
HdCyclesRenderParam::_HandleSessionRenderSetting(const TfToken& key,
                                                 const VtValue& value)
{
#ifdef USE_USD_CYCLES_SCHEMA

    ccl::SessionParams* sessionParams = &m_sessionParams;

    if (m_cyclesSession)
        sessionParams = &m_cyclesSession->params;

    bool session_updated = false;
    bool samples_updated = false;

    // This is now handled by HdCycles depending on tiled or not tiled rendering...
    /*if (key == usdCyclesTokens->cyclesBackground) {
        sessionParams->background
            = _HdCyclesGetVtValue<bool>(value, sessionParams->background,
                                        &session_updated);
    }*/

    if (key == usdCyclesTokens->cyclesProgressive_refine) {
        sessionParams->progressive_refine = _HdCyclesGetVtValue<bool>(
            value, sessionParams->progressive_refine, &session_updated);
    }

    if (key == usdCyclesTokens->cyclesProgressive) {
        sessionParams->progressive
            = _HdCyclesGetVtValue<bool>(value, sessionParams->progressive,
                                        &session_updated);
    }

    if (key == usdCyclesTokens->cyclesProgressive_update_timeout) {
        sessionParams->progressive_update_timeout = _HdCyclesGetVtValue<float>(
            value, sessionParams->progressive_update_timeout, &session_updated);
    }

    if (key == usdCyclesTokens->cyclesExperimental) {
        sessionParams->experimental
            = _HdCyclesGetVtValue<bool>(value, sessionParams->experimental,
                                        &session_updated);
    }

    if (key == usdCyclesTokens->cyclesSamples) {
        // If branched-path mode is set, make sure to set samples to use the
        // aa_samples instead from the integrator.
        int samples = sessionParams->samples;
        int aa_samples = 0;
        ccl::Integrator::Method method = ccl::Integrator::PATH;

        if (m_cyclesScene) {
            method = m_cyclesScene->integrator->method;
            aa_samples = m_cyclesScene->integrator->aa_samples;

            // Don't apply aaSamples if it is 0
            if (aa_samples && method == ccl::Integrator::BRANCHED_PATH) {
                samples = aa_samples;
            }
        }

        sessionParams->samples
            = _HdCyclesGetVtValue<int>(value, samples,
                                       &samples_updated);
        if (samples_updated) {
            session_updated = true;

            if (m_cyclesScene && aa_samples && 
                method == ccl::Integrator::BRANCHED_PATH) {
                sessionParams->samples = aa_samples;
            }
        }
    }

    // Tiles

    if (key == usdCyclesTokens->cyclesTile_size) {
        sessionParams->tile_size = vec2i_to_int2(_HdCyclesGetVtValue<GfVec2i>(
            value, int2_to_vec2i(sessionParams->tile_size), &session_updated));
    }

    TfToken tileOrder;
    if (key == usdCyclesTokens->cyclesTile_order) {
        tileOrder = _HdCyclesGetVtValue<TfToken>(value, tileOrder,
                                                 &session_updated);

        if (tileOrder == usdCyclesTokens->hilbert_spiral) {
            sessionParams->tile_order = ccl::TILE_HILBERT_SPIRAL;
        } else if (tileOrder == usdCyclesTokens->center) {
            sessionParams->tile_order = ccl::TILE_CENTER;
        } else if (tileOrder == usdCyclesTokens->right_to_left) {
            sessionParams->tile_order = ccl::TILE_RIGHT_TO_LEFT;
        } else if (tileOrder == usdCyclesTokens->left_to_right) {
            sessionParams->tile_order = ccl::TILE_LEFT_TO_RIGHT;
        } else if (tileOrder == usdCyclesTokens->top_to_bottom) {
            sessionParams->tile_order = ccl::TILE_TOP_TO_BOTTOM;
        } else if (tileOrder == usdCyclesTokens->bottom_to_top) {
            sessionParams->tile_order = ccl::TILE_BOTTOM_TO_TOP;
        }
    }

    if (key == usdCyclesTokens->cyclesStart_resolution) {
        sessionParams->start_resolution
            = _HdCyclesGetVtValue<int>(value, sessionParams->start_resolution,
                                       &session_updated);
    }

    if (key == usdCyclesTokens->cyclesPixel_size) {
        sessionParams->pixel_size
            = _HdCyclesGetVtValue<int>(value, sessionParams->pixel_size,
                                       &session_updated);
    }

    if (key == usdCyclesTokens->cyclesThreads) {
        sessionParams->threads
            = _HdCyclesGetVtValue<int>(value, sessionParams->threads,
                                       &session_updated);
    }

    if (key == usdCyclesTokens->cyclesAdaptive_sampling) {
        sessionParams->adaptive_sampling
            = _HdCyclesGetVtValue<bool>(value, sessionParams->adaptive_sampling,
                                        &session_updated);
    }

    if (key == usdCyclesTokens->cyclesUse_profiling) {
        sessionParams->use_profiling
            = _HdCyclesGetVtValue<bool>(value, sessionParams->use_profiling,
                                        &session_updated);
    }

    if (key == usdCyclesTokens->cyclesDisplay_buffer_linear) {
        sessionParams->display_buffer_linear = _HdCyclesGetVtValue<bool>(
            value, sessionParams->display_buffer_linear, &session_updated);
    }

    //DenoiseParams denoising;

    TfToken shadingSystem;
    if (key == usdCyclesTokens->cyclesShading_system) {
        shadingSystem = _HdCyclesGetVtValue<TfToken>(value, shadingSystem,
                                                     &session_updated);

        if (shadingSystem == usdCyclesTokens->osl) {
            sessionParams->shadingsystem = ccl::SHADINGSYSTEM_OSL;
        } else {
            sessionParams->shadingsystem = ccl::SHADINGSYSTEM_SVM;
        }
    }

    if (key == usdCyclesTokens->cyclesUse_profiling) {
        sessionParams->use_profiling
            = _HdCyclesGetVtValue<bool>(value, sessionParams->use_profiling,
                                        &session_updated);
    }

    // Session BVH


    // Denoising

    bool denoising_updated = false;
    ccl::DenoiseParams denoisingParams;

    if (key == usdCyclesTokens->cyclesRun_denoising) {
        denoisingParams.use = _HdCyclesGetVtValue<int>(value,
                                                       denoisingParams.use,
                                                       &denoising_updated);
    }

    if (key == usdCyclesTokens->cyclesDenoising_start_sample) {
        sessionParams->denoising_start_sample = _HdCyclesGetVtValue<int>(
            value, sessionParams->denoising_start_sample, &denoising_updated);
    }

    if (denoising_updated) {
        sessionParams->denoising = denoisingParams;
        session_updated          = true;
    }

    // Final

    if (session_updated) {
        // Although this is called, it does not correctly reset session in IPR
        //Interrupt();
        return true;
    }

#endif
    return false;
}

// -- Scene

void
HdCyclesRenderParam::_UpdateSceneFromConfig(bool a_forceInit)
{
    static const HdCyclesConfig& config = HdCyclesConfig::GetInstance();

    ccl::SceneParams* sceneParams = &m_sceneParams;

    if (m_cyclesScene)
        sceneParams = &m_cyclesScene->params;

    ccl::SessionParams* sessionParams = &m_sessionParams;

    if (m_cyclesSession)
        sessionParams = &m_cyclesSession->params;

    // -- Scene init
    sceneParams->shadingsystem = sessionParams->shadingsystem;

    sceneParams->bvh_type = ccl::SceneParams::BVH_DYNAMIC;
    if (config.bvh_type.value == "STATIC")
        sceneParams->bvh_type = ccl::SceneParams::BVH_STATIC;

    sceneParams->bvh_layout = ccl::BVH_LAYOUT_EMBREE;

    sceneParams->persistent_data = true;

    config.curve_subdivisions.eval(sceneParams->hair_subdivisions, a_forceInit);
}

void
HdCyclesRenderParam::_UpdateSceneFromRenderSettings(
    HdRenderSettingsMap const& settingsMap)
{
    for (auto& entry : settingsMap) {
        TfToken key   = entry.first;
        VtValue value = entry.second;
        _HandleSceneRenderSetting(key, value);
    }
}

bool
HdCyclesRenderParam::_HandleSceneRenderSetting(const TfToken& key,
                                               const VtValue& value)
{
#ifdef USE_USD_CYCLES_SCHEMA
    // -- Scene

    ccl::SceneParams* sceneParams = &m_sceneParams;

    if (m_cyclesScene)
        sceneParams = &m_cyclesScene->params;

    bool scene_updated = false;

    if (key == usdCyclesTokens->cyclesShading_system) {
        TfToken shading_system
            = _HdCyclesGetVtValue<TfToken>(value, usdCyclesTokens->svm,
                                           &scene_updated);
        if (shading_system == usdCyclesTokens->svm) {
            sceneParams->shadingsystem = ccl::SHADINGSYSTEM_SVM;
        } else if (shading_system == usdCyclesTokens->osl) {
            sceneParams->shadingsystem = ccl::SHADINGSYSTEM_OSL;
        }
    }

    if (key == usdCyclesTokens->cyclesBvh_type) {
        TfToken bvh_type
            = _HdCyclesGetVtValue<TfToken>(value, usdCyclesTokens->bvh_dynamic,
                                           &scene_updated);
        if (bvh_type == usdCyclesTokens->bvh_dynamic) {
            sceneParams->bvh_type = ccl::SceneParams::BVH_DYNAMIC;
        } else if (bvh_type == usdCyclesTokens->bvh_static) {
            sceneParams->bvh_type = ccl::SceneParams::BVH_STATIC;
        }
    }

    if (key == usdCyclesTokens->cyclesCurve_subdivisions) {
        sceneParams->hair_subdivisions
            = _HdCyclesGetVtValue<int>(value, sceneParams->hair_subdivisions,
                                       &scene_updated);
    }

    // TODO: Unsure how we will handle this if the camera hasn't been created yet/at all...
    /*if (key == usdCyclesTokens->cyclesDicing_camera) {
        scene->dicing_camera
            = _HdCyclesGetVtValue<std::string>(value, scene->dicing_camera,
                                       &scene_updated);
    }*/


    if (key == usdCyclesTokens->cyclesUse_bvh_spatial_split) {
        sceneParams->use_bvh_spatial_split = _HdCyclesGetVtValue<bool>(
            value, sceneParams->use_bvh_spatial_split, &scene_updated);
    }

    if (key == usdCyclesTokens->cyclesUse_bvh_unaligned_nodes) {
        sceneParams->use_bvh_unaligned_nodes = _HdCyclesGetVtValue<bool>(
            value, sceneParams->use_bvh_unaligned_nodes, &scene_updated);
    }

    if (key == usdCyclesTokens->cyclesNum_bvh_time_steps) {
        sceneParams->num_bvh_time_steps
            = _HdCyclesGetVtValue<int>(value, sceneParams->num_bvh_time_steps,
                                       &scene_updated);
    }

    if (scene_updated) {
        // Although this is called, it does not correctly reset session in IPR
        if (m_cyclesSession && m_cyclesScene)
            Interrupt(true);
        return true;
    }

#endif
    return false;
}

// -- Config

void
HdCyclesRenderParam::_UpdateIntegratorFromConfig(bool a_forceInit)
{
    if (!m_cyclesScene)
        return;

    static const HdCyclesConfig& config = HdCyclesConfig::GetInstance();

    ccl::Integrator* integrator = m_cyclesScene->integrator;

    if (config.integrator_method.value == "PATH") {
        integrator->method = ccl::Integrator::PATH;
    } else {
        integrator->method = ccl::Integrator::BRANCHED_PATH;
    }

    // Samples

    if (config.diffuse_samples.eval(integrator->diffuse_samples, a_forceInit)
        && m_useSquareSamples) {
        integrator->diffuse_samples = integrator->diffuse_samples
                                      * integrator->diffuse_samples;
    }
    if (config.glossy_samples.eval(integrator->glossy_samples, a_forceInit)
        && m_useSquareSamples) {
        integrator->glossy_samples = integrator->glossy_samples
                                     * integrator->glossy_samples;
    }
    if (config.transmission_samples.eval(integrator->transmission_samples,
                                         a_forceInit)
        && m_useSquareSamples) {
        integrator->transmission_samples = integrator->transmission_samples
                                           * integrator->transmission_samples;
    }
    if (config.ao_samples.eval(integrator->ao_samples, a_forceInit)
        && m_useSquareSamples) {
        integrator->ao_samples = integrator->ao_samples
                                 * integrator->ao_samples;
    }
    if (config.mesh_light_samples.eval(integrator->mesh_light_samples,
                                       a_forceInit)
        && m_useSquareSamples) {
        integrator->mesh_light_samples = integrator->mesh_light_samples
                                         * integrator->mesh_light_samples;
    }
    if (config.subsurface_samples.eval(integrator->subsurface_samples,
                                       a_forceInit)
        && m_useSquareSamples) {
        integrator->subsurface_samples = integrator->subsurface_samples
                                         * integrator->subsurface_samples;
    }
    if (config.volume_samples.eval(integrator->volume_samples, a_forceInit)
        && m_useSquareSamples) {
        integrator->volume_samples = integrator->volume_samples
                                     * integrator->volume_samples;
    }
    /*if (config.adaptive_min_samples.eval(integrator->adaptive_min_samples)
        && m_useSquareSamples) {
        integrator->adaptive_min_samples
            = std::min(integrator->adaptive_min_samples
                           * integrator->adaptive_min_samples,
                       INT_MAX);
    }*/

    config.enable_motion_blur.eval(integrator->motion_blur, a_forceInit);

    integrator->tag_update(m_cyclesScene);
}

void
HdCyclesRenderParam::_UpdateIntegratorFromRenderSettings(
    HdRenderSettingsMap const& settingsMap)
{
    for (auto& entry : settingsMap) {
        TfToken key   = entry.first;
        VtValue value = entry.second;

        _HandleIntegratorRenderSetting(key, value);
    }
}

bool
HdCyclesRenderParam::_HandleIntegratorRenderSetting(const TfToken& key,
                                                    const VtValue& value)
{
#ifdef USE_USD_CYCLES_SCHEMA

    // -- Integrator Settings

    ccl::Integrator* integrator = m_cyclesScene->integrator;
    bool integrator_updated     = false;
    bool method_updated         = false;

    if (key == usdCyclesTokens->cyclesIntegratorSeed) {
        integrator->seed = _HdCyclesGetVtValue<int>(value, integrator->seed,
                                                    &integrator_updated);
    }

    if (key == usdCyclesTokens->cyclesIntegratorMin_bounce) {
        integrator->min_bounce
            = _HdCyclesGetVtValue<int>(value, integrator->min_bounce,
                                       &integrator_updated);
    }

    if (key == usdCyclesTokens->cyclesIntegratorMax_bounce) {
        integrator->max_bounce
            = _HdCyclesGetVtValue<int>(value, integrator->max_bounce,
                                       &integrator_updated);
    }

    if (key == usdCyclesTokens->cyclesIntegratorMethod) {
        TfToken integratorMethod
            = _HdCyclesGetVtValue<TfToken>(value, usdCyclesTokens->path,
                                           &method_updated);
        if (integratorMethod == usdCyclesTokens->path) {
            integrator->method = ccl::Integrator::PATH;
        } else {
            integrator->method = ccl::Integrator::BRANCHED_PATH;
        }

        if (method_updated) {
            integrator_updated = true;
            if (integrator->aa_samples && 
                integrator->method == ccl::Integrator::BRANCHED_PATH) {
                m_cyclesSession->params.samples = integrator->aa_samples;
            }
        }
    }

    if (key == usdCyclesTokens->cyclesIntegratorSampling_method) {
        TfToken defaultPattern = usdCyclesTokens->sobol;
        if (integrator->sampling_pattern == ccl::SAMPLING_PATTERN_CMJ) {
            defaultPattern = usdCyclesTokens->cmj;
        }
        else if(integrator->sampling_pattern == ccl::SAMPLING_PATTERN_PMJ) {
            defaultPattern = usdCyclesTokens->pmj;
        }

        TfToken samplingMethod
            = _HdCyclesGetVtValue<TfToken>(value, defaultPattern,
                                           &integrator_updated);
        if (samplingMethod == usdCyclesTokens->sobol) {
            integrator->sampling_pattern = ccl::SAMPLING_PATTERN_SOBOL;
        } else if (samplingMethod == usdCyclesTokens->cmj) {
            integrator->sampling_pattern = ccl::SAMPLING_PATTERN_CMJ;
        } else {
            integrator->sampling_pattern = ccl::SAMPLING_PATTERN_PMJ;
        }

        // Adaptive sampling must use PMJ
        if (m_cyclesSession->params.adaptive_sampling && 
            integrator->sampling_pattern != ccl::SAMPLING_PATTERN_PMJ) {
            integrator_updated = true;
            integrator->sampling_pattern = ccl::SAMPLING_PATTERN_PMJ;
        }
    }

    if (key == usdCyclesTokens->cyclesIntegratorMax_diffuse_bounce) {
        integrator->max_diffuse_bounce
            = _HdCyclesGetVtValue<int>(value, integrator->max_diffuse_bounce,
                                       &integrator_updated);
    }

    if (key == usdCyclesTokens->cyclesIntegratorMax_glossy_bounce) {
        integrator->max_glossy_bounce
            = _HdCyclesGetVtValue<int>(value, integrator->max_glossy_bounce,
                                       &integrator_updated);
    }
    if (key == usdCyclesTokens->cyclesIntegratorMax_transmission_bounce) {
        integrator->max_transmission_bounce = _HdCyclesGetVtValue<int>(
            value, integrator->max_transmission_bounce, &integrator_updated);
    }

    if (key == usdCyclesTokens->cyclesIntegratorMax_volume_bounce) {
        integrator->max_volume_bounce
            = _HdCyclesGetVtValue<int>(value, integrator->max_volume_bounce,
                                       &integrator_updated);
    }
    if (key == usdCyclesTokens->cyclesIntegratorTransparent_min_bounce) {
        integrator->transparent_min_bounce = _HdCyclesGetVtValue<int>(
            value, integrator->transparent_min_bounce, &integrator_updated);
    }

    if (key == usdCyclesTokens->cyclesIntegratorTransparent_max_bounce) {
        integrator->transparent_max_bounce = _HdCyclesGetVtValue<int>(
            value, integrator->transparent_max_bounce, &integrator_updated);
    }

    if (key == usdCyclesTokens->cyclesIntegratorAo_bounces) {
        integrator->ao_bounces
            = _HdCyclesGetVtValue<int>(value, integrator->ao_bounces,
                                       &integrator_updated);
    }

    if (key == usdCyclesTokens->cyclesIntegratorVolume_max_steps) {
        integrator->volume_max_steps
            = _HdCyclesGetVtValue<int>(value, integrator->volume_max_steps,
                                       &integrator_updated);
    }

    if (key == usdCyclesTokens->cyclesIntegratorVolume_step_size) {
        integrator->volume_step_rate
            = _HdCyclesGetVtValue<float>(value, integrator->volume_step_rate,
                                         &integrator_updated);
    }

    if (key == usdCyclesTokens->cyclesIntegratorAdaptive_threshold) {
        integrator->adaptive_threshold
            = _HdCyclesGetVtValue<float>(value, integrator->adaptive_threshold,
                                         &integrator_updated);
    }

    // Samples

    if (key == usdCyclesTokens->cyclesIntegratorAa_samples) {
        bool sample_updated = false;
        integrator->aa_samples
            = _HdCyclesGetVtValue<int>(value, integrator->aa_samples,
                                       &sample_updated);

        if (sample_updated) {
            if (m_useSquareSamples) {
                integrator->aa_samples = integrator->aa_samples
                                         * integrator->aa_samples;
            }
            if (integrator->aa_samples &&
                integrator->method == ccl::Integrator::BRANCHED_PATH) {
                m_cyclesSession->params.samples = integrator->aa_samples;
            }
            integrator_updated = true;
        }
    }

    if (key == usdCyclesTokens->cyclesIntegratorAdaptive_min_samples) {
        bool sample_updated = false;
        integrator->adaptive_min_samples
            = _HdCyclesGetVtValue<int>(value, integrator->adaptive_min_samples,
                                       &sample_updated);

        if (sample_updated) {
            if (m_useSquareSamples) {
                integrator->adaptive_min_samples
                    = std::min(integrator->adaptive_min_samples
                                   * integrator->adaptive_min_samples,
                               INT_MAX);
            }
            integrator_updated = true;
        }
    }

    if (key == usdCyclesTokens->cyclesIntegratorDiffuse_samples) {
        bool sample_updated = false;
        integrator->diffuse_samples
            = _HdCyclesGetVtValue<int>(value, integrator->diffuse_samples,
                                       &sample_updated);

        if (sample_updated) {
            if (m_useSquareSamples) {
                integrator->diffuse_samples = integrator->diffuse_samples
                                              * integrator->diffuse_samples;
            }
            integrator_updated = true;
        }
    }

    if (key == usdCyclesTokens->cyclesIntegratorGlossy_samples) {
        bool sample_updated = false;
        integrator->glossy_samples
            = _HdCyclesGetVtValue<int>(value, integrator->glossy_samples,
                                       &sample_updated);

        if (sample_updated) {
            if (m_useSquareSamples) {
                integrator->glossy_samples = integrator->glossy_samples
                                             * integrator->glossy_samples;
            }
            integrator_updated = true;
        }
    }

    if (key == usdCyclesTokens->cyclesIntegratorTransmission_samples) {
        bool sample_updated = false;
        integrator->transmission_samples
            = _HdCyclesGetVtValue<int>(value, integrator->transmission_samples,
                                       &sample_updated);

        if (sample_updated) {
            if (m_useSquareSamples) {
                integrator->transmission_samples
                    = integrator->transmission_samples
                      * integrator->transmission_samples;
            }
            integrator_updated = true;
        }
    }

    if (key == usdCyclesTokens->cyclesIntegratorAo_samples) {
        bool sample_updated = false;
        integrator->ao_samples
            = _HdCyclesGetVtValue<int>(value, integrator->ao_samples,
                                       &sample_updated);

        if (sample_updated) {
            if (m_useSquareSamples) {
                integrator->ao_samples = integrator->ao_samples
                                         * integrator->ao_samples;
            }
            integrator_updated = true;
        }
    }

    if (key == usdCyclesTokens->cyclesIntegratorMesh_light_samples) {
        bool sample_updated = false;
        integrator->mesh_light_samples
            = _HdCyclesGetVtValue<int>(value, integrator->mesh_light_samples,
                                       &sample_updated);

        if (sample_updated) {
            if (m_useSquareSamples) {
                integrator->mesh_light_samples
                    = integrator->mesh_light_samples
                      * integrator->mesh_light_samples;
            }
            integrator_updated = true;
        }
    }

    if (key == usdCyclesTokens->cyclesIntegratorSubsurface_samples) {
        bool sample_updated = false;
        integrator->subsurface_samples
            = _HdCyclesGetVtValue<int>(value, integrator->subsurface_samples,
                                       &sample_updated);

        if (sample_updated) {
            if (m_useSquareSamples) {
                integrator->subsurface_samples
                    = integrator->subsurface_samples
                      * integrator->subsurface_samples;
            }
            integrator_updated = true;
        }
    }

    if (key == usdCyclesTokens->cyclesIntegratorVolume_samples) {
        bool sample_updated = false;
        integrator->volume_samples
            = _HdCyclesGetVtValue<int>(value, integrator->volume_samples,
                                       &sample_updated);

        if (sample_updated) {
            if (m_useSquareSamples) {
                integrator->volume_samples = integrator->volume_samples
                                             * integrator->volume_samples;
            }
            integrator_updated = true;
        }
    }

    if (key == usdCyclesTokens->cyclesIntegratorStart_sample) {
        integrator->start_sample
            = _HdCyclesGetVtValue<int>(value, integrator->start_sample,
                                       &integrator_updated);
    }

    // Caustics

    if (key == usdCyclesTokens->cyclesIntegratorCaustics_reflective) {
        integrator->caustics_reflective
            = _HdCyclesGetVtValue<bool>(value, integrator->caustics_reflective,
                                        &integrator_updated);
    }
    if (key == usdCyclesTokens->cyclesIntegratorCaustics_refractive) {
        integrator->caustics_refractive
            = _HdCyclesGetVtValue<bool>(value, integrator->caustics_refractive,
                                        &integrator_updated);
    }

    // Filter

    if (key == usdCyclesTokens->cyclesIntegratorFilter_glossy) {
        integrator->filter_glossy
            = _HdCyclesGetVtValue<float>(value, integrator->filter_glossy,
                                         &integrator_updated);
    }

    if (key == usdCyclesTokens->cyclesIntegratorSample_clamp_direct) {
        integrator->sample_clamp_direct
            = _HdCyclesGetVtValue<float>(value, integrator->sample_clamp_direct,
                                         &integrator_updated);
    }
    if (key == usdCyclesTokens->cyclesIntegratorSample_clamp_indirect) {
        integrator->sample_clamp_indirect = _HdCyclesGetVtValue<float>(
            value, integrator->sample_clamp_indirect, &integrator_updated);
    }

    if (key == usdCyclesTokens->cyclesIntegratorMotion_blur) {
        integrator->motion_blur
            = _HdCyclesGetVtValue<bool>(value, integrator->motion_blur,
                                        &integrator_updated);
    }

    if (key == usdCyclesTokens->cyclesIntegratorSample_all_lights_direct) {
        integrator->sample_all_lights_direct = _HdCyclesGetVtValue<bool>(
            value, integrator->sample_all_lights_direct, &integrator_updated);
    }
    if (key == usdCyclesTokens->cyclesIntegratorSample_all_lights_indirect) {
        integrator->sample_all_lights_indirect = _HdCyclesGetVtValue<bool>(
            value, integrator->sample_all_lights_indirect, &integrator_updated);
    }

    if (key == usdCyclesTokens->cyclesIntegratorLight_sampling_threshold) {
        integrator->light_sampling_threshold = _HdCyclesGetVtValue<float>(
            value, integrator->light_sampling_threshold, &integrator_updated);
    }

    if (integrator_updated) {
        integrator->tag_update(m_cyclesScene);
        return true;
    }

#endif
    return false;
}

// -- Film

void
HdCyclesRenderParam::_UpdateFilmFromConfig(bool a_forceInit)
{
    if (!m_cyclesScene)
        return;

    static const HdCyclesConfig& config = HdCyclesConfig::GetInstance();

    ccl::Film* film = m_cyclesScene->film;

    config.exposure.eval(film->exposure, a_forceInit);

    film->tag_update(m_cyclesScene);
}

void
HdCyclesRenderParam::_UpdateFilmFromRenderSettings(
    HdRenderSettingsMap const& settingsMap)
{
    for (auto& entry : settingsMap) {
        TfToken key   = entry.first;
        VtValue value = entry.second;

        _HandleFilmRenderSetting(key, value);
    }
}

bool
HdCyclesRenderParam::_HandleFilmRenderSetting(const TfToken& key,
                                              const VtValue& value)
{
#ifdef USE_USD_CYCLES_SCHEMA
    // -- Film Settings

    ccl::Film* film   = m_cyclesScene->film;
    bool film_updated = false;

    if (key == usdCyclesTokens->cyclesFilmExposure) {
        film->exposure = _HdCyclesGetVtValue<float>(value, film->exposure,
                                                    &film_updated, false);
    }

    if (key == usdCyclesTokens->cyclesFilmPass_alpha_threshold) {
        film->pass_alpha_threshold
            = _HdCyclesGetVtValue<float>(value, film->pass_alpha_threshold,
                                         &film_updated, false);
    }

    // Filter

    if (key == usdCyclesTokens->cyclesFilmFilter_type) {
        TfToken filter = _HdCyclesGetVtValue<TfToken>(value,
                                                      usdCyclesTokens->box,
                                                      &film_updated);
        if (filter == usdCyclesTokens->box) {
            film->filter_type = ccl::FilterType::FILTER_BOX;
        } else if (filter == usdCyclesTokens->gaussian) {
            film->filter_type = ccl::FilterType::FILTER_GAUSSIAN;
        } else {
            film->filter_type = ccl::FilterType::FILTER_BLACKMAN_HARRIS;
        }
    }

    if (key == usdCyclesTokens->cyclesFilmFilter_width) {
        film->filter_width = _HdCyclesGetVtValue<float>(value,
                                                        film->filter_width,
                                                        &film_updated, false);
    }

    // Mist

    if (key == usdCyclesTokens->cyclesFilmMist_start) {
        film->mist_start = _HdCyclesGetVtValue<float>(value, film->mist_start,
                                                      &film_updated, false);
    }

    if (key == usdCyclesTokens->cyclesFilmMist_depth) {
        film->mist_depth = _HdCyclesGetVtValue<float>(value, film->mist_depth,
                                                      &film_updated, false);
    }

    if (key == usdCyclesTokens->cyclesFilmMist_falloff) {
        film->mist_falloff = _HdCyclesGetVtValue<float>(value,
                                                        film->mist_falloff,
                                                        &film_updated, false);
    }

    // Light

    if (key == usdCyclesTokens->cyclesFilmUse_light_visibility) {
        film->use_light_visibility
            = _HdCyclesGetVtValue<bool>(value, film->use_light_visibility,
                                        &film_updated, false);
    }

    // Sampling

    // TODO: Check if cycles actually uses this, doesnt appear to...
    if (key == usdCyclesTokens->cyclesFilmUse_adaptive_sampling) {
        film->use_adaptive_sampling
            = _HdCyclesGetVtValue<bool>(value, film->use_adaptive_sampling,
                                        &film_updated, false);
    }

    if (key == usdCyclesTokens->cyclesFilmCryptomatte_depth) {
        int cryptomatte_depth = _HdCyclesGetVtValue<int>(value, 4, 
                                                         &film_updated, false);
        film->cryptomatte_depth = ccl::divide_up(ccl::min(16, cryptomatte_depth), 2);
    }

    if (film_updated) {
        film->tag_update(m_cyclesScene);
        return true;
    }

#endif
    return false;
}

void
HdCyclesRenderParam::_UpdateBackgroundFromConfig(bool a_forceInit)
{
    if (!m_cyclesScene)
        return;

    static const HdCyclesConfig& config = HdCyclesConfig::GetInstance();

    ccl::Background* background = m_cyclesScene->background;

    if (config.enable_transparent_background.value)
        background->transparent = true;


    background->tag_update(m_cyclesScene);
}

void
HdCyclesRenderParam::_UpdateBackgroundFromRenderSettings(
    HdRenderSettingsMap const& settingsMap)
{
    for (auto& entry : settingsMap) {
        TfToken key   = entry.first;
        VtValue value = entry.second;
        _HandleBackgroundRenderSetting(key, value);
    }
}

bool
HdCyclesRenderParam::_HandleBackgroundRenderSetting(const TfToken& key,
                                                    const VtValue& value)
{
#ifdef USE_USD_CYCLES_SCHEMA

    // -- Background Settings

    ccl::Background* background = m_cyclesScene->background;
    bool background_updated     = false;

    if (key == usdCyclesTokens->cyclesBackgroundAo_factor) {
        background->ao_factor
            = _HdCyclesGetVtValue<float>(value, background->ao_factor,
                                         &background_updated);
    }
    if (key == usdCyclesTokens->cyclesBackgroundAo_distance) {
        background->ao_distance
            = _HdCyclesGetVtValue<float>(value, background->ao_distance,
                                         &background_updated);
    }

    if (key == usdCyclesTokens->cyclesBackgroundUse_shader) {
        background->use_shader
            = _HdCyclesGetVtValue<bool>(value, background->use_shader,
                                        &background_updated);
    }
    if (key == usdCyclesTokens->cyclesBackgroundUse_ao) {
        background->use_ao = _HdCyclesGetVtValue<bool>(value,
                                                       background->use_ao,
                                                       &background_updated);
    }

    // Visibility

    bool visCamera, visDiffuse, visGlossy, visTransmission, visScatter;
    visCamera = visDiffuse = visGlossy = visTransmission = visScatter = true;

    unsigned int visFlags = 0;

    if (key == usdCyclesTokens->cyclesBackgroundVisibilityCamera) {
        visCamera = _HdCyclesGetVtValue<bool>(value, visCamera,
                                              &background_updated);
    }

    if (key == usdCyclesTokens->cyclesBackgroundVisibilityDiffuse) {
        visDiffuse = _HdCyclesGetVtValue<bool>(value, visDiffuse,
                                               &background_updated);
    }

    if (key == usdCyclesTokens->cyclesBackgroundVisibilityGlossy) {
        visGlossy = _HdCyclesGetVtValue<bool>(value, visGlossy,
                                              &background_updated);
    }

    if (key == usdCyclesTokens->cyclesBackgroundVisibilityTransmission) {
        visTransmission = _HdCyclesGetVtValue<bool>(value, visTransmission,
                                                    &background_updated);
    }

    if (key == usdCyclesTokens->cyclesBackgroundVisibilityScatter) {
        visScatter = _HdCyclesGetVtValue<bool>(value, visScatter,
                                               &background_updated);
    }

    visFlags |= visCamera ? ccl::PATH_RAY_CAMERA : 0;
    visFlags |= visDiffuse ? ccl::PATH_RAY_DIFFUSE : 0;
    visFlags |= visGlossy ? ccl::PATH_RAY_GLOSSY : 0;
    visFlags |= visTransmission ? ccl::PATH_RAY_TRANSMIT : 0;
    visFlags |= visScatter ? ccl::PATH_RAY_VOLUME_SCATTER : 0;

    background->visibility = visFlags;

    // Glass

    if (key == usdCyclesTokens->cyclesBackgroundTransparent) {
        background->transparent
            = _HdCyclesGetVtValue<bool>(value, background->transparent,
                                        &background_updated);
    }

    if (key == usdCyclesTokens->cyclesBackgroundTransparent_glass) {
        background->transparent_glass
            = _HdCyclesGetVtValue<bool>(value, background->transparent_glass,
                                        &background_updated);
    }

    if (key
        == usdCyclesTokens->cyclesBackgroundTransparent_roughness_threshold) {
        background->transparent_roughness_threshold = _HdCyclesGetVtValue<float>(
            value, background->transparent_roughness_threshold,
            &background_updated);
    }

    // Volume

    if (key == usdCyclesTokens->cyclesBackgroundVolume_step_size) {
        background->volume_step_size
            = _HdCyclesGetVtValue<float>(value, background->volume_step_size,
                                         &background_updated);
    }

    if (background_updated) {
        background->tag_update(m_cyclesScene);
        return true;
    }

#endif
    return false;
}

void
HdCyclesRenderParam::_HandlePasses()
{
    // TODO: These might need to live elsewhere when we fully implement aovs/passes
    m_bufferParams.passes.clear();

    ccl::Pass::add(ccl::PASS_COMBINED, m_bufferParams.passes, "Combined");

    m_cyclesScene->film->tag_passes_update(m_cyclesScene,
                                           m_bufferParams.passes);
}

bool
HdCyclesRenderParam::SetRenderSetting(const TfToken& key, const VtValue& value)
{
    // This has some inherent performance overheads (runs multiple times, unecessary)
    // however for now, this works the most clearly due to Cycles restrictions
#ifdef USE_USD_CYCLES_SCHEMA
    _HandleSessionRenderSetting(key, value);
    _HandleSceneRenderSetting(key, value);
    _HandleIntegratorRenderSetting(key, value);
    _HandleFilmRenderSetting(key, value);
    _HandleBackgroundRenderSetting(key, value);
#endif
    return false;
}

bool
HdCyclesRenderParam::_CreateSession()
{
    bool foundDevice = SetDeviceType(m_deviceName, m_sessionParams);

    if (!foundDevice)
        return false;

    m_cyclesSession = new ccl::Session(m_sessionParams);

    m_cyclesSession->write_render_tile_cb
        = std::bind(&HdCyclesRenderParam::_WriteRenderTile, this, ccl::_1);
    m_cyclesSession->update_render_tile_cb
        = std::bind(&HdCyclesRenderParam::_UpdateRenderTile, this, ccl::_1,
                    ccl::_2);

    m_cyclesSession->progress.set_update_callback(
        std::bind(&HdCyclesRenderParam::_SessionUpdateCallback, this));

    return true;
}

void
HdCyclesRenderParam::_WriteRenderTile(ccl::RenderTile& rtile)
{
    // No session, exit out
    if (!m_cyclesSession)
        return;

    if (!m_useTiledRendering)
        return;

    const int w = rtile.w;
    const int h = rtile.h;

    ccl::RenderBuffers* buffers = rtile.buffers;

    // copy data from device
    if (!buffers->copy_from_device())
        return;

    // Adjust absolute sample number to the range.
    int sample = rtile.sample;
    const int range_start_sample
        = m_cyclesSession->tile_manager.range_start_sample;
    if (range_start_sample != -1) {
        sample -= range_start_sample;
    }

    const float exposure = m_cyclesScene->film->exposure;

    if (!m_aovs.empty()) {
        // Blit from the framebuffer to currently selected aovs...
        for (auto& aov : m_aovs) {
            if (!TF_VERIFY(aov.renderBuffer != nullptr)) {
                continue;
            }

            auto* rb = static_cast<HdCyclesRenderBuffer*>(aov.renderBuffer);

            if (rb == nullptr) {
                continue;
            }

            if (rb->GetFormat() == HdFormatInvalid) {
                continue;
            }

            HdCyclesAov cyclesAov;
            if (!GetCyclesAov(aov, cyclesAov)) {
                continue;
            }

            // We don't want a mismatch of formats
            if (rb->GetFormat() != cyclesAov.format) {
                continue;
            }

            bool custom = false;
            if ((cyclesAov.token == HdCyclesAovTokens->CryptoObject) ||
                (cyclesAov.token == HdCyclesAovTokens->CryptoMaterial) ||
                (cyclesAov.token == HdCyclesAovTokens->CryptoAsset) ||
                (cyclesAov.token == HdCyclesAovTokens->AOVC) ||
                (cyclesAov.token == HdCyclesAovTokens->AOVV)) {
                    custom = true;
            }

            // Pixels we will use to get from cycles.
            int numComponents = HdGetComponentCount(cyclesAov.format);
            ccl::vector<float> tileData(w * h * numComponents);

            rb->SetConverged(IsConverged());

            bool read = false;
            if (!custom) {
                read = buffers->get_pass_rect(cyclesAov.name.c_str(),
                                              exposure, sample,
                                              numComponents,
                                              &tileData[0]);
            } else {
                read = buffers->get_pass_rect(aov.aovName.GetText(),
                                              exposure, sample,
                                              numComponents,
                                              &tileData[0]);
            }

            if (!read) {
                memset(&tileData[0], 0,
                        tileData.size() * sizeof(float));
            }

            rb->BlitTile(cyclesAov.format, rtile.x, rtile.y, rtile.w,
                            rtile.h, 0, rtile.w,
                            reinterpret_cast<uint8_t*>(tileData.data()));
        }
    }
}

void
HdCyclesRenderParam::_UpdateRenderTile(ccl::RenderTile& rtile, bool highlight)
{
    if (m_cyclesSession->params.progressive_refine)
        _WriteRenderTile(rtile);
}

bool
HdCyclesRenderParam::_CreateScene()
{
    static const HdCyclesConfig& config = HdCyclesConfig::GetInstance();

    m_cyclesScene = new ccl::Scene(m_sceneParams, m_cyclesSession->device);

    m_width  = config.render_width.value;
    m_height = config.render_height.value;

    m_cyclesScene->camera->width  = m_width;
    m_cyclesScene->camera->height = m_height;

    m_cyclesScene->camera->compute_auto_viewplane();

    m_cyclesSession->scene = m_cyclesScene;

    m_bufferParams.width       = m_width;
    m_bufferParams.height      = m_height;
    m_bufferParams.full_width  = m_width;
    m_bufferParams.full_height = m_height;

    default_attrib_display_color_surface = HdCyclesCreateAttribColorSurface();
    default_attrib_display_color_surface->tag_update(m_cyclesScene);
    m_cyclesScene->shaders.push_back(default_attrib_display_color_surface);

    default_object_display_color_surface = HdCyclesCreateObjectColorSurface();
    default_object_display_color_surface->tag_update(m_cyclesScene);
    m_cyclesScene->shaders.push_back(default_object_display_color_surface);

    SetBackgroundShader(nullptr);

    m_cyclesSession->reset(m_bufferParams, m_sessionParams.samples);

    return true;
}

void
HdCyclesRenderParam::StartRender()
{
    _CyclesStart();
}

void
HdCyclesRenderParam::StopRender()
{
    _CyclesExit();
}


// Deprecate? This isnt used... Also doesnt work
void
HdCyclesRenderParam::RestartRender()
{
    StopRender();
    Initialize({});
    StartRender();
}

void
HdCyclesRenderParam::PauseRender()
{
    if (m_cyclesSession)
        m_cyclesSession->set_pause(true);
}

void
HdCyclesRenderParam::ResumeRender()
{
    if (m_cyclesSession)
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
    lock_guard lock{m_cyclesScene->mutex};

    if (m_shouldUpdate) {
        if (m_cyclesScene->lights.size() > 0) {
            if (m_numDomeLights <= 0)
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

// -- Cycles render device

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
    ccl::SessionParams* params = &m_sessionParams;
    if (m_cyclesSession)
        params = &m_cyclesSession->params;

    return SetDeviceType(a_deviceType, *params);
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

/* ====== HdCycles Settings ====== */

/* ====== Cycles Lifecycle ====== */

void
HdCyclesRenderParam::_CyclesStart()
{
    m_cyclesSession->start();
}

void
HdCyclesRenderParam::_CyclesExit()
{
    m_cyclesSession->set_pause(true);

    lock_guard lock{m_cyclesScene->mutex};

    m_cyclesScene->shaders.clear();
    m_cyclesScene->geometry.clear();
    m_cyclesScene->objects.clear();
    m_cyclesScene->lights.clear();
    m_cyclesScene->particle_systems.clear();

    if (m_cyclesSession) {
        delete m_cyclesSession;
        m_cyclesSession = nullptr;
    }
}

// TODO: Refactor these two resets
void
HdCyclesRenderParam::CyclesReset(bool a_forceUpdate)
{
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
        m_shadersUpdated = false;
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

    m_cyclesSession->reset(m_bufferParams, m_cyclesSession->params.samples);
}

void
HdCyclesRenderParam::SetViewport(int w, int h)
{
    m_width  = w;
    m_height = h;

    m_bufferParams.width          = m_width;
    m_bufferParams.height         = m_height;
    m_bufferParams.full_width     = m_width;
    m_bufferParams.full_height    = m_height;
    m_cyclesScene->camera->width  = m_width;
    m_cyclesScene->camera->height = m_height;
    m_cyclesScene->camera->compute_auto_viewplane();
    m_cyclesScene->camera->need_update        = true;
    m_cyclesScene->camera->need_device_update = true;

    m_aovBindingsNeedValidation = true;

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
    lock_guard lock{m_cyclesScene->mutex};

    if (!m_cyclesScene) {
        TF_WARN("Couldn't add light to scene. Scene is null.");
        return;
    }

    m_lightsUpdated = true;

    m_cyclesScene->lights.push_back(a_light);

    if (a_light->type == ccl::LIGHT_BACKGROUND) {
        m_numDomeLights += 1;
    }
}

void
HdCyclesRenderParam::AddObject(ccl::Object* a_object)
{
    lock_guard lock{m_cyclesScene->mutex};

    if (!m_cyclesScene) {
        TF_WARN("Couldn't add object to scene. Scene is null.");
        return;
    }

    m_objectsUpdated = true;

    m_cyclesScene->objects.push_back(a_object);

    Interrupt();
}

void
HdCyclesRenderParam::AddGeometry(ccl::Geometry* a_geometry)
{
    lock_guard lock{m_cyclesScene->mutex};

    if (!m_cyclesScene) {
        TF_WARN("Couldn't add geometry to scene. Scene is null.");
        return;
    }

    m_geometryUpdated = true;

    m_cyclesScene->geometry.push_back(a_geometry);

    Interrupt();
}

void
HdCyclesRenderParam::AddMesh(ccl::Mesh* a_mesh)
{
    lock_guard lock{m_cyclesScene->mutex};

    if (!m_cyclesScene) {
        TF_WARN("Couldn't add geometry to scene. Scene is null.");
        return;
    }

    m_meshUpdated = true;

    m_cyclesScene->geometry.push_back(a_mesh);

    Interrupt();
}

void
HdCyclesRenderParam::AddCurve(ccl::Geometry* a_curve)
{
    lock_guard lock{m_cyclesScene->mutex};

    if (!m_cyclesScene) {
        TF_WARN("Couldn't add geometry to scene. Scene is null.");
        return;
    }

    m_curveUpdated = true;

    m_cyclesScene->geometry.push_back(a_curve);

    Interrupt();
}

void
HdCyclesRenderParam::AddShader(ccl::Shader* a_shader)
{
    lock_guard lock{m_cyclesScene->mutex};

    if (!m_cyclesScene) {
        TF_WARN("Couldn't add geometry to scene. Scene is null.");
        return;
    }

    m_shadersUpdated = true;

    m_cyclesScene->shaders.push_back(a_shader);
}

void
HdCyclesRenderParam::RemoveObject(ccl::Object* a_object)
{
    lock_guard lock{m_cyclesScene->mutex};

    for (ccl::vector<ccl::Object*>::iterator it = m_cyclesScene->objects.begin();
         it != m_cyclesScene->objects.end();) {
        if (a_object == *it) {
            it = m_cyclesScene->objects.erase(it);

            m_objectsUpdated = true;
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
    lock_guard lock{m_cyclesScene->mutex};

    for (ccl::vector<ccl::Light*>::iterator it = m_cyclesScene->lights.begin();
         it != m_cyclesScene->lights.end();) {
        if (a_light == *it) {
            it = m_cyclesScene->lights.erase(it);

            // TODO: This doesnt respect multiple dome lights
            if (a_light->type == ccl::LIGHT_BACKGROUND) {
                m_numDomeLights = std::max(0, m_numDomeLights - 1);
            }

            m_lightsUpdated = true;

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
    lock_guard lock{m_cyclesScene->mutex};

    for (ccl::vector<ccl::Geometry*>::iterator it
         = m_cyclesScene->geometry.begin();
         it != m_cyclesScene->geometry.end();) {
        if (a_mesh == *it) {
            it = m_cyclesScene->geometry.erase(it);

            m_meshUpdated = true;

            break;
        } else {
            ++it;
        }
    }

    if (m_geometryUpdated)
        Interrupt();
}

void
HdCyclesRenderParam::UpdateShadersTag(ccl::vector<ccl::Shader*>& shaders) {
    lock_guard lock{m_cyclesScene->mutex};
    for(auto& shader : shaders) {
        shader->tag_update(m_cyclesScene);
    }
}

void
HdCyclesRenderParam::RemoveCurve(ccl::Hair* a_hair)
{
    lock_guard lock{m_cyclesScene->mutex};

    for (ccl::vector<ccl::Geometry*>::iterator it
         = m_cyclesScene->geometry.begin();
         it != m_cyclesScene->geometry.end();) {
        if (a_hair == *it) {
            it = m_cyclesScene->geometry.erase(it);

            m_curveUpdated = true;

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
    lock_guard lock{m_cyclesScene->mutex};

    for (ccl::vector<ccl::Shader*>::iterator it = m_cyclesScene->shaders.begin();
         it != m_cyclesScene->shaders.end();) {
        if (a_shader == *it) {
            it = m_cyclesScene->shaders.erase(it);

            m_shadersUpdated = true;

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
    // Currently, collect_statistics errors seemingly during render,
    // we probably need to only access these when the render is complete
    // however this codeflow is currently undefined...

    //ccl::RenderStats stats;
    //m_cyclesSession->collect_statistics(&stats);

    VtDictionary result = {
        { "hdcycles:version", VtValue(HD_CYCLES_VERSION) },

        // - Cycles specific

        // These error out currently, kept for future reference
        /*{ "hdcycles:geometry:total_memory",
          VtValue(ccl::string_human_readable_size(stats.mesh.geometry.total_size)
                      .c_str()) },*/
        /*{ "hdcycles:textures:total_memory",
          VtValue(
              ccl::string_human_readable_size(stats.image.textures.total_size)
                  .c_str()) },*/
        { "hdcycles:scene:num_objects", VtValue(m_cyclesScene->objects.size()) },
        { "hdcycles:scene:num_shaders", VtValue(m_cyclesScene->shaders.size()) },

        // - Solaris, husk specific

        // Currently these don't update properly. It is unclear if we need to tag renderstats as
        // dynamic. Maybe our VtValues need to live longer?

        { "rendererName", VtValue("Cycles") },
        { "rendererVersion", VtValue(HD_CYCLES_VERSION) },
        { "percentDone", VtValue(m_renderPercent) },
        { "fractionDone", VtValue(m_renderProgress) },
        { "lightCounts", VtValue(m_cyclesScene->lights.size()) },
        { "totalClockTime", VtValue(m_totalTime) },
        { "cameraRays", VtValue(0) },
        { "numCompletedSamples", VtValue(0) }

    };

    // We need to store the cryptomatte metadata here, based on if there's any Cryptomatte AOVs

    bool cryptoAsset = false;
    bool cryptoObject = false;
    bool cryptoMaterial = false;

    for (const HdRenderPassAovBinding& aov : m_aovs) {
        TfToken sourceName = GetSourceName(aov);
        if (sourceName == HdCyclesAovTokens->CryptoAsset) {
            cryptoAsset = true;
            continue;
        }
        if (sourceName == HdCyclesAovTokens->CryptoObject) {
            cryptoObject = true;
            continue;
        }
        if (sourceName == HdCyclesAovTokens->CryptoMaterial) {
            cryptoMaterial = true;
            continue;
        }
    }

    if (cryptoAsset) {
        std::string cryptoName = HdCyclesAovTokens->CryptoAsset.GetText();
        std::string identifier = ccl::string_printf("%08x", ccl::util_murmur_hash3(cryptoName.c_str(), cryptoName.length(), 0));
        std::string prefix = "cryptomatte/" + identifier.substr(0, 7) + "/";
        result[prefix + "name"] = VtValue(cryptoName);
        result[prefix + "hash"] = VtValue("MurmurHash3_32");
        result[prefix + "conversion"] = VtValue("uint32_to_float32");
        result[prefix + "manifest"] = VtValue(m_cyclesScene->object_manager->get_cryptomatte_assets(m_cyclesScene));
    }

    if (cryptoObject) {
        std::string cryptoName = HdCyclesAovTokens->CryptoObject.GetText();
        std::string identifier = ccl::string_printf("%08x", ccl::util_murmur_hash3(cryptoName.c_str(), cryptoName.length(), 0));
        std::string prefix = "cryptomatte/" + identifier.substr(0, 7) + "/";
        result[prefix + "name"] = VtValue(cryptoName);
        result[prefix + "hash"] = VtValue("MurmurHash3_32");
        result[prefix + "conversion"] = VtValue("uint32_to_float32");
        result[prefix + "manifest"] = VtValue(m_cyclesScene->object_manager->get_cryptomatte_objects(m_cyclesScene));
    }

    if (cryptoMaterial) {
        std::string cryptoName = HdCyclesAovTokens->CryptoMaterial.GetText();
        std::string identifier = ccl::string_printf("%08x", ccl::util_murmur_hash3(cryptoName.c_str(), cryptoName.length(), 0));
        std::string prefix = "cryptomatte/" + identifier.substr(0, 7) + "/";
        result[prefix + "name"] = VtValue(cryptoName);
        result[prefix + "hash"] = VtValue("MurmurHash3_32");
        result[prefix + "conversion"] = VtValue("uint32_to_float32");
        result[prefix + "manifest"] = VtValue(m_cyclesScene->shader_manager->get_cryptomatte_materials(m_cyclesScene));
    }

    return result;
}

void
HdCyclesRenderParam::SetAovBindings(HdRenderPassAovBindingVector const& a_aovs)
{
    m_aovs = a_aovs;
    m_bufferParams.passes.clear();
    bool has_combined = false;
    bool has_sample_count = false;
    ccl::Film *film = m_cyclesScene->film;

    ccl::CryptomatteType cryptomatte_passes = ccl::CRYPT_NONE;
    if (film->cryptomatte_passes & ccl::CRYPT_ACCURATE) {
        cryptomatte_passes = (ccl::CryptomatteType)(cryptomatte_passes | ccl::CRYPT_ACCURATE);
    }
    film->cryptomatte_passes = cryptomatte_passes;

    int cryptoObject = 0;
    int cryptoMaterial = 0;
    int cryptoAsset = 0;

    for (const HdRenderPassAovBinding& aov : m_aovs) {

        TfToken sourceName = GetSourceName(aov);

        for (HdCyclesAov& cyclesAov : DefaultAovs) {
            if (sourceName == cyclesAov.token) {
                if (cyclesAov.type == ccl::PASS_COMBINED) {
                    has_combined = true;
                }
                else if(cyclesAov.type == ccl::PASS_SAMPLE_COUNT) {
                    has_sample_count = true;
                }
                ccl::Pass::add(cyclesAov.type, m_bufferParams.passes, cyclesAov.name.c_str(), cyclesAov.filter);
                continue;
            }
        }

        for (HdCyclesAov& cyclesAov : CustomAovs) {
            if (sourceName == cyclesAov.token) {
                ccl::Pass::add(cyclesAov.type, m_bufferParams.passes, aov.aovName.GetText(), cyclesAov.filter);
                continue;
            }
        }

        for (HdCyclesAov& cyclesAov : CryptomatteAovs) {
            if (sourceName == cyclesAov.token) {
                if (cyclesAov.token == HdCyclesAovTokens->CryptoObject) {
                    cryptoObject += 1;
                    continue;
                }
                if (cyclesAov.token == HdCyclesAovTokens->CryptoMaterial) {
                    cryptoMaterial += 1;
                    continue;
                }
                if (cyclesAov.token == HdCyclesAovTokens->CryptoAsset) {
                    cryptoAsset += 1;
                    continue;
                }
            }
        }
    }

    if (cryptoObject != film->cryptomatte_depth) {
        TF_WARN("Cryptomatte Object AOV/depth mismatch");
    }
    if (cryptoMaterial != film->cryptomatte_depth) {
        TF_WARN("Cryptomatte Material AOV/depth mismatch");
    }
    if (cryptoAsset != film->cryptomatte_depth) {
        TF_WARN("Cryptomatte Asset AOV/depth mismatch");
    }

    // Ordering matters
    if (cryptoObject) {
        film->cryptomatte_passes = (ccl::CryptomatteType)(film->cryptomatte_passes | ccl::CRYPT_OBJECT);
        for (int i = 0; i < cryptoObject; ++i) {
            ccl::Pass::add(ccl::PASS_CRYPTOMATTE, m_bufferParams.passes, 
                           ccl::string_printf("%s%02i", HdCyclesAovTokens->CryptoObject.GetText(), i).c_str());
        }
    }
    if (cryptoMaterial) {
        film->cryptomatte_passes = (ccl::CryptomatteType)(film->cryptomatte_passes | ccl::CRYPT_MATERIAL);
        for (int i = 0; i < cryptoMaterial; ++i) {
            ccl::Pass::add(ccl::PASS_CRYPTOMATTE, m_bufferParams.passes,
                           ccl::string_printf("%s%02i", HdCyclesAovTokens->CryptoMaterial.GetText(), i).c_str());
        }
    }
    if (cryptoAsset) {
        film->cryptomatte_passes = (ccl::CryptomatteType)(film->cryptomatte_passes | ccl::CRYPT_ASSET);
        for (int i = 0; i < cryptoAsset; ++i) {
            ccl::Pass::add(ccl::PASS_CRYPTOMATTE, m_bufferParams.passes,
                           ccl::string_printf("%s%02i", HdCyclesAovTokens->CryptoAsset.GetText(), i).c_str());
        }
    }

    if (m_sessionParams.adaptive_sampling)
    {
        ccl::Pass::add(ccl::PASS_ADAPTIVE_AUX_BUFFER, m_bufferParams.passes);
        if (!has_sample_count) {
            ccl::Pass::add(ccl::PASS_SAMPLE_COUNT, m_bufferParams.passes);
        }
    }

    if (!has_combined) {
        ccl::Pass::add(DefaultAovs[0].type, m_bufferParams.passes, DefaultAovs[0].name.c_str(), DefaultAovs[0].filter);
    }
    film->display_pass = m_bufferParams.passes[0].type;
    film->tag_passes_update(m_cyclesScene, m_bufferParams.passes);

    film->tag_update(m_cyclesScene);
    Interrupt();
}

void
HdCyclesRenderParam::SetDisplayAov(HdRenderPassAovBinding const& a_aov)
{
    m_cyclesScene->film->display_pass = DefaultAovs[0].type;
    m_displayAovToken = DefaultAovs[0].token;
    if(!m_aovs.empty()) {
        TfToken sourceName = GetSourceName(a_aov);
        for (HdCyclesAov& cyclesAov : DefaultAovs) {
            if (sourceName == cyclesAov.token) {
                m_cyclesScene->film->display_pass = cyclesAov.type;
                m_displayAovToken = a_aov.aovName;
                break;
            }
        }
        m_cyclesScene->film->tag_update(m_cyclesScene);
        Interrupt();
    }
}

PXR_NAMESPACE_CLOSE_SCOPE
