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

#include "renderDelegate.h"

#include "basisCurves.h"
#include "camera.h"
#include "config.h"
#include "instancer.h"
#include "light.h"
#include "material.h"
#include "mesh.h"
#include "points.h"
#include "renderBuffer.h"
#include "renderParam.h"
#include "renderPass.h"
#include "utils.h"

#include <render/film.h>
#include <render/integrator.h>

#include <boost/algorithm/string.hpp>

#include <pxr/base/gf/api.h>
#include <pxr/base/gf/vec2i.h>
#include <pxr/base/tf/getenv.h>
#include <pxr/base/vt/api.h>
#include <pxr/imaging/hd/camera.h>
#include <pxr/imaging/hd/tokens.h>

#ifdef USE_USD_CYCLES_SCHEMA
#    include <usdCycles/tokens.h>
#endif

PXR_NAMESPACE_OPEN_SCOPE

// clang-format off
TF_DEFINE_PRIVATE_TOKENS(_tokens,
    (cycles)
    (openvdbAsset)
);
// clang-format on

TF_DEFINE_PUBLIC_TOKENS(HdCyclesIntegratorTokens, HDCYCLES_INTEGRATOR_TOKENS);

// clang-format off
const TfTokenVector HdCyclesRenderDelegate::SUPPORTED_RPRIM_TYPES = {
    HdPrimTypeTokens->mesh,
    HdPrimTypeTokens->basisCurves,
    HdPrimTypeTokens->points,
};

const TfTokenVector HdCyclesRenderDelegate::SUPPORTED_SPRIM_TYPES = {
    HdPrimTypeTokens->camera,
    HdPrimTypeTokens->material,
    HdPrimTypeTokens->cylinderLight,
    HdPrimTypeTokens->distantLight,
    HdPrimTypeTokens->diskLight,
    HdPrimTypeTokens->domeLight,
    HdPrimTypeTokens->rectLight,
    HdPrimTypeTokens->sphereLight,
};

const TfTokenVector HdCyclesRenderDelegate::SUPPORTED_BPRIM_TYPES = { 
    HdPrimTypeTokens->renderBuffer
};

// clang-format on

HdCyclesRenderDelegate::HdCyclesRenderDelegate()
    : HdRenderDelegate()
    , m_hasStarted(false)
{
    _Initialize();
}

HdCyclesRenderDelegate::HdCyclesRenderDelegate(
    HdRenderSettingsMap const& settingsMap)
    : HdRenderDelegate(settingsMap)
    , m_hasStarted(false)
{
    _Initialize();

    // Set initial render settings from settings map
    for (auto& entry : settingsMap) {
        _SetRenderSetting(entry.first, entry.second);
    }
}

void
HdCyclesRenderDelegate::_Initialize()
{
    // -- Initialize Render Param (Core cycles wrapper)
    m_renderParam.reset(new HdCyclesRenderParam());

    if (!m_renderParam->Initialize())
        return;

    // -- Initialize Render Delegate components

    m_resourceRegistry.reset(new HdResourceRegistry());

    // -- Setup render settings

    _InitializeCyclesRenderSettings();

    _PopulateDefaultSettings(m_settingDescriptors);

    // Set default render settings in cycles
    for (size_t i = 0; i < m_settingDescriptors.size(); ++i) {
        _SetRenderSetting(m_settingDescriptors[i].key,
                          m_settingDescriptors[i].defaultValue);
    }
}

HdCyclesRenderDelegate::~HdCyclesRenderDelegate()
{
    m_renderParam->StopRender();
    m_resourceRegistry.reset();
}

TfTokenVector const&
HdCyclesRenderDelegate::GetSupportedRprimTypes() const
{
    return SUPPORTED_RPRIM_TYPES;
}

TfTokenVector const&
HdCyclesRenderDelegate::GetSupportedSprimTypes() const
{
    return SUPPORTED_SPRIM_TYPES;
}

TfTokenVector const&
HdCyclesRenderDelegate::GetSupportedBprimTypes() const
{
    return SUPPORTED_BPRIM_TYPES;
}

