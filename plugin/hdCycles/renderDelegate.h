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
#include "resourceRegistry.h"

#include <pxr/base/tf/diagnosticMgr.h>
#include <pxr/base/tf/staticTokens.h>
#include <pxr/imaging/hd/renderDelegate.h>
#include <pxr/pxr.h>

PXR_NAMESPACE_OPEN_SCOPE

class HdCyclesRenderParam;
class HdCyclesRenderPass;
class HdCyclesRenderDelegate;

// clang-format off
#ifdef __GNUC__
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-variable"
#endif
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
#ifdef __GNUC__
#pragma GCC diagnostic pop
#endif

#define HDCYCLES_INTEGRATOR_TOKENS  \
    (BranchedPathTracing)           \
    (PathTracing)

TF_DECLARE_PUBLIC_TOKENS(HdCyclesIntegratorTokens,
    HDCYCLES_INTEGRATOR_TOKENS
);

#define HDCYCLES_AOV_TOKENS \
    (UV)                    \
    (Vector)                \
    (IndexMA)               \
                            \
    (DiffDir)               \
    (GlossDir)              \
    (TransDir)              \
    (VolumeDir)             \
                            \
    (DiffInd)               \
    (GlossInd)              \
    (TransInd)              \
    (VolumeInd)             \
                            \
    (DiffCol)               \
    (GlossCol)              \
    (TransCol)              \
    (VolumeCol)             \
                            \
    (Mist)                  \
    (Emit)                  \
    (Env)                   \
    (AO)                    \
    (Shadow)                \
                            \
    (CryptoObject)          \
    (CryptoMaterial)        \
    (CryptoAsset)           \
                            \
    (AOVC)                  \
    (AOVV)                  \
                            \
    (P)                     \
    (Pref)                  \
    (Ngn)                   \
    (RenderTime)            \
    (SampleCount)

TF_DECLARE_PUBLIC_TOKENS(HdCyclesAovTokens,
    HDCYCLES_AOV_TOKENS
);

// clang-format on

///
/// Issues errors messages to specified ostream
///
class HdCyclesDiagnosticDelegate : public TfDiagnosticMgr::Delegate {
public:
    explicit HdCyclesDiagnosticDelegate(std::ostream& os);
    ~HdCyclesDiagnosticDelegate() override;

    void IssueError(const TfError& err) override;
    void IssueFatalError(const TfCallContext& context, const std::string& msg) override;
    void IssueStatus(const TfStatus& status) override {}
    void IssueWarning(const TfWarning& warning) override {}

private:
    void IssueDiagnosticBase(const TfDiagnosticBase& d);
    void IssueMessage(const std::string& message) { _os << message << '\n'; }

    std::ostream& _os;
};

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
    HdCyclesRenderDelegate();
    HdCyclesRenderDelegate(HdRenderSettingsMap const& settingsMap);
    ~HdCyclesRenderDelegate() override;

    HdRenderParam* GetRenderParam() const override;
    HdCyclesRenderParam* GetCyclesRenderParam() const;

    /// -- Supported types
    virtual const TfTokenVector& GetSupportedRprimTypes() const override;
    virtual const TfTokenVector& GetSupportedSprimTypes() const override;
    virtual const TfTokenVector& GetSupportedBprimTypes() const override;

    bool IsPauseSupported() const override;

    bool Pause() override;
    bool Resume() override;

    void _InitializeCyclesRenderSettings();

    void SetRenderSetting(const TfToken& key, const VtValue& value) override;

    HdRenderSettingDescriptorList GetRenderSettingDescriptors() const override;

    virtual HdRenderSettingsMap GetRenderSettingsMap() const;

    virtual HdResourceRegistrySharedPtr GetResourceRegistry() const override;

    // Render Pass and State
    HdRenderPassSharedPtr CreateRenderPass(HdRenderIndex* index, HdRprimCollection const& collection) override;
    HdRenderPassStateSharedPtr CreateRenderPassState() const override;

    // Prims
    HdInstancer* CreateInstancer(HdSceneDelegate* delegate, SdfPath const& id, SdfPath const& instancerId) override;
    void DestroyInstancer(HdInstancer* instancer) override;

    HdRprim* CreateRprim(TfToken const& typeId, SdfPath const& rprimId, SdfPath const& instancerId) override;
    void DestroyRprim(HdRprim* rPrim) override;

    HdSprim* CreateSprim(TfToken const& typeId, SdfPath const& sprimId) override;
    HdSprim* CreateFallbackSprim(TfToken const& typeId) override;
    void DestroySprim(HdSprim* sprim) override;

    HdBprim* CreateBprim(TfToken const& typeId, SdfPath const& bprimId) override;
    HdBprim* CreateFallbackBprim(TfToken const& typeId) override;
    void DestroyBprim(HdBprim* bprim) override;

    virtual HdAovDescriptor GetDefaultAovDescriptor(TfToken const& name) const override;

    void CommitResources(HdChangeTracker* tracker) override;

    TfToken GetMaterialNetworkSelector() const override;
    virtual TfToken GetMaterialBindingPurpose() const override;

    virtual VtDictionary GetRenderStats() const override;

private:
    static const TfTokenVector SUPPORTED_RPRIM_TYPES;
    static const TfTokenVector SUPPORTED_SPRIM_TYPES;
    static const TfTokenVector SUPPORTED_BPRIM_TYPES;

    // This class does not support copying.
    HdCyclesRenderDelegate(const HdCyclesRenderDelegate&) = delete;
    HdCyclesRenderDelegate& operator=(const HdCyclesRenderDelegate&) = delete;

    void _Initialize(HdRenderSettingsMap const& settingsMap);

    HdCyclesRenderPass* m_renderPass;
    HdRenderSettingDescriptorList m_settingDescriptors;

    std::unique_ptr<HdCyclesRenderParam> m_renderParam;
    bool m_hasStarted;

    HdCyclesResourceRegistrySharedPtr m_resourceRegistry;

    ///
    /// Auto add/remove cycles diagnostic delegate
    ///
    class HdCyclesDiagnosticDelegateHolder {
    public:
        HdCyclesDiagnosticDelegateHolder();
        ~HdCyclesDiagnosticDelegateHolder();

    private:
        std::unique_ptr<HdCyclesDiagnosticDelegate> _delegate;
    };

    HdCyclesDiagnosticDelegateHolder _diagnostic_holder;
};

PXR_NAMESPACE_CLOSE_SCOPE

#endif  // HD_CYCLES_RENDER_DELEGATE_H
