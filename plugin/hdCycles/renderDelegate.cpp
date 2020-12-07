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
#include "volume.h"
#include "openvdb_asset.h"

#include "renderBuffer.h"
#include "renderParam.h"
#include "renderPass.h"
#include "utils.h"

#include <render/integrator.h>

#include <boost/algorithm/string.hpp>

#include <pxr/base/gf/api.h>
#include <pxr/base/gf/vec2i.h>
#include <pxr/base/tf/getenv.h>
#include <pxr/base/vt/api.h>
#include <pxr/imaging/hd/camera.h>
#include <pxr/imaging/hd/tokens.h>

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
    HdPrimTypeTokens->volume,
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
    HdPrimTypeTokens->renderBuffer,
    _tokens->openvdbAsset,
};

// clang-format on

HdCyclesRenderDelegate::HdCyclesRenderDelegate()
    : m_hasStarted(false)
{
    _Initialize();
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

    m_settingDescriptors.push_back(
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
          VtValue(config.volume_samples) });
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

    if (key == HdCyclesRenderSettingsTokens->useMotionBlur) {
        _CheckForBoolValue(_value, [&](const bool b) {
            if (m_renderParam->GetUseMotionBlur() != b)
                m_renderParam->SetUseMotionBlur(b);
        });
    } else if (key == HdCyclesRenderSettingsTokens->motionSteps) {
        _CheckForIntValue(_value, [&](const int i) {
            if (m_renderParam->GetMotionSteps() != i)
                m_renderParam->SetMotionSteps(i);
        });
    } else if (key == HdCyclesRenderSettingsTokens->experimental) {
        _CheckForBoolValue(_value, [&](const bool b) {
            if (m_renderParam->GetUseExperimental() != b)
                m_renderParam->SetUseExperimental(b);
        });
    } else if (key == HdCyclesRenderSettingsTokens->samples) {
        _CheckForIntValue(_value, [&](const int i) {
            if (m_renderParam->GetMaxSamples() != i)
                m_renderParam->SetMaxSamples(i);
        });
    } else if (key == HdCyclesRenderSettingsTokens->threads) {
        _CheckForIntValue(_value, [&](const int i) {
            if (m_renderParam->GetNumThreads() != i)
                m_renderParam->SetNumThreads(i);
        });
    } else if (key == HdCyclesRenderSettingsTokens->tileSize) {
        _CheckForVec2iValue(_value, [&](const pxr::GfVec2i v) {
            if (m_renderParam->GetTileSize() != v)
                m_renderParam->SetTileSize(v);
        });
    } else if (key == HdCyclesRenderSettingsTokens->pixelSize) {
        _CheckForIntValue(_value, [&](const int i) {
            if (m_renderParam->GetPixelSize() != i)
                m_renderParam->GetCyclesSession()->params.pixel_size = i;
        });
    } else if (key == HdCyclesRenderSettingsTokens->startResolution) {
        _CheckForIntValue(_value, [&](const int i) {
            if (m_renderParam->GetStartResolution() != i)
                m_renderParam->GetCyclesSession()->params.start_resolution = i;
        });
    } else if (key == HdCyclesRenderSettingsTokens->device) {
        _CheckForStringValue(_value, [&](const std::string s) {
            if (m_renderParam->GetDeviceTypeName() != s)
                m_renderParam->SetDeviceType(s);
        });
    } else if (key == HdCyclesRenderSettingsTokens->integratorMethod) {
        _CheckForStringValue(_value, [&](const std::string s) {
            ccl::Integrator::Method m = ccl::Integrator::PATH;

            if (boost::iequals(s, "BRANCHED_PATH")) {
                m = ccl::Integrator::BRANCHED_PATH;
            }

            if (integrator->method != m) {
                integrator->method = m;
                integrator_updated = true;
            }
        });
    } else if (key == HdCyclesRenderSettingsTokens->lightPathsDiffuse) {
        _CheckForIntValue(_value, [&](const int i) {
            if (integrator->diffuse_samples != i) {
                integrator->diffuse_samples = i;
                integrator_updated          = true;
            }
        });
    } else if (key == HdCyclesRenderSettingsTokens->lightPathsGlossy) {
        _CheckForIntValue(_value, [&](const int i) {
            if (integrator->glossy_samples != i) {
                integrator->glossy_samples = i;
                integrator_updated         = true;
            }
        });
    } else if (key == HdCyclesRenderSettingsTokens->lightPathsTransmission) {
        _CheckForIntValue(_value, [&](const int i) {
            if (integrator->transmission_samples != i) {
                integrator->transmission_samples = i;
                integrator_updated               = true;
            }
        });
    } else if (key == HdCyclesRenderSettingsTokens->lightPathsAO) {
        _CheckForIntValue(_value, [&](const int i) {
            if (integrator->ao_samples != i) {
                integrator->ao_samples = i;
                integrator_updated     = true;
            }
        });
    } else if (key == HdCyclesRenderSettingsTokens->lightPathsMeshLight) {
        _CheckForIntValue(_value, [&](const int i) {
            if (integrator->mesh_light_samples != i) {
                integrator->mesh_light_samples = i;
                integrator_updated             = true;
            }
        });
    } else if (key == HdCyclesRenderSettingsTokens->lightPathsSubsurface) {
        _CheckForIntValue(_value, [&](const int i) {
            if (integrator->subsurface_samples != i) {
                integrator->subsurface_samples = i;
                integrator_updated             = true;
            }
        });
    } else if (key == HdCyclesRenderSettingsTokens->lightPathsVolume) {
        _CheckForIntValue(_value, [&](const int i) {
            if (integrator->volume_samples != i) {
                integrator->volume_samples = i;
                integrator_updated         = true;
            }
        });
    } else if (key == HdCyclesRenderSettingsTokens->exposure) {
        _CheckForFloatValue(_value, [&](float f) {
            if (m_renderParam->GetExposure() != f)
                m_renderParam->SetExposure(f);
        });
        _CheckForDoubleValue(_value, [&](double d) {
            if (m_renderParam->GetExposure() != d)
                m_renderParam->SetExposure((float)d);
        });
    }

    //if (integrator_updated) {
   //     integrator->tag_update(m_renderParam->GetCyclesScene());
    //}
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
    } else if (typeId == HdPrimTypeTokens->volume) {
        return new HdCyclesVolume(rprimId, instancerId, this);
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
    if (typeId == _tokens->openvdbAsset) {
        return new HdCyclesOpenvdbAsset(this, bprimId);
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
    if (typeId == _tokens->openvdbAsset) {
        return new HdCyclesOpenvdbAsset(this, SdfPath());
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
