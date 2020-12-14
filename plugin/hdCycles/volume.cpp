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

#include "volume.h"

#include "openvdb_asset.h"

#include "config.h"
#include "material.h"
#include "renderParam.h"
#include "utils.h"

#include <render/object.h>
#include <render/scene.h>
#include <render/shader.h>

#include <render/image.h>
#include <render/mesh.h>
#include <render/object.h>

#include <util/util_hash.h>
#include <util/util_math_float3.h>

#include <pxr/base/gf/vec2f.h>
#include <pxr/base/gf/vec3f.h>
#include <pxr/base/gf/vec3i.h>
#include <pxr/imaging/hd/sceneDelegate.h>

#include <pxr/usd/sdf/assetPath.h>
#include <pxr/usd/sdf/path.h>

#include <iostream>

PXR_NAMESPACE_OPEN_SCOPE

// clang-format off
TF_DEFINE_PRIVATE_TOKENS(_tokens,
    (openvdbAsset)
    (filePath)
);
// clang-format on

HdCyclesVolume::HdCyclesVolume(SdfPath const& id, SdfPath const& instancerId,
                               HdCyclesRenderDelegate* a_renderDelegate)
    : HdVolume(id, instancerId)
    , m_cyclesObject(nullptr)
    , m_cyclesVolume(nullptr)
    , m_renderDelegate(a_renderDelegate)
{
    static const HdCyclesConfig& config = HdCyclesConfig::GetInstance();
    m_useMotionBlur                     = config.enable_motion_blur.eval(m_useMotionBlur, true);

    m_cyclesObject = _CreateObject();
    m_renderDelegate->GetCyclesRenderParam()->AddObject(m_cyclesObject);

    m_cyclesVolume = _CreateVolume();
    m_renderDelegate->GetCyclesRenderParam()->AddMesh(m_cyclesVolume);

    m_cyclesObject->geometry = m_cyclesVolume;
}

HdCyclesVolume::~HdCyclesVolume()
{
    if (m_cyclesObject) {
        m_renderDelegate->GetCyclesRenderParam()->RemoveObject(m_cyclesObject);
        delete m_cyclesObject;
    }

    if (m_cyclesVolume) {
        m_renderDelegate->GetCyclesRenderParam()->RemoveMesh(m_cyclesVolume);
        delete m_cyclesVolume;
    }
}

void
HdCyclesVolume::_InitRepr(TfToken const& reprToken, HdDirtyBits* dirtyBits)
{
}

HdDirtyBits
HdCyclesVolume::_PropagateDirtyBits(HdDirtyBits bits) const
{
    return bits;
}

void
HdCyclesVolume::Finalize(HdRenderParam* renderParam)
{
}

ccl::Object*
HdCyclesVolume::_CreateObject()
{
    // Create container object
    ccl::Object* object = new ccl::Object();

    object->visibility = ccl::PATH_RAY_ALL_VISIBILITY;

    return object;
}

ccl::Mesh*
HdCyclesVolume::_CreateVolume()
{
    // Create container object
    ccl::Mesh* volume = new ccl::Mesh();

    volume->volume_clipping     = 0.001f;
    volume->volume_step_size    = 0.0f;
    volume->volume_object_space = true;

    return volume;
}

void
HdCyclesVolume::_PopulateVolume(const SdfPath& id, HdSceneDelegate* delegate,
                                ccl::Scene* scene)
{
#ifdef WITH_OPENVDB
    std::unordered_map<std::string, std::vector<TfToken>> openvdbs;
    std::unordered_map<std::string, std::vector<TfToken>> houVdbs;

    const auto fieldDescriptors = delegate->GetVolumeFieldDescriptors(id);
    for (const auto& field : fieldDescriptors) {
        auto* openvdbAsset = dynamic_cast<HdCyclesOpenvdbAsset*>(
            delegate->GetRenderIndex().GetBprim(_tokens->openvdbAsset,
                                                field.fieldId));

        if (openvdbAsset == nullptr) {
            continue;
        }

        const auto vv = delegate->Get(field.fieldId, _tokens->filePath);

        if (vv.IsHolding<SdfAssetPath>()) {
            const auto& assetPath = vv.UncheckedGet<SdfAssetPath>();
            auto path             = assetPath.GetResolvedPath();
            if (path.empty()) {
                path = assetPath.GetAssetPath();
            }

            auto& fields = openvdbs[path];
            if (std::find(fields.begin(), fields.end(), field.fieldName)
                == fields.end()) {
                fields.push_back(field.fieldName);

                ccl::ustring name = ccl::ustring(field.fieldName.GetString());
                ccl::ustring filepath = ccl::ustring(path);

                ccl::AttributeStandard std = ccl::ATTR_STD_NONE;

                if (name
                    == ccl::Attribute::standard_name(
                        ccl::ATTR_STD_VOLUME_DENSITY)) {
                    std = ccl::ATTR_STD_VOLUME_DENSITY;
                } else if (name
                           == ccl::Attribute::standard_name(
                               ccl::ATTR_STD_VOLUME_COLOR)) {
                    std = ccl::ATTR_STD_VOLUME_COLOR;
                } else if (name
                           == ccl::Attribute::standard_name(
                               ccl::ATTR_STD_VOLUME_FLAME)) {
                    std = ccl::ATTR_STD_VOLUME_FLAME;
                } else if (name
                           == ccl::Attribute::standard_name(
                               ccl::ATTR_STD_VOLUME_HEAT)) {
                    std = ccl::ATTR_STD_VOLUME_HEAT;
                } else if (name
                           == ccl::Attribute::standard_name(
                               ccl::ATTR_STD_VOLUME_TEMPERATURE)) {
                    std = ccl::ATTR_STD_VOLUME_TEMPERATURE;
                } else if (name
                           == ccl::Attribute::standard_name(
                               ccl::ATTR_STD_VOLUME_VELOCITY)) {
                    std = ccl::ATTR_STD_VOLUME_VELOCITY;
                }

                ccl::Attribute* attr = (std != ccl::ATTR_STD_NONE)
                                           ? m_cyclesVolume->attributes.add(std)
                                           : m_cyclesVolume->attributes.add(
                                               name, ccl::TypeDesc::TypeFloat,
                                               ccl::ATTR_ELEMENT_VOXEL);

                ccl::ImageLoader* loader
                    = new HdCyclesVolumeLoader(filepath.c_str(), name.c_str());
                ccl::ImageParams params;
                params.frame = 0.0f;

                attr->data_voxel() = scene->image_manager->add_image(loader,
                                                                     params);
            }
        }
    }
#endif
}