void
HdCyclesRenderDelegate::_InitializeCyclesRenderSettings()
{
    static const HdCyclesConfig& config = HdCyclesConfig::GetInstance();

#ifdef USE_USD_CYCLES_SCHEMA

    m_settingDescriptors.push_back({ std::string("Exposure"),
                                     usdCyclesTokens->cyclesFilmExposure,
                                     VtValue(m_renderParam->GetExposure()) });


    m_settingDescriptors.push_back({ std::string("Samples"),
                                     usdCyclesTokens->cyclesSamples,
                                     VtValue(m_renderParam->GetMaxSamples()) });

#endif

    /*m_settingDescriptors.push_back(
        { std::string("Use Motion Blur"),
          HdCyclesRenderSettingsTokens->useMotionBlur,
          VtValue(m_renderParam->GetUseMotionBlur()) });

    m_settingDescriptors.push_back(
        { std::string("Motion Steps"),
          HdCyclesRenderSettingsTokens->motionSteps,
          VtValue(m_renderParam->GetMotionSteps()) });

    // -- Featureset

    m_settingDescriptors.push_back(
        { std::string("Device"), HdCyclesRenderSettingsTokens->device,
          VtValue(m_renderParam->GetDeviceTypeName()) });

    m_settingDescriptors.push_back(
        { std::string("Use Experimental Cycles"),
          HdCyclesRenderSettingsTokens->experimental,
          VtValue(m_renderParam->GetUseExperimental()) });

    m_settingDescriptors.push_back({ std::string("Max Samples"),
                                     HdCyclesRenderSettingsTokens->samples,
                                     VtValue(m_renderParam->GetMaxSamples()) });

    m_settingDescriptors.push_back({ std::string("Num Threads"),
                                     HdCyclesRenderSettingsTokens->threads,
                                     VtValue(m_renderParam->GetNumThreads()) });

    m_settingDescriptors.push_back({ std::string("Pixel Size"),
                                     HdCyclesRenderSettingsTokens->pixelSize,
                                     VtValue(m_renderParam->GetPixelSize()) });

    m_settingDescriptors.push_back({ std::string("Tile Size"),
                                     HdCyclesRenderSettingsTokens->tileSize,
                                     VtValue(m_renderParam->GetTileSize()) });

    m_settingDescriptors.push_back(
        { std::string("Start Resolution"),
          HdCyclesRenderSettingsTokens->startResolution,
          VtValue(m_renderParam->GetStartResolution()) });

    m_settingDescriptors.push_back({ std::string("Exposure"),
                                     HdCyclesRenderSettingsTokens->exposure,
                                     VtValue(m_renderParam->GetExposure()) });

    m_settingDescriptors.push_back(
        { std::string("Motion Position"),
          HdCyclesRenderSettingsTokens->motionBlurPosition,
          VtValue((int)m_renderParam->GetShutterMotionPosition()) });

    // -- Integrator Settings

    m_settingDescriptors.push_back(
        { std::string("Integrator Method"),
          HdCyclesRenderSettingsTokens->integratorMethod,
          VtValue(config.integrator_method) });

    m_settingDescriptors.push_back(
        { std::string("Diffuse Samples"),
          HdCyclesRenderSettingsTokens->lightPathsDiffuse,
          VtValue(config.diffuse_samples) });

    m_settingDescriptors.push_back(
        { std::string("Glossy Samples"),
          HdCyclesRenderSettingsTokens->lightPathsGlossy,
          VtValue(config.glossy_samples) });

    m_settingDescriptors.push_back(
        { std::string("Transmission Samples"),
          HdCyclesRenderSettingsTokens->lightPathsTransmission,
          VtValue(config.transmission_samples) });

    m_settingDescriptors.push_back({ std::string("AO Samples"),
                                     HdCyclesRenderSettingsTokens->lightPathsAO,
                                     VtValue(config.ao_samples) });

    m_settingDescriptors.push_back(
        { std::string("Mesh Light Samples"),
          HdCyclesRenderSettingsTokens->lightPathsMeshLight,
          VtValue(config.mesh_light_samples) });

    m_settingDescriptors.push_back(
        { std::string("Subsurface Samples"),
          HdCyclesRenderSettingsTokens->lightPathsSubsurface,
          VtValue(config.subsurface_samples) });

    m_settingDescriptors.push_back(
        { std::string("Volume Samples"),
          HdCyclesRenderSettingsTokens->lightPathsVolume,
          VtValue(config.volume_samples) });*/
}

