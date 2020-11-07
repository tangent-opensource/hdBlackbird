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

#ifdef WITH_CYCLES_LOGGING
#    include <util/util_logging.h>
#endif
#ifdef USE_USD_CYCLES_SCHEMA
#    include <usdCycles/tokens.h>
#endif

PXR_NAMESPACE_OPEN_SCOPE

double
clamp(double d, double min, double max)
{
    const double t = d < min ? min : d;
    return t > max ? max : t;
}

HdCyclesRenderParam::HdCyclesRenderParam()
    : m_shouldUpdate(false)
    , m_useSquareSamples(false)
    , m_cyclesScene(nullptr)
    , m_cyclesSession(nullptr)
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
    return m_cyclesSession->progress.get_progress();
}

void
HdCyclesRenderParam::_SessionPrintStatus()
{
    std::string status, substatus;

    /* get status */
    float progress = m_cyclesSession->progress.get_progress();
    m_cyclesSession->progress.get_status(status, substatus);

    if (HdCyclesConfig::GetInstance().enable_progress) {
        std::cout << "Progress: " << (int)(round(progress * 100)) << "%\n";
    }

    if (HdCyclesConfig::GetInstance().enable_logging) {
        if (substatus != "")
            status += ": " + substatus;

        std::cout << "cycles: " << progress << " : " << status << '\n';
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

    return true;
}

// -- Session

void
HdCyclesRenderParam::_UpdateSessionFromConfig(bool a_forceInit)
{
    static const HdCyclesConfig& config = HdCyclesConfig::GetInstance();

    //m_sessionParams.experimental          = config.enable_experimental;
    config.enable_experimental.eval(m_sessionParams.experimental, a_forceInit);

    config.display_buffer_linear.eval(m_sessionParams.display_buffer_linear,
                                      a_forceInit);

    m_sessionParams.shadingsystem = ccl::SHADINGSYSTEM_SVM;
    if (config.shading_system.value == "OSL"
        || config.shading_system.value == "SHADINGSYSTEM_OSL")
        m_sessionParams.shadingsystem = ccl::SHADINGSYSTEM_OSL;

    m_sessionParams.background = false;

    /* Use progressive rendering */

    m_sessionParams.progressive = true;

    config.start_resolution.eval(m_sessionParams.start_resolution, a_forceInit);

    m_sessionParams.progressive_refine         = false;
    m_sessionParams.progressive_update_timeout = 0.1;
    config.pixel_size.eval(m_sessionParams.pixel_size, a_forceInit);
    //m_sessionParams.tile_size.x                = config.tile_size[0];64
    //m_sessionParams.tile_size.y                = config.tile_size[1];64

    config.max_samples.eval(m_sessionParams.samples, a_forceInit);
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

    if (!m_cyclesSession)
        return false;

    bool session_updated = false;

    if (key == usdCyclesTokens->cyclesBackground) {
        m_sessionParams.background
            = _HdCyclesGetVtValue<bool>(value, m_sessionParams.background,
                                        &session_updated);
    }
    if (key == usdCyclesTokens->cyclesProgressive_refine) {
        m_sessionParams.progressive_refine = _HdCyclesGetVtValue<bool>(
            value, m_sessionParams.progressive_refine, &session_updated);
    }

    if (key == usdCyclesTokens->cyclesProgressive) {
        m_sessionParams.progressive
            = _HdCyclesGetVtValue<bool>(value, m_sessionParams.progressive,
                                        &session_updated);
    }

    if (key == usdCyclesTokens->cyclesExperimental) {
        m_sessionParams.experimental
            = _HdCyclesGetVtValue<bool>(value, m_sessionParams.experimental,
                                        &session_updated);
    }

    if (key == usdCyclesTokens->cyclesSamples) {
        m_sessionParams.samples
            = _HdCyclesGetVtValue<int>(value, m_sessionParams.samples,
                                       &session_updated);
    }

    /*if (key == usdCyclesTokens->cyclesTile_size) {
        m_sessionParams.tile_size = vec2i_to_int2(
            _HdCyclesGetVtValue<GfVec2i>(value, m_sessionParams.tile_size,
                                         &session_updated));
    }*/

    //TileOrder tile_order;

    if (key == usdCyclesTokens->cyclesStart_resolution) {
        m_sessionParams.start_resolution
            = _HdCyclesGetVtValue<int>(value, m_sessionParams.start_resolution,
                                       &session_updated);
    }
    if (key == usdCyclesTokens->cyclesDenoising_start_sample) {
        m_sessionParams.denoising_start_sample = _HdCyclesGetVtValue<int>(
            value, m_sessionParams.denoising_start_sample, &session_updated);
    }
    if (key == usdCyclesTokens->cyclesPixel_size) {
        m_sessionParams.pixel_size
            = _HdCyclesGetVtValue<int>(value, m_sessionParams.pixel_size,
                                       &session_updated);
    }
    if (key == usdCyclesTokens->cyclesThreads) {
        m_sessionParams.threads
            = _HdCyclesGetVtValue<int>(value, m_sessionParams.threads,
                                       &session_updated);
    }
    if (key == usdCyclesTokens->cyclesAdaptive_sampling) {
        m_sessionParams.adaptive_sampling = _HdCyclesGetVtValue<bool>(
            value, m_sessionParams.adaptive_sampling, &session_updated);
    }
    if (key == usdCyclesTokens->cyclesUse_profiling) {
        m_sessionParams.use_profiling
            = _HdCyclesGetVtValue<bool>(value, m_sessionParams.use_profiling,
                                        &session_updated);
    }
    if (key == usdCyclesTokens->cyclesDisplay_buffer_linear) {
        m_sessionParams.display_buffer_linear = _HdCyclesGetVtValue<bool>(
            value, m_sessionParams.display_buffer_linear, &session_updated);
    }

    //DenoiseParams denoising;
    //ShadingSystem shadingsystem;

    if (session_updated) {
        // Although this is called, it does not correctly reset session in IPR
        //Interrupt();
        return true;
    }


#endif
}

// -- Scene

void
HdCyclesRenderParam::_UpdateSceneFromConfig(bool a_forceInit)
{
    static const HdCyclesConfig& config = HdCyclesConfig::GetInstance();

    // -- Scene init
    m_sceneParams.shadingsystem = m_sessionParams.shadingsystem;

    m_sceneParams.bvh_type = ccl::SceneParams::BVH_DYNAMIC;
    if (config.bvh_type.value == "STATIC")
        m_sceneParams.bvh_type = ccl::SceneParams::BVH_STATIC;

    m_sceneParams.persistent_data = true;
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

    bool scene_updated = false;

    if (key == usdCyclesTokens->cyclesShading_system) {
        TfToken shading_system
            = _HdCyclesGetVtValue<TfToken>(value, usdCyclesTokens->svm,
                                           &scene_updated);
        if (shading_system == usdCyclesTokens->svm) {
            m_sceneParams.shadingsystem = ccl::SHADINGSYSTEM_SVM;
        } else if (shading_system == usdCyclesTokens->osl) {
            m_sceneParams.shadingsystem = ccl::SHADINGSYSTEM_OSL;
        }
    }

    if (key == usdCyclesTokens->cyclesBvh_type) {
        TfToken bvh_type
            = _HdCyclesGetVtValue<TfToken>(value, usdCyclesTokens->bvh_dynamic,
                                           &scene_updated);
        if (bvh_type == usdCyclesTokens->bvh_dynamic) {
            m_sceneParams.bvh_type = ccl::SceneParams::BVH_DYNAMIC;
        } else if (bvh_type == usdCyclesTokens->bvh_static) {
            m_sceneParams.bvh_type = ccl::SceneParams::BVH_STATIC;
        }
    }

    if (scene_updated) {
        // Although this is called, it does not correctly reset session in IPR
        //Interrupt();
        return true;
    }

#endif
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
                                           &integrator_updated);
        if (integratorMethod == usdCyclesTokens->path) {
            integrator->method = ccl::Integrator::PATH;
        } else {
            integrator->method = ccl::Integrator::BRANCHED_PATH;
        }
    }
    if (key == usdCyclesTokens->cyclesIntegratorSampling_method) {
        TfToken samplingMethod
            = _HdCyclesGetVtValue<TfToken>(value, usdCyclesTokens->sobol,
                                           &integrator_updated);
        if (samplingMethod == usdCyclesTokens->sobol) {
            integrator->sampling_pattern = ccl::SAMPLING_PATTERN_SOBOL;
        } else if (samplingMethod == usdCyclesTokens->cmj) {
            integrator->sampling_pattern = ccl::SAMPLING_PATTERN_CMJ;
        } else {
            integrator->sampling_pattern = ccl::SAMPLING_PATTERN_PMJ;
        }
    }
    //uniform token cycles:integrator:sampling_method = "sobol" (
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

    // Samples
    if (key == usdCyclesTokens->cyclesIntegratorAa_samples) {
        integrator->aa_samples
            = _HdCyclesGetVtValue<int>(value, integrator->aa_samples,
                                       &integrator_updated);
    }
    if (key == usdCyclesTokens->cyclesIntegratorDiffuse_samples) {
        integrator->diffuse_samples
            = _HdCyclesGetVtValue<int>(value, integrator->diffuse_samples,
                                       &integrator_updated);
    }
    if (key == usdCyclesTokens->cyclesIntegratorGlossy_samples) {
        integrator->glossy_samples
            = _HdCyclesGetVtValue<int>(value, integrator->glossy_samples,
                                       &integrator_updated);
    }
    if (key == usdCyclesTokens->cyclesIntegratorTransmission_samples) {
        integrator->transmission_samples
            = _HdCyclesGetVtValue<int>(value, integrator->transmission_samples,
                                       &integrator_updated);
    }
    if (key == usdCyclesTokens->cyclesIntegratorAo_samples) {
        integrator->ao_samples
            = _HdCyclesGetVtValue<int>(value, integrator->ao_samples,
                                       &integrator_updated);
    }
    if (key == usdCyclesTokens->cyclesIntegratorMesh_light_samples) {
        integrator->mesh_light_samples
            = _HdCyclesGetVtValue<int>(value, integrator->mesh_light_samples,
                                       &integrator_updated);
    }
    if (key == usdCyclesTokens->cyclesIntegratorSubsurface_samples) {
        integrator->subsurface_samples
            = _HdCyclesGetVtValue<int>(value, integrator->subsurface_samples,
                                       &integrator_updated);
    }
    if (key == usdCyclesTokens->cyclesIntegratorVolume_samples) {
        integrator->volume_samples
            = _HdCyclesGetVtValue<int>(value, integrator->volume_samples,
                                       &integrator_updated);
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

    if (film_updated) {
        film->tag_update(m_cyclesScene);
        return true;
    }

#endif
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

    //uint visibility;

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
}