/* If the voxel attributes change, we need to rebuild the bounding mesh. */
static ccl::vector<int>
get_voxel_image_slots(ccl::Mesh* mesh)
{
    ccl::vector<int> slots;
    for (const ccl::Attribute& attr : mesh->attributes.attributes) {
        if (attr.element == ccl::ATTR_ELEMENT_VOXEL) {
            slots.push_back(attr.data_voxel().svm_slot());
        }
    }

    return slots;
}

void
HdCyclesVolume::Sync(HdSceneDelegate* sceneDelegate, HdRenderParam* renderParam,
                     HdDirtyBits* dirtyBits, TfToken const& reprSelector)
{
    SdfPath const& id = GetId();

    HdCyclesRenderParam* param = (HdCyclesRenderParam*)renderParam;

    ccl::Scene* scene = param->GetCyclesScene();

    HdCyclesPDPIMap pdpi;
    bool generate_new_curve = false;
    bool update_volumes     = false;

    ccl::vector<int> old_voxel_slots = get_voxel_image_slots(m_cyclesVolume);

    if (HdChangeTracker::IsTopologyDirty(*dirtyBits, id)) {
        m_cyclesVolume->clear();
        _PopulateVolume(id, sceneDelegate, scene);
        update_volumes = true;
    }

    if (*dirtyBits & HdChangeTracker::DirtyVisibility) {
        update_volumes = true;
        if (sceneDelegate->GetVisible(id)) {
            m_cyclesObject->visibility |= ccl::PATH_RAY_ALL_VISIBILITY;
        } else {
            m_cyclesObject->visibility &= ~ccl::PATH_RAY_ALL_VISIBILITY;
        }
    }

    if (*dirtyBits & HdChangeTracker::DirtyTransform) {
        m_transformSamples = HdCyclesSetTransform(m_cyclesObject, sceneDelegate,
                                                  id, m_useMotionBlur);

        update_volumes = true;
    }

    if (*dirtyBits & HdChangeTracker::DirtyPrimvar) {
        HdCyclesPopulatePrimvarDescsPerInterpolation(sceneDelegate, id, &pdpi);

        for (auto& primvarDescsEntry : pdpi) {
            for (auto& pv : primvarDescsEntry.second) {
                if (HdChangeTracker::IsPrimvarDirty(*dirtyBits, id, pv.name)) {
                    VtValue value = GetPrimvar(sceneDelegate, pv.name);
                    
                }
            }
        }
    }

    if (*dirtyBits & HdChangeTracker::DirtyMaterialId) {
        if (m_cyclesVolume) {
            // Add default shader
            const SdfPath& materialId = sceneDelegate->GetMaterialId(GetId());
            const HdCyclesMaterial* material
                = static_cast<const HdCyclesMaterial*>(
                    sceneDelegate->GetRenderIndex().GetSprim(
                        HdPrimTypeTokens->material, materialId));

            if (material && material->GetCyclesShader()) {
                m_usedShaders.push_back(material->GetCyclesShader());
                material->GetCyclesShader()->tag_update(scene);
            } else {
                m_usedShaders.push_back(scene->default_volume);
            }
            m_cyclesVolume->used_shaders = m_usedShaders;
            update_volumes               = true;
        }
    }

    if (update_volumes) {
        bool rebuild = (old_voxel_slots
                        != get_voxel_image_slots(m_cyclesVolume));

        m_cyclesVolume->tag_update(scene, rebuild);
        m_cyclesObject->tag_update(scene);

        param->Interrupt();
    }

    *dirtyBits = HdChangeTracker::Clean;
}

HdDirtyBits
HdCyclesVolume::GetInitialDirtyBitsMask() const
{
    return HdChangeTracker::DirtyTopology | HdChangeTracker::DirtyPoints
           | HdChangeTracker::DirtyNormals | HdChangeTracker::DirtyWidths
           | HdChangeTracker::DirtyPrimvar | HdChangeTracker::DirtyTransform
           | HdChangeTracker::DirtyVisibility
           | HdChangeTracker::DirtyMaterialId;
}

bool
HdCyclesVolume::IsValid() const
{
    return true;
}

PXR_NAMESPACE_CLOSE_SCOPE