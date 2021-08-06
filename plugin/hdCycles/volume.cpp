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
#include "instancer.h"
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

#include <usdCycles/tokens.h>

PXR_NAMESPACE_OPEN_SCOPE

// clang-format off
TF_DEFINE_PRIVATE_TOKENS(_tokens,
    (openvdbAsset)
    (filePath)
);
// clang-format on

HdCyclesVolume::HdCyclesVolume(SdfPath const& id, SdfPath const& instancerId, HdCyclesRenderDelegate* a_renderDelegate)
    : HdBbRPrim<HdVolume>(id, instancerId)
    , m_cyclesVolume(nullptr)
    , m_renderDelegate(a_renderDelegate)
{
    static const HdCyclesConfig& config = HdCyclesConfig::GetInstance();
    m_useMotionBlur = config.motion_blur.eval(m_useMotionBlur, true);

    m_cyclesObject = _CreateObject();
    m_renderDelegate->GetCyclesRenderParam()->AddObjectSafe(m_cyclesObject);

    m_cyclesVolume = _CreateVolume();
    m_renderDelegate->GetCyclesRenderParam()->AddGeometrySafe(m_cyclesVolume);

    m_cyclesObject->geometry = m_cyclesVolume;

    auto resource_registry = dynamic_cast<HdCyclesResourceRegistry*>(m_renderDelegate->GetResourceRegistry().get());
    HdInstance<HdCyclesObjectSourceSharedPtr> object_instance = resource_registry->GetObjectInstance(id);
    object_instance.SetValue(std::make_shared<HdCyclesObjectSource>(m_cyclesObject, id, true));
    m_object_source = object_instance.GetValue();
}