void
HdCyclesRenderDelegate::_SetRenderSetting(const TfToken& key,
                                          const VtValue& _value)
{
    if (!m_renderParam && !m_renderParam->GetCyclesSession())
        return;

    static const HdCyclesConfig& config = HdCyclesConfig::GetInstance();

    ccl::Integrator* integrator = m_renderParam->GetCyclesScene()->integrator;
    bool integrator_updated     = false;

    ccl::Film* film   = m_renderParam->GetCyclesScene()->film;
    bool film_updated = false;

    ccl::SessionParams* session_params
        = &m_renderParam->GetCyclesSession()->params;
    bool session_updated = false;

#ifdef USE_USD_CYCLES_SCHEMA

    // -- Session Settings

    if (key == usdCyclesTokens->cyclesSamples) {
        session_params->samples
            = _HdCyclesGetVtValue<int>(_value, session_params->samples,
                                       &session_updated);
    }

    // -- Integrator Settings

    if (key == usdCyclesTokens->cyclesIntegratorMin_bounce) {
        integrator->min_bounce
            = _HdCyclesGetVtValue<int>(_value, integrator->min_bounce,
                                       &integrator_updated);
    }

    if (key == usdCyclesTokens->cyclesIntegratorMax_bounce) {
        integrator->max_bounce
            = _HdCyclesGetVtValue<int>(_value, integrator->max_bounce,
                                       &integrator_updated);
    }
    if (key == usdCyclesTokens->cyclesIntegratorMethod) {
        TfToken integratorMethod
            = _HdCyclesGetVtValue<TfToken>(_value, usdCyclesTokens->path,
                                           &integrator_updated);
        if (integratorMethod == usdCyclesTokens->path) {
            integrator->method = ccl::Integrator::PATH;
        } else {
            integrator->method = ccl::Integrator::BRANCHED_PATH;
        }
    }
    if (key == usdCyclesTokens->cyclesIntegratorSampling_method) {
        TfToken samplingMethod
            = _HdCyclesGetVtValue<TfToken>(_value, usdCyclesTokens->sobol,
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
            = _HdCyclesGetVtValue<int>(_value, integrator->max_diffuse_bounce,
                                       &integrator_updated);
    }
    if (key == usdCyclesTokens->cyclesIntegratorMax_glossy_bounce) {
        integrator->max_glossy_bounce
            = _HdCyclesGetVtValue<int>(_value, integrator->max_glossy_bounce,
                                       &integrator_updated);
    }
    if (key == usdCyclesTokens->cyclesIntegratorMax_transmission_bounce) {
        integrator->max_transmission_bounce = _HdCyclesGetVtValue<int>(
            _value, integrator->max_transmission_bounce, &integrator_updated);
    }
    if (key == usdCyclesTokens->cyclesIntegratorMax_volume_bounce) {
        integrator->max_volume_bounce
            = _HdCyclesGetVtValue<int>(_value, integrator->max_volume_bounce,
                                       &integrator_updated);
    }
    if (key == usdCyclesTokens->cyclesIntegratorTransparent_min_bounce) {
        integrator->transparent_min_bounce = _HdCyclesGetVtValue<int>(
            _value, integrator->transparent_min_bounce, &integrator_updated);
    }
    if (key == usdCyclesTokens->cyclesIntegratorTransparent_max_bounce) {
        integrator->transparent_max_bounce = _HdCyclesGetVtValue<int>(
            _value, integrator->transparent_max_bounce, &integrator_updated);
    }

    if (key == usdCyclesTokens->cyclesIntegratorAo_bounces) {
        integrator->ao_bounces
            = _HdCyclesGetVtValue<int>(_value, integrator->ao_bounces,
                                       &integrator_updated);
    }
    if (key == usdCyclesTokens->cyclesIntegratorVolume_max_steps) {
        integrator->volume_max_steps
            = _HdCyclesGetVtValue<int>(_value, integrator->volume_max_steps,
                                       &integrator_updated);
    }
    if (key == usdCyclesTokens->cyclesIntegratorVolume_step_size) {
        integrator->volume_step_rate
            = _HdCyclesGetVtValue<float>(_value, integrator->volume_step_rate,
                                         &integrator_updated);
    }

    // Samples
    if (key == usdCyclesTokens->cyclesIntegratorAa_samples) {
        integrator->aa_samples
            = _HdCyclesGetVtValue<int>(_value, integrator->aa_samples,
                                       &integrator_updated);
    }
    if (key == usdCyclesTokens->cyclesIntegratorDiffuse_samples) {
        integrator->diffuse_samples
            = _HdCyclesGetVtValue<int>(_value, integrator->diffuse_samples,
                                       &integrator_updated);
    }
    if (key == usdCyclesTokens->cyclesIntegratorGlossy_samples) {
        integrator->glossy_samples
            = _HdCyclesGetVtValue<int>(_value, integrator->glossy_samples,
                                       &integrator_updated);
    }
    if (key == usdCyclesTokens->cyclesIntegratorTransmission_samples) {
        integrator->transmission_samples
            = _HdCyclesGetVtValue<int>(_value, integrator->transmission_samples,
                                       &integrator_updated);
    }
    if (key == usdCyclesTokens->cyclesIntegratorAo_samples) {
        integrator->ao_samples
            = _HdCyclesGetVtValue<int>(_value, integrator->ao_samples,
                                       &integrator_updated);
    }
    if (key == usdCyclesTokens->cyclesIntegratorMesh_light_samples) {
        integrator->mesh_light_samples
            = _HdCyclesGetVtValue<int>(_value, integrator->mesh_light_samples,
                                       &integrator_updated);
    }
    if (key == usdCyclesTokens->cyclesIntegratorSubsurface_samples) {
        integrator->subsurface_samples
            = _HdCyclesGetVtValue<int>(_value, integrator->subsurface_samples,
                                       &integrator_updated);
    }
    if (key == usdCyclesTokens->cyclesIntegratorVolume_samples) {
        integrator->volume_samples
            = _HdCyclesGetVtValue<int>(_value, integrator->volume_samples,
                                       &integrator_updated);
    }
    if (key == usdCyclesTokens->cyclesIntegratorStart_sample) {
        integrator->start_sample
            = _HdCyclesGetVtValue<int>(_value, integrator->start_sample,
                                       &integrator_updated);
    }

    // Caustics
    if (key == usdCyclesTokens->cyclesIntegratorCaustics_reflective) {
        integrator->caustics_reflective
            = _HdCyclesGetVtValue<bool>(_value, integrator->caustics_reflective,
                                        &integrator_updated);
    }
    if (key == usdCyclesTokens->cyclesIntegratorCaustics_refractive) {
        integrator->caustics_refractive
            = _HdCyclesGetVtValue<bool>(_value, integrator->caustics_refractive,
                                        &integrator_updated);
    }

    // Filter
    if (key == usdCyclesTokens->cyclesIntegratorFilter_glossy) {
        integrator->filter_glossy
            = _HdCyclesGetVtValue<float>(_value, integrator->filter_glossy,
                                         &integrator_updated);
    }

    if (key == usdCyclesTokens->cyclesIntegratorSample_clamp_direct) {
        integrator->sample_clamp_direct = _HdCyclesGetVtValue<float>(
            _value, integrator->sample_clamp_direct, &integrator_updated);
    }
    if (key == usdCyclesTokens->cyclesIntegratorSample_clamp_indirect) {
        integrator->sample_clamp_indirect = _HdCyclesGetVtValue<float>(
            _value, integrator->sample_clamp_indirect, &integrator_updated);
    }

    if (key == usdCyclesTokens->cyclesIntegratorMotion_blur) {
        integrator->motion_blur
            = _HdCyclesGetVtValue<bool>(_value, integrator->motion_blur,
                                        &integrator_updated);
    }

    if (key == usdCyclesTokens->cyclesIntegratorSample_all_lights_direct) {
        integrator->sample_all_lights_direct = _HdCyclesGetVtValue<bool>(
            _value, integrator->sample_all_lights_direct, &integrator_updated);
    }
    if (key == usdCyclesTokens->cyclesIntegratorSample_all_lights_indirect) {
        integrator->sample_all_lights_indirect
            = _HdCyclesGetVtValue<bool>(_value,
                                        integrator->sample_all_lights_indirect,
                                        &integrator_updated);
    }

    if (key == usdCyclesTokens->cyclesIntegratorLight_sampling_threshold) {
        integrator->light_sampling_threshold = _HdCyclesGetVtValue<float>(
            _value, integrator->light_sampling_threshold, &integrator_updated);
    }

    // -- Film Settings

    if (key == usdCyclesTokens->cyclesFilmExposure) {
        film->exposure = _HdCyclesGetVtValue<float>(_value, film->exposure,
                                                    &film_updated, false);
    }

    if (key == usdCyclesTokens->cyclesFilmPass_alpha_threshold) {
        film->pass_alpha_threshold
            = _HdCyclesGetVtValue<float>(_value, film->pass_alpha_threshold,
                                         &film_updated, false);
    }

    // Filter
    if (key == usdCyclesTokens->cyclesFilmFilter_type) {
        TfToken filter = _HdCyclesGetVtValue<TfToken>(_value,
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
        film->filter_width = _HdCyclesGetVtValue<float>(_value,
                                                        film->filter_width,
                                                        &film_updated, false);
    }

    // Mist
    if (key == usdCyclesTokens->cyclesFilmMist_start) {
        film->mist_start = _HdCyclesGetVtValue<float>(_value, film->mist_start,
                                                      &film_updated, false);
    }
    if (key == usdCyclesTokens->cyclesFilmMist_depth) {
        film->mist_depth = _HdCyclesGetVtValue<float>(_value, film->mist_depth,
                                                      &film_updated, false);
    }
    if (key == usdCyclesTokens->cyclesFilmMist_falloff) {
        film->mist_falloff = _HdCyclesGetVtValue<float>(_value,
                                                        film->mist_falloff,
                                                        &film_updated, false);
    }

    // Light
    if (key == usdCyclesTokens->cyclesFilmUse_light_visibility) {
        film->use_light_visibility
            = _HdCyclesGetVtValue<bool>(_value, film->use_light_visibility,
                                        &film_updated, false);
    }
#endif

    if (integrator_updated) {
        integrator->tag_update(m_renderParam->GetCyclesScene());
    }

    if (film_updated) {
        film->tag_update(m_renderParam->GetCyclesScene());
    }

    if (session_updated) {
        m_renderParam->Interrupt();
    }
}

void
HdCyclesRenderDelegate::SetRenderSetting(const TfToken& key,
                                         const VtValue& value)
{
    HdRenderDelegate::SetRenderSetting(key, value);
    _SetRenderSetting(key, value);
    m_renderParam->Interrupt();
}

HdRenderSettingDescriptorList
HdCyclesRenderDelegate::GetRenderSettingDescriptors() const
{
    return m_settingDescriptors;
}

HdRenderSettingsMap
HdCyclesRenderDelegate::GetRenderSettingsMap() const
{
    return _settingsMap;
}

HdResourceRegistrySharedPtr
HdCyclesRenderDelegate::GetResourceRegistry() const
{
    return m_resourceRegistry;
}

HdRenderPassSharedPtr
HdCyclesRenderDelegate::CreateRenderPass(HdRenderIndex* index,
                                         HdRprimCollection const& collection)
{
    HdRenderPassSharedPtr xx = HdRenderPassSharedPtr(
        new HdCyclesRenderPass(this, index, collection));
    m_renderPass = (HdCyclesRenderPass*)xx.get();
    return xx;
}

void
HdCyclesRenderDelegate::CommitResources(HdChangeTracker* tracker)
{
    if (!m_hasStarted) {
        m_renderParam->StartRender();
        m_hasStarted = true;
    }

    m_renderParam->CommitResources();
}

HdRprim*
HdCyclesRenderDelegate::CreateRprim(TfToken const& typeId,
                                    SdfPath const& rprimId,
                                    SdfPath const& instancerId)
{
    if (typeId == HdPrimTypeTokens->mesh) {
        return new HdCyclesMesh(rprimId, instancerId, this);
    } else if (typeId == HdPrimTypeTokens->basisCurves) {
        return new HdCyclesBasisCurves(rprimId, instancerId, this);
    } else if (typeId == HdPrimTypeTokens->points) {
        return new HdCyclesPoints(rprimId, instancerId, this);
    } else {
        TF_CODING_ERROR("Unknown Rprim type=%s id=%s", typeId.GetText(),
                        rprimId.GetText());
    }
    return nullptr;
}

void
HdCyclesRenderDelegate::DestroyRprim(HdRprim* rPrim)
{
    if (rPrim)
        delete rPrim;
}

HdSprim*
HdCyclesRenderDelegate::CreateSprim(TfToken const& typeId,
                                    SdfPath const& sprimId)
{
    if (typeId == HdPrimTypeTokens->camera) {
        return new HdCyclesCamera(sprimId, this);
    }
    if (typeId == HdPrimTypeTokens->material) {
        return new HdCyclesMaterial(sprimId, this);
    }
    if (typeId == HdPrimTypeTokens->distantLight
        || typeId == HdPrimTypeTokens->domeLight
        || typeId == HdPrimTypeTokens->rectLight
        || typeId == HdPrimTypeTokens->diskLight
        || typeId == HdPrimTypeTokens->cylinderLight
        || typeId == HdPrimTypeTokens->sphereLight) {
        return new HdCyclesLight(sprimId, typeId, this);
    }
    TF_CODING_ERROR("Unknown Sprim type=%s id=%s", typeId.GetText(),
                    sprimId.GetText());
    return nullptr;
}

HdSprim*
HdCyclesRenderDelegate::CreateFallbackSprim(TfToken const& typeId)
{
    if (typeId == HdPrimTypeTokens->camera) {
        return new HdCyclesCamera(SdfPath::EmptyPath(), this);
    } else if (typeId == HdPrimTypeTokens->material) {
        return new HdCyclesMaterial(SdfPath::EmptyPath(), this);
    } else if (typeId == HdPrimTypeTokens->distantLight
               || typeId == HdPrimTypeTokens->domeLight
               || typeId == HdPrimTypeTokens->rectLight
               || typeId == HdPrimTypeTokens->diskLight
               || typeId == HdPrimTypeTokens->cylinderLight
               || typeId == HdPrimTypeTokens->sphereLight) {
        return new HdCyclesLight(SdfPath::EmptyPath(), typeId, this);
    }
    TF_CODING_ERROR("Creating unknown fallback sprim type=%s",
                    typeId.GetText());
    return nullptr;
}

void
HdCyclesRenderDelegate::DestroySprim(HdSprim* sPrim)
{
    if (sPrim)
        delete sPrim;
}

HdBprim*
HdCyclesRenderDelegate::CreateBprim(TfToken const& typeId,
                                    SdfPath const& bprimId)
{
    if (typeId == HdPrimTypeTokens->renderBuffer) {
        return new HdCyclesRenderBuffer(bprimId);
    }
    TF_CODING_ERROR("Unknown Bprim type=%s id=%s", typeId.GetText(),
                    bprimId.GetText());
    return nullptr;
}

HdBprim*
HdCyclesRenderDelegate::CreateFallbackBprim(TfToken const& typeId)
{
    if (typeId == HdPrimTypeTokens->renderBuffer) {
        return new HdCyclesRenderBuffer(SdfPath());
    }

    TF_CODING_ERROR("Creating unknown fallback bprim type=%s",
                    typeId.GetText());
    return nullptr;
}

void
HdCyclesRenderDelegate::DestroyBprim(HdBprim* bPrim)
{
    if (bPrim)
        delete bPrim;
}

HdInstancer*
HdCyclesRenderDelegate::CreateInstancer(HdSceneDelegate* delegate,
                                        SdfPath const& id,
                                        SdfPath const& instancerId)
{
    return new HdCyclesInstancer(delegate, id, instancerId);
}

void
HdCyclesRenderDelegate::DestroyInstancer(HdInstancer* instancer)
{
    delete instancer;
}

HdRenderParam*
HdCyclesRenderDelegate::GetRenderParam() const
{
    return m_renderParam.get();
}

HdCyclesRenderParam*
HdCyclesRenderDelegate::GetCyclesRenderParam() const
{
    return m_renderParam.get();
}

HdAovDescriptor
HdCyclesRenderDelegate::GetDefaultAovDescriptor(TfToken const& name) const
{
    if (name == HdAovTokens->color) {
        HdFormat colorFormat = GetCyclesRenderParam()
                                       ->GetCyclesSession()
                                       ->params.display_buffer_linear
                                   ? HdFormatFloat16Vec4
                                   : HdFormatUNorm8Vec4;
        return HdAovDescriptor(colorFormat, false, VtValue(GfVec4f(0.0f)));
    } else if (name == HdAovTokens->depth) {
        return HdAovDescriptor(HdFormatFloat32, false, VtValue(1.0f));
    } else if (name == HdAovTokens->primId || name == HdAovTokens->instanceId
               || name == HdAovTokens->elementId) {
        return HdAovDescriptor(HdFormatInt32, false, VtValue(-1));
    }

    return HdAovDescriptor();
}

TfToken
HdCyclesRenderDelegate::GetMaterialNetworkSelector() const
{
    return _tokens->cycles;
}

TfToken
HdCyclesRenderDelegate::GetMaterialBindingPurpose() const
{
    return HdTokens->full;
}

bool
HdCyclesRenderDelegate::IsPauseSupported() const
{
    return true;
}

bool
HdCyclesRenderDelegate::Pause()
{
    m_renderParam->PauseRender();
    return true;
}

bool
HdCyclesRenderDelegate::Resume()
{
    m_renderParam->ResumeRender();
    return true;
}

PXR_NAMESPACE_CLOSE_SCOPE
