//  Copyright 2021 Tangent Animation
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


#ifndef HDBLACKBIRD_RPRIM_H
#define HDBLACKBIRD_RPRIM_H

#include <usdCycles/tokens.h>

#include <pxr/imaging/hd/rprim.h>

#include <render/object.h>

#include <map>

PXR_NAMESPACE_OPEN_SCOPE

template<typename T> class HdBbRPrim : public T {
public:
    static_assert(!std::is_base_of<T, HdRprim>::value, "HdBbRPrim must wrap HdRPrim class");

    using HdPrimvarDescriptorMap = std::map<HdInterpolation, HdPrimvarDescriptorVector>;

protected:
    HdBbRPrim(SdfPath const& id, SdfPath const& instancerId)
        : T { id, instancerId }
        , m_cyclesObject { nullptr }
        , m_visibilityFlags { ccl::PATH_RAY_ALL_VISIBILITY }
        , m_motionBlur { true }
        , m_motionTransformSteps { 3 }
        , m_motionDeformSteps { 3 }
    {
    }

    HdPrimvarDescriptorMap GetPrimvarDescriptorMap(HdSceneDelegate* sceneDelegate) const
    {
        SdfPath const& id = T::GetId();
        return {
            { HdInterpolationFaceVarying, sceneDelegate->GetPrimvarDescriptors(id, HdInterpolationFaceVarying) },
            { HdInterpolationVertex, sceneDelegate->GetPrimvarDescriptors(id, HdInterpolationVertex) },
            { HdInterpolationConstant, sceneDelegate->GetPrimvarDescriptors(id, HdInterpolationConstant) },
            { HdInterpolationUniform, sceneDelegate->GetPrimvarDescriptors(id, HdInterpolationUniform) },
        };
    }

    void GetObjectPrimvars(HdPrimvarDescriptorMap const& descriptor_map, HdSceneDelegate* sceneDelegate,
                           const HdDirtyBits* dirtyBits)
    {
        assert(m_cyclesObject != nullptr);

        const SdfPath& id = T::GetId();

        // visibility
        m_visibilityFlags = 0;
        bool visCamera = true;
        bool visDiffuse = true;
        bool visGlossy = true;
        bool visScatter = true;
        bool visShadow = true;
        bool visTransmission = true;

        // motion blur
        m_motionBlur = true;
        m_motionTransformSteps = 3;
        m_motionDeformSteps = 3;

        // pass and names
        m_cyclesObject->set_is_shadow_catcher(false);
        m_cyclesObject->set_pass_id(0);
        m_cyclesObject->set_use_holdout(false);
        m_cyclesObject->set_asset_name(ccl::ustring());
        m_cyclesObject->set_lightgroup(ccl::ustring());

        for (auto& descriptor_interpolation : descriptor_map) {
            for (auto& pv : descriptor_interpolation.second) {
                if (!HdChangeTracker::IsPrimvarDirty(*dirtyBits, id, pv.name)) {
                    continue;
                }

                const std::string primvar_name = std::string { "primvars:" } + pv.name.GetString();

                // visibility
                if (primvar_name == usdCyclesTokens->primvarsCyclesObjectVisibilityCamera) {
                    VtValue value = T::GetPrimvar(sceneDelegate, pv.name);
                    visCamera = value.Get<bool>();
                    continue;
                }

                if (primvar_name == usdCyclesTokens->primvarsCyclesObjectVisibilityDiffuse) {
                    VtValue value = T::GetPrimvar(sceneDelegate, pv.name);
                    visDiffuse = value.Get<bool>();
                    continue;
                }

                if (primvar_name == usdCyclesTokens->primvarsCyclesObjectVisibilityGlossy) {
                    VtValue value = T::GetPrimvar(sceneDelegate, pv.name);
                    visGlossy = value.Get<bool>();
                    continue;
                }

                if (primvar_name == usdCyclesTokens->primvarsCyclesObjectVisibilityScatter) {
                    VtValue value = T::GetPrimvar(sceneDelegate, pv.name);
                    visScatter = value.Get<bool>();
                    continue;
                }

                if (primvar_name == usdCyclesTokens->primvarsCyclesObjectVisibilityShadow) {
                    VtValue value = T::GetPrimvar(sceneDelegate, pv.name);
                    visShadow = value.Get<bool>();
                    continue;
                }

                if (primvar_name == usdCyclesTokens->primvarsCyclesObjectVisibilityTransmission) {
                    VtValue value = T::GetPrimvar(sceneDelegate, pv.name);
                    visTransmission = value.Get<bool>();
                    continue;
                }

                // motion blur
                if (primvar_name == usdCyclesTokens->primvarsCyclesObjectMblur) {
                    VtValue value = T::GetPrimvar(sceneDelegate, pv.name);
                    m_motionBlur = value.Get<bool>();
                    continue;
                }

                if (primvar_name == usdCyclesTokens->primvarsCyclesObjectTransformSamples) {
                    VtValue value = T::GetPrimvar(sceneDelegate, pv.name);
                    m_motionTransformSteps = value.Get<int>();
                    continue;
                }

                if (primvar_name == usdCyclesTokens->primvarsCyclesObjectDeformSamples) {
                    VtValue value = T::GetPrimvar(sceneDelegate, pv.name);
                    m_motionDeformSteps = value.Get<int>();
                    continue;
                }

                // names
                if (primvar_name == usdCyclesTokens->primvarsCyclesObjectAsset_name) {
                    VtValue value = T::GetPrimvar(sceneDelegate, pv.name);
                    auto& assetName = value.Get<std::string>();
                    m_cyclesObject->set_asset_name(ccl::ustring(assetName));
                    continue;
                }

                if (primvar_name == usdCyclesTokens->primvarsCyclesObjectLightgroup) {
                    VtValue value = T::GetPrimvar(sceneDelegate, pv.name);
                    auto& lightGroup = value.Get<std::string>();
                    m_cyclesObject->set_lightgroup(ccl::ustring(lightGroup));
                    continue;
                }

                // pass
                if (primvar_name == usdCyclesTokens->primvarsCyclesObjectPass_id) {
                    VtValue value = T::GetPrimvar(sceneDelegate, pv.name);
                    m_cyclesObject->set_pass_id(value.Get<int>());
                    continue;
                }

                // shadows
                if (primvar_name == usdCyclesTokens->primvarsCyclesObjectIs_shadow_catcher) {
                    VtValue value = T::GetPrimvar(sceneDelegate, pv.name);
                    m_cyclesObject->set_is_shadow_catcher(value.Get<bool>());
                    continue;
                }

                if (primvar_name == usdCyclesTokens->primvarsCyclesObjectUse_holdout) {
                    VtValue value = T::GetPrimvar(sceneDelegate, pv.name);
                    m_cyclesObject->set_use_holdout(value.Get<bool>());
                    continue;
                }
            }
        }

        // build visibility flags
        m_visibilityFlags |= visCamera ? ccl::PATH_RAY_CAMERA : 0;
        m_visibilityFlags |= visDiffuse ? ccl::PATH_RAY_DIFFUSE : 0;
        m_visibilityFlags |= visGlossy ? ccl::PATH_RAY_GLOSSY : 0;
        m_visibilityFlags |= visScatter ? ccl::PATH_RAY_VOLUME_SCATTER : 0;
        m_visibilityFlags |= visShadow ? ccl::PATH_RAY_SHADOW : 0;
        m_visibilityFlags |= visTransmission ? ccl::PATH_RAY_TRANSMIT : 0;
    }

    void UpdateObject(ccl::Scene* scene, HdDirtyBits* dirtyBits, bool rebuildBvh)
    {
        assert(m_cyclesObject != nullptr);

        if (m_cyclesObject->get_geometry()) {
            m_cyclesObject->get_geometry()->tag_update(scene, rebuildBvh);
        }
        m_cyclesObject->set_visibility(T::IsVisible() ? m_visibilityFlags : 0);
        m_cyclesObject->tag_update(scene);

        // Mark visibility clean. When sync method is called object might be invisible. At that point we do not
        // need to trigger the topology and data generation. It can be postponed until visibility becomes on.
        // We need to manually mark visibility clean, but other flags remain dirty.
        if (!T::IsVisible()) {
            *dirtyBits &= ~HdChangeTracker::DirtyVisibility;
        }
    }

    ccl::Object* m_cyclesObject;

    unsigned int m_visibilityFlags;

    bool m_motionBlur;
    int m_motionTransformSteps;
    int m_motionDeformSteps;
};

PXR_NAMESPACE_CLOSE_SCOPE

#endif  //HDBLACKBIRD_RPRIM_H