HdCyclesVolume::~HdCyclesVolume()
{
    if (m_cyclesObject) {
        m_renderDelegate->GetCyclesRenderParam()->RemoveObjectSafe(m_cyclesObject);
        delete m_cyclesObject;
    }

    if (m_cyclesVolume) {
        m_renderDelegate->GetCyclesRenderParam()->RemoveGeometrySafe(m_cyclesVolume);
        delete m_cyclesVolume;
    }

    for (auto instance : m_cyclesInstances) {
        if (instance) {
            m_renderDelegate->GetCyclesRenderParam()->RemoveObjectSafe(instance);
            delete instance;
        }
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
    object->velocity_scale = 1.0f;
    return object;
}

ccl::Mesh*
HdCyclesVolume::_CreateVolume()
{
    // Create container object
    ccl::Mesh* volume = new ccl::Mesh();

    volume->volume_clipping = 0.001f;
    volume->volume_step_size = 0.0f;
    volume->volume_object_space = true;

    return volume;
}

void
HdCyclesVolume::_PopulateVolume(const SdfPath& id, HdSceneDelegate* delegate, ccl::Scene* scene)
{
#ifdef WITH_OPENVDB
    std::unordered_map<std::string, std::vector<TfToken>> field_map;

    const auto fieldDescriptors = delegate->GetVolumeFieldDescriptors(id);
    for (const auto& field : fieldDescriptors) {
        auto* openvdbAsset = dynamic_cast<HdCyclesOpenvdbAsset*>(
            delegate->GetRenderIndex().GetBprim(_tokens->openvdbAsset, field.fieldId));

        if (openvdbAsset == nullptr) {
            continue;
        }

        const auto vv = delegate->Get(field.fieldId, _tokens->filePath);

        if (vv.IsHolding<SdfAssetPath>()) {
            const auto& assetPath = vv.UncheckedGet<SdfAssetPath>();
            auto path = assetPath.GetResolvedPath();
            if (path.empty()) {
                path = assetPath.GetAssetPath();
            }

            auto& fields = field_map[path];
            if (std::find(fields.begin(), fields.end(), field.fieldName) == fields.end()) {
                fields.push_back(field.fieldName);

                ccl::ustring name = ccl::ustring(field.fieldName.GetString());
                ccl::ustring filepath = ccl::ustring(path);

                ccl::AttributeStandard std = ccl::ATTR_STD_NONE;

                if (name == ccl::Attribute::standard_name(ccl::ATTR_STD_VOLUME_DENSITY)) {
                    std = ccl::ATTR_STD_VOLUME_DENSITY;
                } else if (name == ccl::Attribute::standard_name(ccl::ATTR_STD_VOLUME_COLOR)) {
                    std = ccl::ATTR_STD_VOLUME_COLOR;
                } else if (name == ccl::Attribute::standard_name(ccl::ATTR_STD_VOLUME_FLAME)) {
                    std = ccl::ATTR_STD_VOLUME_FLAME;
                } else if (name == ccl::Attribute::standard_name(ccl::ATTR_STD_VOLUME_HEAT)) {
                    std = ccl::ATTR_STD_VOLUME_HEAT;
                } else if (name == ccl::Attribute::standard_name(ccl::ATTR_STD_VOLUME_TEMPERATURE)) {
                    std = ccl::ATTR_STD_VOLUME_TEMPERATURE;
                } else if (name == ccl::Attribute::standard_name(ccl::ATTR_STD_VOLUME_VELOCITY)) {
                    std = ccl::ATTR_STD_VOLUME_VELOCITY;
                }

                ccl::Attribute* attr = (std != ccl::ATTR_STD_NONE)
                                           ? m_cyclesVolume->attributes.add(std)
                                           : m_cyclesVolume->attributes.add(name, ccl::TypeDesc::TypeFloat,
                                                                            ccl::ATTR_ELEMENT_VOXEL);
                ccl::ImageLoader* loader = new HdCyclesVolumeLoader(filepath.c_str(), name.c_str());
                ccl::ImageParams params;
                params.frame = 0.0f;

                attr->data_voxel() = scene->image_manager->add_image(loader, params);
            }
        }
    }
#endif
}

void
HdCyclesVolume::_PopulateConstantPrimvars(const SdfPath& id, HdSceneDelegate* delegate, ccl::Scene* scene,
                                          HdPrimvarDescriptorMap const& descriptor_map, HdDirtyBits* dirtyBits)
{
    for (auto& interpolation_description : descriptor_map) {
        if (interpolation_description.first != HdInterpolationConstant) {
            continue;
        }

        for (const HdPrimvarDescriptor& description : interpolation_description.second) {
            if (!HdChangeTracker::IsPrimvarDirty(*dirtyBits, id, description.name)) {
                continue;
            }

            auto value = GetPrimvar(delegate, description.name);
            const ccl::TypeDesc value_type = HdBbAttributeSource::GetTypeDesc(HdGetValueTupleType(value).type);
            m_object_source->CreateAttributeSource<HdBbAttributeSource>(description.name, description.role, value,
                                                                        &m_cyclesVolume->attributes,
                                                                        ccl::ATTR_ELEMENT_OBJECT, value_type);
        }
    }
}

void
HdCyclesVolume::_UpdateGrids()
{
#ifdef WITH_OPENVDB
    if (m_cyclesVolume) {
        for (ccl::Attribute& attr : m_cyclesVolume->attributes.attributes) {
            if (attr.element == ccl::ATTR_ELEMENT_VOXEL) {
                ccl::ImageHandle& handle = attr.data_voxel();
                auto* loader = static_cast<HdCyclesVolumeLoader*>(handle.vdb_loader());

                if (loader) {
                    loader->UpdateGrid();
                }
            }
        }
    }
#endif
}

void
HdCyclesVolume::_UpdateObject(ccl::Scene* scene, HdCyclesRenderParam* param, HdDirtyBits* dirtyBits, bool rebuildBvh)
{
    if (m_cyclesInstances.empty()) {
        m_cyclesObject->visibility = _sharedData.visible ? m_visibilityFlags : 0;
    } else {
        m_cyclesObject->visibility = 0;
    }

    m_cyclesVolume->tag_update(scene, rebuildBvh);
    m_cyclesObject->tag_update(scene);

    // Mark visibility clean. When sync method is called object might be invisible. At that point we do not
    // need to trigger the topology and data generation. It can be postponed until visibility becomes on.
    // We need to manually mark visibility clean, but other flags remain dirty.
    if (!_sharedData.visible) {
        *dirtyBits &= ~HdChangeTracker::DirtyVisibility;
    }

    param->Interrupt();
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
HdCyclesVolume::Sync(HdSceneDelegate* sceneDelegate, HdRenderParam* renderParam, HdDirtyBits* dirtyBits,
                     TfToken const& reprSelector)
{
    SdfPath const& id = GetId();

    HdCyclesRenderParam* param = static_cast<HdCyclesRenderParam*>(renderParam);

    ccl::Scene* scene = param->GetCyclesScene();

    HdPrimvarDescriptorMap primvar_descriptor_map;
    bool update_volumes = false;

    ccl::vector<int> old_voxel_slots = get_voxel_image_slots(m_cyclesVolume);

    // Defaults
    m_useMotionBlur = false;
    m_cyclesObject->velocity_scale = 1.0f;
    m_cyclesObject->lightgroup = "";

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

    // Object transform needs to be applied to instances.
    ccl::Transform obj_tfm = ccl::transform_identity();

    if (*dirtyBits & HdChangeTracker::DirtyTransform) {
        m_transformSamples = HdCyclesSetTransform(m_cyclesObject, sceneDelegate, id, m_useMotionBlur);

        obj_tfm = mat4d_to_transform(sceneDelegate->GetTransform(id));
        *dirtyBits |= HdChangeTracker::DirtyInstancer;

        update_volumes = true;
    }

    if (*dirtyBits & HdChangeTracker::DirtyPrimvar) {
        primvar_descriptor_map = GetPrimvarDescriptorMap(sceneDelegate);
        GetObjectPrimvars(primvar_descriptor_map, sceneDelegate, dirtyBits);
        _PopulateConstantPrimvars(id, sceneDelegate, scene, primvar_descriptor_map, dirtyBits);
        update_volumes = true;
    }

    if (*dirtyBits & HdChangeTracker::DirtyMaterialId) {
        if (m_cyclesVolume) {
            // Add default shader
            const SdfPath& materialId = sceneDelegate->GetMaterialId(GetId());
            const HdCyclesMaterial* material = static_cast<const HdCyclesMaterial*>(
                sceneDelegate->GetRenderIndex().GetSprim(HdPrimTypeTokens->material, materialId));

            if (material && material->GetCyclesShader()) {
                m_usedShaders.push_back(material->GetCyclesShader());
                material->GetCyclesShader()->tag_update(scene);
            } else {
                m_usedShaders.push_back(scene->default_volume);
            }
            m_cyclesVolume->used_shaders = m_usedShaders;
            update_volumes = true;
        }
    }

    for (auto& primvarDescsEntry : primvar_descriptor_map) {
        for (auto& pv : primvarDescsEntry.second) {
            if (!HdChangeTracker::IsPrimvarDirty(*dirtyBits, id, pv.name)) {
                continue;
            }

            m_cyclesObject->velocity_scale
                = _HdCyclesGetVolumeParam<float>(pv, dirtyBits, id, this, sceneDelegate,
                                                 usdCyclesTokens->primvarsCyclesObjectMblurVolume_vel_scale,
                                                 m_cyclesObject->velocity_scale);

            update_volumes = true;
        }
    }

    // -------------------------------------
    // -- Handle point instances
    // -------------------------------------
    if (*dirtyBits & HdChangeTracker::DirtyInstancer) {
        const SdfPath& instancer_id = GetInstancerId();
        auto instancer = dynamic_cast<HdCyclesInstancer*>(sceneDelegate->GetRenderIndex().GetInstancer(instancer_id));
        if (instancer) {
            // Clear all instances...
            for (auto instance : m_cyclesInstances) {
                if (instance) {
                    m_renderDelegate->GetCyclesRenderParam()->RemoveObject(instance);
                    delete instance;
                }
            }
            m_cyclesInstances.clear();

            // create new instances
            auto instanceTransforms = instancer->SampleInstanceTransforms(id);
            auto newNumInstances = (instanceTransforms.count > 0) ? instanceTransforms.values[0].size() : 0;

            if (newNumInstances != 0) {
                using size_type = typename decltype(m_transformSamples.values)::size_type;

                std::vector<TfSmallVector<GfMatrix4d, 1>> combinedTransforms;
                combinedTransforms.reserve(newNumInstances);
                for (size_t i = 0; i < newNumInstances; ++i) {
                    // Apply prototype transform (m_transformSamples) to all the instances
                    combinedTransforms.emplace_back(instanceTransforms.count);
                    auto& instanceTransform = combinedTransforms.back();

                    if (m_transformSamples.count == 0
                        || (m_transformSamples.count == 1 && (m_transformSamples.values[0] == GfMatrix4d(1)))) {
                        for (size_type j = 0; j < instanceTransforms.count; ++j) {
                            instanceTransform[j] = instanceTransforms.values[j][i];
                        }
                    } else {
                        for (size_type j = 0; j < instanceTransforms.count; ++j) {
                            GfMatrix4d xf_j = m_transformSamples.Resample(instanceTransforms.times[j]);
                            instanceTransform[j] = xf_j * instanceTransforms.values[j][i];
                        }
                    }
                }

                for (size_t j = 0; j < newNumInstances; ++j) {
                    ccl::Object* instanceObj = _CreateObject();

                    instanceObj->visibility = _sharedData.visible ? m_visibilityFlags : 0;
                    instanceObj->tfm = mat4d_to_transform(combinedTransforms[j].data()[0]) * obj_tfm;
                    instanceObj->geometry = m_cyclesVolume;

                    m_cyclesInstances.push_back(instanceObj);

                    m_renderDelegate->GetCyclesRenderParam()->AddObject(instanceObj);
                }

                update_volumes = true;
            }
        }
    }

    if (update_volumes) {
        _UpdateGrids();
        m_cyclesVolume->use_motion_blur = m_useMotionBlur;

        bool rebuild = (old_voxel_slots != get_voxel_image_slots(m_cyclesVolume));
        _UpdateObject(scene, param, dirtyBits, rebuild);
    }

    *dirtyBits = HdChangeTracker::Clean;
}

HdDirtyBits
HdCyclesVolume::GetInitialDirtyBitsMask() const
{
    return HdChangeTracker::DirtyTopology | HdChangeTracker::DirtyPoints | HdChangeTracker::DirtyNormals
           | HdChangeTracker::DirtyWidths | HdChangeTracker::DirtyPrimvar | HdChangeTracker::DirtyTransform
           | HdChangeTracker::DirtyVisibility | HdChangeTracker::DirtyMaterialId;
}

bool
HdCyclesVolume::IsValid() const
{
    return true;
}

PXR_NAMESPACE_CLOSE_SCOPE