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

#ifndef HD_CYCLES_RENDER_DELEGATE_H
#define HD_CYCLES_RENDER_DELEGATE_H

#include "api.h"

#include <pxr/base/tf/staticTokens.h>
#include <pxr/imaging/hd/renderDelegate.h>
#include <pxr/imaging/hd/resourceRegistry.h>
#include <pxr/pxr.h>

PXR_NAMESPACE_OPEN_SCOPE

class HdCyclesRenderParam;
class HdCyclesRenderPass;
class HdCyclesRenderDelegate;

// clang-format off
TF_DEFINE_PRIVATE_TOKENS(HdCyclesRenderSettingsTokens,
    ((useDefaultBackground, "useDefaultBackground"))
    ((device, "device"))
    ((CPU, "CPU"))
    ((GPU, "GPU"))
    ((experimental, "experimental"))
    ((samples, "samples"))
    ((integrator, "integrator"))
    ((integratorMethod, "integratorMethod"))
    ((integratorName, "ci:integrator:name"))
    ((integratorPath, "ci:integrator:path"))
    ((integratorBranchedPath, "ci:integrator:branchedPath"))
    ((threads, "threads"))
    ((pixelSize, "pixelSize"))
    ((seed, "seed"))
    ((pattern, "pattern"))
    ((squareSamples,"squareSamples"))
    ((tileSize, "tileSize"))
    ((startResolution, "startResolution"))
    ((lightPathsTotal, "lightPaths:total"))
    ((lightPathsDiffuse,"lightPaths:diffuse"))
    ((lightPathsGlossy, "lightPaths:glossy"))
    ((lightPathsTransmission, "lightPaths:transmission"))
    ((lightPathsAO, "lightPaths:ambientOcclussion"))
    ((lightPathsMeshLight, "lightPaths:meshLight"))
    ((lightPathsSubsurface, "lightPaths:subsurface"))
    ((lightPathsVolume, "lightPaths:volume"))
    ((volumeStepSize, "volume:stepSize"))
    ((volumeMaxSteps, "volume:maxSteps"))
    ((hairShape, "hair:shape"))
    ((hairShapeThick, "hair:shape:thick"))
    ((hairShapeRibbons, "hair:shape:ribbons"))
    ((useMotionBlur, "useMotionBlur"))
    ((motionSteps, "motionSteps"))
    ((motionBlurPosition, "motionBlur:position"))
    ((motionBlurPositionStart, "motionBlur:position:start"))
    ((motionBlurPositionCenter, "motionBlur:position:center"))
    ((motionBlurPositionEnd, "motionBlur:position:end"))
    ((useRollingShutter, "useRollingShutter"))
    ((rollingShutterDuration, "rollingShutterDuration"))
    ((exposure, "exposure"))
    ((pixelFilter, "pixelFilter"))
    ((pixelFilterBlackmanHarris, "pixelFilter:blackmanHarris"))
    ((pixelFilterBox, "pixelFilter:box"))
    ((pixelFilterGaussian, "pixelFilter:gaussian"))
);

#define HDCYCLES_INTEGRATOR_TOKENS  \
    (BranchedPathTracing)           \
    (PathTracing)

TF_DECLARE_PUBLIC_TOKENS(HdCyclesIntegratorTokens, 
    HDCYCLES_API,
    HDCYCLES_INTEGRATOR_TOKENS
);


#define HDCYCLES_AOV_TOKENS \
    (Vector)                \
    (IndexMA)               \
                            \
    (DiffDir)               \
    (GlossDir)              \
    (TransDir)              \
    (VolumeDir)             \
                            \
    (Emit)                  \
    (Env)                   \
    (AO)                    \
    (Shadow)

TF_DECLARE_PUBLIC_TOKENS(HdCyclesAovTokens, 
    HDCYCLES_API, 
    HDCYCLES_AOV_TOKENS
);

// clang-format on

/**
 * @brief Represents the core interactions between Cycles and Hydra.
 * Responsible for creating and deleting scene primitives, and
 * renderpasses.
 * 
 */
