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

#include <render/background.h>
#include <render/film.h>
#include <render/integrator.h>

#include <pxr/base/gf/api.h>
#include <pxr/base/gf/vec2i.h>
#include <pxr/base/tf/getenv.h>
#include <pxr/base/vt/api.h>
#include <pxr/imaging/hd/camera.h>
#include <pxr/imaging/hd/extComputation.h>
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
    HdPrimTypeTokens->extComputation,
};

const TfTokenVector HdCyclesRenderDelegate::SUPPORTED_BPRIM_TYPES = { 
    HdPrimTypeTokens->renderBuffer
};

// clang-format on

HdCyclesRenderDelegate::HdCyclesRenderDelegate()
    : HdRenderDelegate()
    , m_hasStarted(false)
{
    _Initialize({});
}

HdCyclesRenderDelegate::HdCyclesRenderDelegate(
    HdRenderSettingsMap const& settingsMap)
    : HdRenderDelegate(settingsMap)
    , m_hasStarted(false)
{
    _Initialize(settingsMap);
}

void
HdCyclesRenderDelegate::_Initialize(HdRenderSettingsMap const& settingsMap)
{
    // -- Initialize Render Param (Core cycles wrapper)
    m_renderParam.reset(new HdCyclesRenderParam());

    if (!m_renderParam->Initialize(settingsMap))
        return;

    // -- Initialize Render Delegate components

    m_resourceRegistry.reset(new HdResourceRegistry());
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
    // TODO: Undecided how to approach these
    /* m_settingDescriptors.push_back({ std::string("Exposure"),
                                     usdCyclesTokens->cyclesFilmExposure,
                                     VtValue(config.exposure.value) });*/
    /*

    m_settingDescriptors.push_back({ std::string("Samples"),
                                     usdCyclesTokens->cyclesSamples,
                                     VtValue(config.exposure.) });*/

#endif
}

void
HdCyclesRenderDelegate::SetRenderSetting(const TfToken& key,
                                         const VtValue& value)
{
    HdRenderDelegate::SetRenderSetting(key, value);
    m_renderParam->SetRenderSetting(key, value);
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
    if (typeId == HdPrimTypeTokens->extComputation) {
        return new HdExtComputation(sprimId);
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
    }else if (typeId == HdPrimTypeTokens->extComputation) {
        return new HdExtComputation(SdfPath::EmptyPath());
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