bool
HdCyclesRenderParam::SetRenderSetting(const TfToken& key, const VtValue& value)
{
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

    if (HdCyclesConfig::GetInstance().enable_logging
        || HdCyclesConfig::GetInstance().enable_progress)
        m_cyclesSession->progress.set_update_callback(
            std::bind(&HdCyclesRenderParam::_SessionPrintStatus, this));

    return true;
}

// TODO: Tidy and move these sub function
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

    default_vcol_surface = HdCyclesCreateDefaultShader();

    default_vcol_surface->tag_update(m_cyclesScene);
    m_cyclesScene->shaders.push_back(default_vcol_surface);

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

void
HdCyclesRenderParam::RestartRender()
{
    std::cout << "===== Restart Render\n";
    // Not called
    StopRender();
    Initialize({});
    StartRender();
}

void
HdCyclesRenderParam::PauseRender()
{
    if (m_cyclesSession)
        m_cyclesSession->set_pause(true);
    else {
        std::cout << "Couldn't pause: No session\n";
    }
}

void
HdCyclesRenderParam::ResumeRender()
{
    if (m_cyclesSession)
        m_cyclesSession->set_pause(false);
    else {
        std::cout << "Couldn't resume: No session\n";
    }
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
    return SetDeviceType(a_deviceType, m_sessionParams);
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
HdCyclesRenderParam::SetShutterMotionPosition(const int& avalue)
{
    SetShutterMotionPosition((ccl::Camera::MotionPosition)avalue);
}

void
HdCyclesRenderParam::SetShutterMotionPosition(
    const ccl::Camera::MotionPosition& avalue)
{
    m_cyclesScene->camera->motion_position = avalue;
}

const ccl::Camera::MotionPosition&
HdCyclesRenderParam::GetShutterMotionPosition()
{
    return m_cyclesScene->camera->motion_position;
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
    m_cyclesSession->reset(m_bufferParams, m_sessionParams.samples);
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
    m_cyclesSession->reset(m_bufferParams, m_sessionParams.samples);
}

void
HdCyclesRenderParam::DirectReset()
{
    m_cyclesSession->reset(m_bufferParams, m_sessionParams.samples);
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

PXR_NAMESPACE_CLOSE_SCOPE