class HdCyclesRenderDelegate : public HdRenderDelegate {
public:
    /**
     * @brief Render delegate constructor.
     * 
     */
    HDCYCLES_API HdCyclesRenderDelegate();
    HDCYCLES_API ~HdCyclesRenderDelegate() override;

    HDCYCLES_API HdRenderParam* GetRenderParam() const override;
    HDCYCLES_API HdCyclesRenderParam* GetCyclesRenderParam() const;

    /// -- Supported types
    HDCYCLES_API virtual const TfTokenVector&
    GetSupportedRprimTypes() const override;
    HDCYCLES_API virtual const TfTokenVector&
    GetSupportedSprimTypes() const override;
    HDCYCLES_API virtual const TfTokenVector&
    GetSupportedBprimTypes() const override;

    HDCYCLES_API bool IsPauseSupported() const override;

    HDCYCLES_API bool Pause() override;
    HDCYCLES_API bool Resume() override;

    HDCYCLES_API void _InitializeCyclesRenderSettings();

    HDCYCLES_API void SetRenderSetting(const TfToken& key,
                                       const VtValue& value) override;

    HDCYCLES_API HdRenderSettingDescriptorList
    GetRenderSettingDescriptors() const override;

    HDCYCLES_API virtual HdResourceRegistrySharedPtr
    GetResourceRegistry() const override;

    // Prims
    HDCYCLES_API virtual HdRenderPassSharedPtr
    CreateRenderPass(HdRenderIndex* index,
                     HdRprimCollection const& collection) override;

    HDCYCLES_API HdInstancer*
    CreateInstancer(HdSceneDelegate* delegate, SdfPath const& id,
                    SdfPath const& instancerId) override;
    HDCYCLES_API void DestroyInstancer(HdInstancer* instancer) override;

    HDCYCLES_API HdRprim* CreateRprim(TfToken const& typeId,
                                      SdfPath const& rprimId,
                                      SdfPath const& instancerId) override;
    HDCYCLES_API void DestroyRprim(HdRprim* rPrim) override;

    HDCYCLES_API HdSprim* CreateSprim(TfToken const& typeId,
                                      SdfPath const& sprimId) override;
    HDCYCLES_API HdSprim* CreateFallbackSprim(TfToken const& typeId) override;
    HDCYCLES_API void DestroySprim(HdSprim* sprim) override;

    HDCYCLES_API HdBprim* CreateBprim(TfToken const& typeId,
                                      SdfPath const& bprimId) override;
    HDCYCLES_API HdBprim* CreateFallbackBprim(TfToken const& typeId) override;
    HDCYCLES_API void DestroyBprim(HdBprim* bprim) override;

    HDCYCLES_API virtual HdAovDescriptor
    GetDefaultAovDescriptor(TfToken const& name) const override;

    HDCYCLES_API void CommitResources(HdChangeTracker* tracker) override;

    HDCYCLES_API TfToken GetMaterialNetworkSelector() const override;
    HDCYCLES_API virtual TfToken GetMaterialBindingPurpose() const override;

protected:
    static const TfTokenVector SUPPORTED_RPRIM_TYPES;
    static const TfTokenVector SUPPORTED_SPRIM_TYPES;
    static const TfTokenVector SUPPORTED_BPRIM_TYPES;

    void _SetRenderSetting(const TfToken& key, const VtValue& value);

private:
    // This class does not support copying.
    HdCyclesRenderDelegate(const HdCyclesRenderDelegate&) = delete;
    HdCyclesRenderDelegate& operator=(const HdCyclesRenderDelegate&) = delete;

    void _Initialize();

protected:  // data
    HdCyclesRenderPass* m_renderPass;
    HdRenderSettingDescriptorList m_settingDescriptors;
    HdResourceRegistrySharedPtr m_resourceRegistry;

    std::unique_ptr<HdCyclesRenderParam> m_renderParam;
    bool m_hasStarted;
};

PXR_NAMESPACE_CLOSE_SCOPE

#endif  // HD_CYCLES_RENDER_DELEGATE_H
