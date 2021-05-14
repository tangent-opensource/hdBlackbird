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

#include "basisCurves.h"

#include "attributeSource.h"
#include "config.h"
#include "material.h"
#include "renderParam.h"
#include "transformSource.h"
#include "utils.h"

#include <render/curves.h>
#include <render/hair.h>
#include <render/mesh.h>
#include <render/object.h>
#include <render/scene.h>
#include <render/shader.h>
#include <util/util_hash.h>
#include <util/util_math_float3.h>

#include <pxr/base/gf/vec2f.h>
#include <pxr/base/gf/vec3f.h>
#include <pxr/base/gf/vec3i.h>
#include <pxr/imaging/hd/sceneDelegate.h>

#include <usdCycles/tokens.h>

PXR_NAMESPACE_OPEN_SCOPE

namespace {

ccl::AttributeElement
interpolation_to_hair_element(const HdInterpolation& interpolation)
{
    switch (interpolation) {
    case HdInterpolationConstant: return ccl::AttributeElement::ATTR_ELEMENT_OBJECT;
    case HdInterpolationUniform: return ccl::AttributeElement::ATTR_ELEMENT_CURVE;
    case HdInterpolationVarying: return ccl::AttributeElement::ATTR_ELEMENT_CURVE_KEY;
    case HdInterpolationVertex: return ccl::AttributeElement::ATTR_ELEMENT_CURVE_KEY;
    case HdInterpolationFaceVarying: return ccl::AttributeElement::ATTR_ELEMENT_NONE;  // not supported
    case HdInterpolationInstance: return ccl::AttributeElement::ATTR_ELEMENT_NONE;     // not supported
    default: return ccl::AttributeElement::ATTR_ELEMENT_NONE;
    }
}

}  // namespace

///
/// Blackbird Hair
///
HdBbHairAttributeSource::HdBbHairAttributeSource(TfToken name, const TfToken& role, const VtValue& value,
                                                 ccl::Hair* hair, const HdInterpolation& interpolation)
    : HdBbAttributeSource(std::move(name), role, value, &hair->attributes, interpolation_to_hair_element(interpolation),
                          GetTypeDesc(HdGetValueTupleType(value).type, role))
{
}

// TODO: Remove this when we deprecate old curve support
// clang-format off
TF_DEFINE_PRIVATE_TOKENS(_tokens,
    ((cyclesCurveResolution, "cycles:object:curve_resolution"))
);
// clang-format on

HdCyclesBasisCurves::HdCyclesBasisCurves(SdfPath const& id, SdfPath const& instancerId,
                                         HdCyclesRenderDelegate* a_renderDelegate)
    : HdBasisCurves(id, instancerId)
    , m_visibilityFlags(ccl::PATH_RAY_ALL_VISIBILITY)
    , m_visCamera(true)
    , m_visDiffuse(true)
    , m_visGlossy(true)
    , m_visScatter(true)
    , m_visShadow(true)
    , m_visTransmission(true)
    , m_curveShape(ccl::CURVE_THICK)
    , m_curveResolution(5)
    , m_cyclesObject(nullptr)
    , m_cyclesMesh(nullptr)
    , m_cyclesHair(nullptr)
    , m_cyclesGeometry(nullptr)
    , m_renderDelegate(a_renderDelegate)
{
    static const HdCyclesConfig& config = HdCyclesConfig::GetInstance();
    config.motion_blur.eval(m_useMotionBlur, true);

    m_cyclesObject = _CreateObject();
}

HdCyclesBasisCurves::~HdCyclesBasisCurves()
{
    if (m_cyclesHair) {
        m_renderDelegate->GetCyclesRenderParam()->RemoveGeometrySafe(m_cyclesHair);
        delete m_cyclesHair;
    }
    if (m_cyclesMesh) {
        m_renderDelegate->GetCyclesRenderParam()->RemoveGeometrySafe(m_cyclesMesh);
        delete m_cyclesMesh;
    }
    if (m_cyclesObject) {
        m_renderDelegate->GetCyclesRenderParam()->RemoveObjectSafe(m_cyclesObject);
        delete m_cyclesObject;
    }
}

void
HdCyclesBasisCurves::_InitRepr(TfToken const& reprToken, HdDirtyBits* dirtyBits)
{
}

HdDirtyBits
HdCyclesBasisCurves::_PropagateDirtyBits(HdDirtyBits bits) const
{
    return bits;
}

void
HdCyclesBasisCurves::Finalize(HdRenderParam* renderParam)
{
}

ccl::Object*
HdCyclesBasisCurves::_CreateObject()
{
    // Create container object
    ccl::Object* object = new ccl::Object();

    object->visibility = ccl::PATH_RAY_ALL_VISIBILITY;

    return object;
}

void
HdCyclesBasisCurves::_PopulateCurveMesh(HdRenderParam* renderParam)
{
    ccl::Scene* scene = (static_cast<HdCyclesRenderParam*>(renderParam))->GetCyclesScene();

    static const HdCyclesConfig& config = HdCyclesConfig::GetInstance();

    // We support optimized embree Curves, as well as legacy/old curves ribbon and tube...
    // Old curves are only enabled via ENV var: HD_CYCLES_USE_OLD_CURVES
    // Old curves will likely be deprecated in the near future...
    bool use_old_curves;
    config.use_old_curves.eval(use_old_curves, true);

    if (use_old_curves) {
        if (m_curveShape == ccl::CURVE_RIBBON) {
            _CreateRibbons(scene->camera);
        } else {
            _CreateTubeMesh();
        }
    } else {
        _CreateCurves(scene);
    }

    if (m_usedShaders.size() > 0)
        m_cyclesGeometry->used_shaders = m_usedShaders;
}

void
HdCyclesBasisCurves::_PopulateMotion()
{
    if (m_pointSamples.count <= 1)
        return;

    m_cyclesGeometry->use_motion_blur = true;

    m_cyclesGeometry->motion_steps = static_cast<unsigned int>(m_pointSamples.count + 1);

    ccl::Attribute* attr_mP = m_cyclesGeometry->attributes.find(ccl::ATTR_STD_MOTION_VERTEX_POSITION);

    if (!attr_mP) {
        attr_mP = m_cyclesGeometry->attributes.add(ccl::ATTR_STD_MOTION_VERTEX_POSITION);
    }

    ccl::float3* mP = attr_mP->data_float3();
    for (size_t i = 0; i < m_pointSamples.count; ++i) {
        if (m_pointSamples.times.data()[i] == 0.0f) {
            continue;
        }
        VtVec3fArray pp;
        pp = m_pointSamples.values.data()[i].Get<VtVec3fArray>();

        for (size_t j = 0; j < m_points.size(); ++j, ++mP) {
            *mP = vec3f_to_float3(pp[j]);
        }
    }
}

void
HdCyclesBasisCurves::_AddColors(TfToken name, VtValue value, HdInterpolation interpolation)
{
    ccl::ustring attribName = ccl::ustring(name.GetString());

    int vecSize = 0;
    size_t numColors = 0;

    VtFloatArray colors1f;
    VtVec2fArray colors2f;
    VtVec3fArray colors3f;
    VtVec4fArray colors4f;

    if (value.IsHolding<VtArray<GfVec3f>>()) {
        colors3f = value.UncheckedGet<VtArray<GfVec3f>>();
        vecSize = 3;
        numColors = colors3f.size();
    } else if (value.IsHolding<VtArray<GfVec4f>>()) {
        colors4f = value.UncheckedGet<VtArray<GfVec4f>>();
        vecSize = 4;
        numColors = colors4f.size();
    } else if (value.IsHolding<VtArray<GfVec2f>>()) {
        colors2f = value.UncheckedGet<VtArray<GfVec2f>>();
        vecSize = 2;
        numColors = colors2f.size();
    } else if (value.IsHolding<VtArray<float>>()) {
        colors1f = value.UncheckedGet<VtArray<float>>();
        vecSize = 1;
        numColors = colors1f.size();
    }

    if (vecSize == 0)
        return;

    if (interpolation == HdInterpolationUniform) {
        if (m_cyclesHair) {
            ccl::Attribute* attr_vcol = m_cyclesHair->attributes.add(attribName, ccl::TypeDesc::TypeColor,
                                                                     ccl::ATTR_ELEMENT_CURVE);

            ccl::float3* fdata = attr_vcol->data_float3();

            if (fdata) {
                size_t i = 0;

                for (size_t curve = 0; curve < numColors; curve++) {
                    ccl::float3 color;

                    switch (vecSize) {
                    case 1: color = float_to_float3(colors1f[curve]); break;
                    case 2: color = vec2f_to_float3(colors2f[curve]); break;
                    case 3: color = vec3f_to_float3(colors3f[curve]); break;
                    case 4: color = vec4f_to_float3(colors4f[curve]); break;
                    }

                    fdata[i++] = color;
                }
            }
        } else {
            // @TODO: Unhandled support for deprecated curve mesh geo
            ccl::Attribute* attr_vcol = m_cyclesMesh->attributes.add(attribName, ccl::TypeDesc::TypeColor,
                                                                     ccl::ATTR_ELEMENT_CORNER_BYTE);
            (void)attr_vcol;
            assert(0);
        }
    } else if (interpolation == HdInterpolationVertex) {
        VtIntArray curveVertexCounts = m_topology.GetCurveVertexCounts();
        if (m_cyclesHair) {
            // Support for vertex varying attributes is not supported in Cycles hair.
            // For now we just get the root value and apply to the whole strand...
            ccl::Attribute* attr_vcol = m_cyclesHair->attributes.add(attribName, ccl::TypeDesc::TypeColor,
                                                                     ccl::ATTR_ELEMENT_CURVE);

            ccl::float3* fdata = attr_vcol->data_float3();

            if (fdata) {
                int curveOffset = 0;
                for (size_t i = 0; i < curveVertexCounts.size(); i++) {
                    ccl::float3 color;

                    switch (vecSize) {
                    case 1: color = float_to_float3(colors1f[curveOffset]); break;
                    case 2: color = vec2f_to_float3(colors2f[curveOffset]); break;
                    case 3: color = vec3f_to_float3(colors3f[curveOffset]); break;
                    case 4: color = vec4f_to_float3(colors4f[curveOffset]); break;
                    }

                    fdata[i] = color;

                    curveOffset += curveVertexCounts[i];
                }
            }
        } else {
            // @TODO: Unhandled support for deprecated curve mesh geo
            ccl::Attribute* attr_vcol = m_cyclesMesh->attributes.add(attribName, ccl::TypeDesc::TypeColor,
                                                                     ccl::ATTR_ELEMENT_CORNER_BYTE);

            (void)attr_vcol;
            assert(0);
        }
    }
}

void
HdCyclesBasisCurves::_AddUVS(TfToken name, VtValue value, HdInterpolation interpolation)
{
    ccl::ustring attribName = ccl::ustring(name.GetString());

    // convert uniform uv attrib

    auto fill_uniform_uv_attrib = [&attribName](auto& attr_uvs, ccl::AttributeSet& attributes) {
        ccl::Attribute* attr_std_uv = attributes.add(ccl::ATTR_STD_UV, attribName);
        ccl::float2* std_uv_data = attr_std_uv->data_float2();

        for (size_t curve = 0; curve < attr_uvs.size(); curve++) {
            std_uv_data[curve][0] = attr_uvs[curve][0];
            std_uv_data[curve][1] = attr_uvs[curve][1];
        }
    };

    if (interpolation == HdInterpolationUniform) {
        if (value.IsHolding<VtArray<GfVec2f>>()) {
            VtVec2fArray uvs = value.UncheckedGet<VtArray<GfVec2f>>();
            if (m_cyclesHair) {
                fill_uniform_uv_attrib(uvs, m_cyclesHair->attributes);
            } else {
                // @TODO: Unhandled support for deprecated curve mesh geo
                ccl::Attribute* attr_std_uv = m_cyclesMesh->attributes.add(ccl::ATTR_STD_UV, attribName);
                (void)attr_std_uv;
            }
        } else if (value.IsHolding<VtArray<GfVec3f>>()) {
            VtVec3fArray uvs = value.UncheckedGet<VtArray<GfVec3f>>();
            if (m_cyclesHair) {
                fill_uniform_uv_attrib(uvs, m_cyclesHair->attributes);
            } else {
                // @TODO: Unhandled support for deprecated curve mesh geo
                ccl::Attribute* attr_std_uv = m_cyclesMesh->attributes.add(ccl::ATTR_STD_UV, attribName);
                (void)attr_std_uv;
            }
        }
        return;
    }

    // convert vertex/varying uv attrib

    auto fill_vertex_or_varying_uv_attrib = [&attribName](auto& attr_uvs, ccl::AttributeSet& attributes,
                                                          const VtIntArray& vertexCounts) {
        ccl::Attribute* attr_std_uv = attributes.add(ccl::ATTR_STD_UV, attribName);
        ccl::float2* std_uv_data = attr_std_uv->data_float2();

        ccl::Attribute* attr_st = attributes.add(attribName, ccl::TypeFloat2, ccl::ATTR_ELEMENT_CURVE_KEY);
        ccl::float2* st_data = attr_st->data_float2();

        for (size_t curve = 0, offset = 0; curve < vertexCounts.size(); ++curve) {
            // std_uv - per curve
            std_uv_data[curve][0] = attr_uvs[offset][0];
            std_uv_data[curve][1] = attr_uvs[offset][1];

            // st - per vertex
            for (size_t vertex = 0; vertex < vertexCounts[curve]; ++vertex) {
                st_data[offset + vertex][0] = attr_uvs[offset + vertex][0];
                st_data[offset + vertex][1] = attr_uvs[offset + vertex][1];
            }
            offset += static_cast<size_t>(vertexCounts[curve]);
        }
    };

    if (interpolation == HdInterpolationVertex || interpolation == HdInterpolationVarying) {
        VtIntArray curveVertexCounts = m_topology.GetCurveVertexCounts();

        if (value.IsHolding<VtArray<GfVec2f>>()) {
            VtVec2fArray uvs = value.UncheckedGet<VtArray<GfVec2f>>();
            if (m_cyclesHair) {
                fill_vertex_or_varying_uv_attrib(uvs, m_cyclesHair->attributes, curveVertexCounts);
            } else {
                // @TODO: Unhandled support for deprecated curve mesh geo
                ccl::Attribute* attr_std_uv = m_cyclesMesh->attributes.add(ccl::ATTR_STD_UV, attribName);
                (void)attr_std_uv;
            }
        } else if (value.IsHolding<VtArray<GfVec3f>>()) {
            VtVec3fArray uvs = value.UncheckedGet<VtArray<GfVec3f>>();
            if (m_cyclesHair) {
                fill_vertex_or_varying_uv_attrib(uvs, m_cyclesHair->attributes, curveVertexCounts);
            } else {
                // @TODO: Unhandled support for deprecated curve mesh geo
                ccl::Attribute* attr_std_uv = m_cyclesMesh->attributes.add(ccl::ATTR_STD_UV, attribName);
                (void)attr_std_uv;
            }
        }
        return;
    }
}

void
HdCyclesBasisCurves::_PopulateGenerated()
{
    if (!m_cyclesObject)
        return;

    ccl::float3 loc, size;

    if (m_cyclesMesh) {
        HdCyclesMeshTextureSpace(m_cyclesMesh, loc, size);
        ccl::Attribute* attr_generated = m_cyclesMesh->attributes.add(ccl::ATTR_STD_GENERATED);
        ccl::float3* generated = attr_generated->data_float3();

        for (size_t i = 0; i < m_cyclesMesh->verts.size(); i++)
            generated[i] = m_cyclesMesh->verts[i] * size - loc;
    } else {
        HdCyclesMeshTextureSpace(m_cyclesHair, loc, size);
        ccl::Attribute* attr_generated = m_cyclesHair->attributes.add(ccl::ATTR_STD_GENERATED);
        ccl::float3* generated = attr_generated->data_float3();

        for (size_t i = 0; i < m_cyclesHair->num_curves(); i++) {
            ccl::float3 co = m_cyclesHair->curve_keys[m_cyclesHair->get_curve(i).first_key];
            generated[i] = co * size - loc;
        }
    }
}

void
HdCyclesBasisCurves::Sync(HdSceneDelegate* sceneDelegate, HdRenderParam* renderParam, HdDirtyBits* dirtyBits,
                          TfToken const& reprSelector)
{
    SdfPath const& id = GetId();


    auto resource_registry = dynamic_cast<HdCyclesResourceRegistry*>(m_renderDelegate->GetResourceRegistry().get());
    HdInstance<HdCyclesObjectSourceSharedPtr> object_instance = resource_registry->GetObjectInstance(id);
    if (object_instance.IsFirstInstance()) {
        object_instance.SetValue(std::make_shared<HdCyclesObjectSource>(m_cyclesObject, id));
    }
    m_object_source = object_instance.GetValue();

    HdCyclesRenderParam* param = static_cast<HdCyclesRenderParam*>(renderParam);

    ccl::Scene* scene = param->GetCyclesScene();
    ccl::thread_scoped_lock scene_lock(scene->mutex);

    HdCyclesPDPIMap pdpi;
    bool generate_new_curve = false;
    bool update_curve = false;

    // Defaults
    m_visCamera = m_visDiffuse = m_visGlossy = m_visScatter = m_visShadow = m_visTransmission = true;
    m_useMotionBlur = false;
    m_cyclesObject->is_shadow_catcher = false;
    m_cyclesObject->pass_id = 0;
    m_cyclesObject->use_holdout = false;
    m_cyclesObject->asset_name = "";

    // initial values
    TfToken curveShape = usdCyclesTokens->ribbon;
    m_points.clear();
    m_indices.clear();
    m_pointSamples.count = 0;
    m_widths = VtFloatArray(1, 0.1f);
    m_widthsInterpolation = HdInterpolationConstant;
    m_normals.clear();
    m_visibilityFlags = 0;
    m_curveResolution = 5;

    if (*dirtyBits & HdChangeTracker::DirtyTopology) {
        m_topology = sceneDelegate->GetBasisCurvesTopology(id);
        if (m_topology.HasIndices()) {
            m_indices = m_topology.GetCurveIndices();
        }
        generate_new_curve = true;
    }

    // to be committed after curve creation
    struct HdBbPrimvar {
        HdPrimvarDescriptor descriptor;
        VtValue value;
    };
    std::vector<HdBbPrimvar> primvars;

    if (*dirtyBits & HdChangeTracker::DirtyPrimvar) {
        std::array<std::pair<HdInterpolation, HdPrimvarDescriptorVector>, 5> primvars_desc {
            std::make_pair(HdInterpolationConstant, HdPrimvarDescriptorVector {}),
            std::make_pair(HdInterpolationUniform, HdPrimvarDescriptorVector {}),
            std::make_pair(HdInterpolationVertex, HdPrimvarDescriptorVector {}),
            std::make_pair(HdInterpolationVarying, HdPrimvarDescriptorVector {}),
            std::make_pair(HdInterpolationFaceVarying, HdPrimvarDescriptorVector {}),
        };

        for (auto& info : primvars_desc) {
            info.second = sceneDelegate->GetPrimvarDescriptors(id, info.first);
        }

        //
        for (auto& interpolation_description : primvars_desc) {
            for (const HdPrimvarDescriptor& description : interpolation_description.second) {
                if (!HdChangeTracker::IsPrimvarDirty(*dirtyBits, id, description.name)) {
                    continue;
                }

                //
                // special primvars
                //
                if (description.name == HdTokens->points) {
                    VtValue value = sceneDelegate->Get(id, HdTokens->points);
                    m_points = value.Get<VtVec3fArray>();
                    sceneDelegate->SamplePrimvar(id, HdTokens->points, &m_pointSamples);
                    generate_new_curve = true;
                    continue;
                }

                if (description.name == HdTokens->widths) {
                    VtValue value = sceneDelegate->Get(id, HdTokens->widths);
                    m_widths = value.Get<VtFloatArray>();
                    m_widthsInterpolation = description.interpolation;
                    generate_new_curve = true;
                    continue;
                }

                if (description.name == HdTokens->normals) {
                    VtValue value = sceneDelegate->Get(id, HdTokens->normals);
                    m_normals = value.Get<VtVec3fArray>();
                    generate_new_curve = true;
                    continue;
                }

                if (description.role == HdPrimvarRoleTokens->textureCoordinate) {
                    VtValue value = GetPrimvar(sceneDelegate, description.name);
                    primvars.emplace_back(HdBbPrimvar { description, value });
                    continue;
                }

                if (description.role == HdPrimvarRoleTokens->color) {
                    VtValue value = GetPrimvar(sceneDelegate, description.name);
                    primvars.emplace_back(HdBbPrimvar { description, value });
                    continue;
                }

                //
                // arbitrary primvar - do not submit cycles: prefixed schema
                //
                if (!TfStringStartsWith(description.name.GetString(), "cycles:")) {
                    VtValue value = GetPrimvar(sceneDelegate, description.name);
                    primvars.emplace_back(HdBbPrimvar { description, value });
                    continue;
                }

                //
                // cycles schema primvars
                //
                if (!TfStringStartsWith(description.name.GetString(), "cycles:")) {
                    continue;
                }

                //
                const std::string primvar_name = std::string { "primvars:" } + description.name.GetString();

                if (primvar_name == usdCyclesTokens->primvarsCyclesCurveShape) {
                    VtValue value = GetPrimvar(sceneDelegate, usdCyclesTokens->primvarsCyclesCurveShape);
                    if (value.IsHolding<TfToken>()) {
                        curveShape = value.UncheckedGet<TfToken>();
                        if (curveShape == usdCyclesTokens->ribbon) {
                            m_curveShape = ccl::CURVE_RIBBON;
                            update_curve = true;
                        } else {
                            m_curveShape = ccl::CURVE_THICK;
                            update_curve = true;
                        }
                    }
                    continue;
                }

                if(primvar_name == usdCyclesTokens->primvarsCyclesObjectAsset_name) {
                    VtValue value = GetPrimvar(sceneDelegate, usdCyclesTokens->primvarsCyclesObjectAsset_name);
                    if(value.IsHolding<std::string>()) {
                        std::string assetName = value.Get<std::string>();
                        m_cyclesObject->asset_name = ccl::ustring(assetName);
                    }
                    continue;
                }

                if (primvar_name == usdCyclesTokens->primvarsCyclesObjectMblur) {
                    VtValue value = GetPrimvar(sceneDelegate, usdCyclesTokens->primvarsCyclesObjectMblur);
                    if (value.IsHolding<bool>())
                        m_useMotionBlur = value.Get<bool>();
                    continue;
                }

                if (primvar_name == usdCyclesTokens->primvarsCyclesObjectIs_shadow_catcher) {
                    VtValue value = GetPrimvar(sceneDelegate, usdCyclesTokens->primvarsCyclesObjectIs_shadow_catcher);
                    if (value.IsHolding<bool>())
                        m_cyclesObject->is_shadow_catcher = value.Get<bool>();
                    continue;
                }

                if (primvar_name == usdCyclesTokens->primvarsCyclesObjectPass_id) {
                    VtValue value = GetPrimvar(sceneDelegate, usdCyclesTokens->primvarsCyclesObjectPass_id);
                    if (value.IsHolding<bool>())
                        m_cyclesObject->pass_id = value.Get<bool>();
                    continue;
                }

                if (primvar_name == usdCyclesTokens->primvarsCyclesObjectUse_holdout) {
                    VtValue value = GetPrimvar(sceneDelegate, usdCyclesTokens->primvarsCyclesObjectUse_holdout);
                    if (value.IsHolding<bool>())
                        m_cyclesObject->use_holdout = value.Get<bool>();
                    continue;
                }

                //
                // Visibility schema
                //
                if (primvar_name == usdCyclesTokens->primvarsCyclesObjectVisibilityCamera) {
                    VtValue value = GetPrimvar(sceneDelegate, usdCyclesTokens->primvarsCyclesObjectVisibilityCamera);
                    if (value.IsHolding<bool>())
                        m_visCamera = value.Get<bool>();
                    continue;
                }

                if (primvar_name == usdCyclesTokens->primvarsCyclesObjectVisibilityDiffuse) {
                    VtValue value = GetPrimvar(sceneDelegate, usdCyclesTokens->primvarsCyclesObjectVisibilityDiffuse);
                    if (value.IsHolding<bool>())
                        m_visDiffuse = value.Get<bool>();
                    continue;
                }

                if (primvar_name == usdCyclesTokens->primvarsCyclesObjectVisibilityGlossy) {
                    VtValue value = GetPrimvar(sceneDelegate, usdCyclesTokens->primvarsCyclesObjectVisibilityGlossy);
                    if (value.IsHolding<bool>())
                        m_visGlossy = value.Get<bool>();
                    continue;
                }

                if (primvar_name == usdCyclesTokens->primvarsCyclesObjectVisibilityScatter) {
                    VtValue value = GetPrimvar(sceneDelegate, usdCyclesTokens->primvarsCyclesObjectVisibilityScatter);
                    if (value.IsHolding<bool>())
                        m_visScatter = value.Get<bool>();
                    continue;
                }

                if (primvar_name == usdCyclesTokens->primvarsCyclesObjectVisibilityShadow) {
                    VtValue value = GetPrimvar(sceneDelegate, usdCyclesTokens->primvarsCyclesObjectVisibilityShadow);
                    if (value.IsHolding<bool>())
                        m_visShadow = value.Get<bool>();
                    continue;
                }

                if (primvar_name == usdCyclesTokens->primvarsCyclesObjectVisibilityTransmission) {
                    VtValue value = GetPrimvar(sceneDelegate, usdCyclesTokens->primvarsCyclesObjectVisibilityTransmission);
                    if (value.IsHolding<bool>())
                        m_visTransmission = value.Get<bool>();
                    continue;
                }
            }
        }
    }

    if (*dirtyBits & HdChangeTracker::DirtyVisibility) {
        update_curve = true;
        _sharedData.visible = sceneDelegate->GetVisible(id);
    }

    //
    // create curve geometry
    //
    if (generate_new_curve) {
        if (HdCyclesIsPrimvarExists(_tokens->cyclesCurveResolution, pdpi)) {
            VtIntArray resolution = sceneDelegate->Get(id, _tokens->cyclesCurveResolution).Get<VtIntArray>();
            if (resolution.size() > 0) {
                m_curveResolution = resolution[0];
            }
        }

        if (m_cyclesGeometry) {
            param->RemoveGeometry(m_cyclesHair);

            m_cyclesGeometry->clear();
            delete m_cyclesGeometry;
        }

        _PopulateCurveMesh(param);

        if (m_cyclesGeometry) {
            m_renderDelegate->GetCyclesRenderParam()->AddObject(m_cyclesObject);
            m_cyclesObject->geometry = m_cyclesGeometry;
            m_cyclesGeometry->compute_bounds();

            _PopulateGenerated();

            param->AddGeometry(m_cyclesGeometry);
        }

        if (m_useMotionBlur)
            _PopulateMotion();
    }

    //
    // commit attributes to the curve
    //
    for (auto& primvar : primvars) {
        if (primvar.descriptor.role == HdPrimvarRoleTokens->textureCoordinate) {
            _AddUVS(primvar.descriptor.name, primvar.value, primvar.descriptor.interpolation);
            continue;
        }

        if (primvar.descriptor.role == HdPrimvarRoleTokens->color) {
            _AddColors(primvar.descriptor.name, primvar.value, primvar.descriptor.interpolation);
            continue;
        }

        m_object_source->CreateAttributeSource<HdBbHairAttributeSource>(primvar.descriptor.name,
                                                                        primvar.descriptor.role, primvar.value,
                                                                        m_cyclesHair, primvar.descriptor.interpolation);
    }

    if (*dirtyBits & HdChangeTracker::DirtyTransform) {
        auto fallback = sceneDelegate->GetTransform(id);
        HdCyclesMatrix4dTimeSampleArray xf {};

        std::shared_ptr<HdCyclesTransformSource> transform_source;
        if (!m_useMotionBlur) {
            transform_source = std::make_shared<HdCyclesTransformSource>(m_object_source->GetObject(), xf, fallback);
        } else {
            sceneDelegate->SampleTransform(id, &xf);

            VtValue ts_value = GetPrimvar(sceneDelegate, usdCyclesTokens->primvarsCyclesObjectTransformSamples);
            if (!ts_value.IsEmpty()) {
                auto num_new_samples = ts_value.Get<int>();
                transform_source = std::make_shared<HdCyclesTransformSource>(m_object_source->GetObject(), xf, fallback,
                                                                             num_new_samples);
            } else {
                transform_source = std::make_shared<HdCyclesTransformSource>(m_object_source->GetObject(), xf, fallback,
                                                                             3);
            }
        }
        m_object_source->AddObjectPropertiesSource(std::move(transform_source));

        update_curve = true;
    }

    if (*dirtyBits & HdChangeTracker::DirtyMaterialId) {
        // We probably need to clear this array, however putting this here,
        // breaks some IPR sessions
        // m_usedShaders.clear();

        if (m_cyclesGeometry) {
            // Add default shader
            const SdfPath& materialId = sceneDelegate->GetMaterialId(GetId());
            const HdCyclesMaterial* material = static_cast<const HdCyclesMaterial*>(
                sceneDelegate->GetRenderIndex().GetSprim(HdPrimTypeTokens->material, materialId));

            if (material && material->GetCyclesShader()) {
                m_usedShaders.push_back(material->GetCyclesShader());

                material->GetCyclesShader()->tag_update(scene);
            } else {
                m_usedShaders.push_back(scene->default_surface);
            }

            m_cyclesGeometry->used_shaders = m_usedShaders;
            update_curve = true;
        }
    }

    if (generate_new_curve || update_curve) {
        m_cyclesHair->curve_shape = m_curveShape;

        m_visibilityFlags |= m_visCamera ? ccl::PATH_RAY_CAMERA : 0;
        m_visibilityFlags |= m_visDiffuse ? ccl::PATH_RAY_DIFFUSE : 0;
        m_visibilityFlags |= m_visGlossy ? ccl::PATH_RAY_GLOSSY : 0;
        m_visibilityFlags |= m_visScatter ? ccl::PATH_RAY_VOLUME_SCATTER : 0;
        m_visibilityFlags |= m_visShadow ? ccl::PATH_RAY_SHADOW : 0;
        m_visibilityFlags |= m_visTransmission ? ccl::PATH_RAY_TRANSMIT : 0;

        m_cyclesObject->visibility = m_visibilityFlags;
        if (!_sharedData.visible)
            m_cyclesObject->visibility = 0;

        m_cyclesGeometry->tag_update(scene, true);
        m_cyclesObject->tag_update(scene);
        param->Interrupt();
    }

    *dirtyBits = HdChangeTracker::Clean;
}

void
HdCyclesBasisCurves::_CreateCurves(ccl::Scene* a_scene)
{
    m_cyclesHair = new ccl::Hair();
    m_cyclesGeometry = m_cyclesHair;

    // Get USD Curve Metadata
    VtIntArray curveVertexCounts = m_topology.GetCurveVertexCounts();
    TfToken curveType = m_topology.GetCurveType();
    TfToken curveBasis = m_topology.GetCurveBasis();
    TfToken curveWrap = m_topology.GetCurveWrap();

    size_t num_curves = curveVertexCounts.size();
    size_t num_keys = 0;

    for (size_t i = 0; i < num_curves; i++) {
        num_keys += static_cast<size_t>(curveVertexCounts[i]);
    }

    ccl::Attribute* attr_intercept = nullptr;
    ccl::Attribute* attr_random = nullptr;

    attr_intercept = m_cyclesHair->attributes.add(ccl::ATTR_STD_CURVE_INTERCEPT);

    attr_random = m_cyclesHair->attributes.add(ccl::ATTR_STD_CURVE_RANDOM);

    // We have patched the Cycles API to allow shape to be set per curve
    m_cyclesHair->curve_shape = m_curveShape;
    m_cyclesHair->reserve_curves(static_cast<int>(num_curves), static_cast<int>(num_keys));

    num_curves = 0;
    num_keys = 0;

    int currentPointCount = 0;

    // For every curve
    for (size_t i = 0; i < curveVertexCounts.size(); i++) {
        size_t num_curve_keys = 0;

        // For every section
        for (size_t j = 0; j < curveVertexCounts[i]; j++) {
            size_t idx = j + currentPointCount;

            const float time = static_cast<float>(j) / static_cast<float>(curveVertexCounts[i] - 1);

            if (idx > m_points.size()) {
                TF_WARN("Attempted to access invalid point. Continuing");
                continue;
            }

            ccl::float3 usd_location = vec3f_to_float3(m_points[idx]);

            // Widths

            // Hydra/USD treats widths as diameters so we halve before sending to cycles
            float radius = 0.1f;

            size_t width_idx = std::min(idx, m_widths.size() - 1);

            if (m_widthsInterpolation == HdInterpolationUniform)
                width_idx = std::min(i, m_widths.size() - 1);
            else if (m_widthsInterpolation == HdInterpolationConstant)
                width_idx = 0;

            if (m_widths.size() > 0)
                radius = m_widths[width_idx] / 2.0f;

            m_cyclesHair->add_curve_key(usd_location, radius);

            // Intercept

            if (attr_intercept)
                attr_intercept->add(time);

            num_curve_keys++;
        }

        if (attr_random != nullptr) {
            attr_random->add(ccl::hash_uint2_to_float(static_cast<unsigned int>(num_curves), 0));
        }

        m_cyclesHair->add_curve(static_cast<int>(num_keys), 0);
        num_keys += num_curve_keys;
        currentPointCount += curveVertexCounts[i];
        num_curves++;
    }

    if ((m_cyclesHair->curve_keys.size() != num_keys) || (m_cyclesHair->num_curves() != num_curves)) {
        TF_WARN("Allocation failed. Clearing data");

        m_cyclesHair->clear();
    }
}

void
HdCyclesBasisCurves::_CreateRibbons(ccl::Camera* a_camera)
{
    m_cyclesMesh = new ccl::Mesh();
    m_cyclesGeometry = m_cyclesMesh;

    bool isCameraOriented = false;

    ccl::float3 RotCam;
    bool is_ortho = false;
    if (m_normals.size() <= 0) {
        if (a_camera != nullptr) {
            isCameraOriented = true;
            ccl::Transform& ctfm = a_camera->matrix;
            if (a_camera->type == ccl::CAMERA_ORTHOGRAPHIC) {
                RotCam = -ccl::make_float3(ctfm.x.z, ctfm.y.z, ctfm.z.z);
            } else {
                ccl::Transform tfm = m_cyclesObject->tfm;
                ccl::Transform itfm = ccl::transform_quick_inverse(tfm);
                RotCam = ccl::transform_point(&itfm, ccl::make_float3(ctfm.x.w, ctfm.y.w, ctfm.z.w));
            }
            is_ortho = a_camera->type == ccl::CAMERA_ORTHOGRAPHIC;
        }
    }

    // Get USD Curve Metadata
    VtIntArray curveVertexCounts = m_topology.GetCurveVertexCounts();
    TfToken curveType = m_topology.GetCurveType();
    TfToken curveBasis = m_topology.GetCurveBasis();
    TfToken curveWrap = m_topology.GetCurveWrap();

    int num_vertices = 0;
    int num_tris = 0;
    for (size_t i = 0; i < curveVertexCounts.size(); i++) {
        num_vertices += curveVertexCounts[i] * 2;
        num_tris += ((curveVertexCounts[i] - 1) * 2);
    }

    // Start Cycles Mesh population
    int vertexindex = 0;

    m_cyclesMesh->reserve_mesh(num_vertices, num_tris);

    // For every curve
    for (size_t i = 0; i < curveVertexCounts.size(); i++) {
        ccl::float3 xbasis;
        ccl::float3 v1;

        ccl::float3 ickey_loc = vec3f_to_float3(m_points[0]);

        // Widths

        // Hydra/USD treats widths as diameters so we halve before sending to cycles
        float radius = 0.1f;

        size_t width_idx = std::min(i, m_widths.size() - 1);

        if (m_widthsInterpolation == HdInterpolationUniform)
            width_idx = std::min(i, m_widths.size() - 1);
        else if (m_widthsInterpolation == HdInterpolationConstant)
            width_idx = 0;

        if (m_widths.size() > 0)
            radius = m_widths[width_idx] / 2.0f;

        v1 = vec3f_to_float3(m_points[1] - m_points[0]);
        if (isCameraOriented) {
            if (is_ortho)
                xbasis = normalize(cross(RotCam, v1));
            else
                xbasis = ccl::normalize(ccl::cross(RotCam - ickey_loc, v1));
        } else {
            if (m_normals.size() > 0)
                xbasis = ccl::normalize(vec3f_to_float3(m_normals[0]));
            else
                xbasis = ccl::normalize(ccl::cross(ickey_loc, v1));
        }
        ccl::float3 ickey_loc_shfl = ickey_loc - radius * xbasis;
        ccl::float3 ickey_loc_shfr = ickey_loc + radius * xbasis;
        m_cyclesMesh->add_vertex(ickey_loc_shfl);
        m_cyclesMesh->add_vertex(ickey_loc_shfr);
        vertexindex += 2;

        // For every section
        for (int j = 0; j < curveVertexCounts[i]; j++) {
            int first_idx = (static_cast<int>(i) * curveVertexCounts[i]);
            int idx = j + (static_cast<int>(i) * curveVertexCounts[i]);

            ickey_loc = vec3f_to_float3(m_points[idx]);

            if (j == 0) {
                // subv = 0;
                // First curve point
                v1 = vec3f_to_float3(m_points[idx] - m_points[std::max(idx - 1, first_idx)]);
            } else {
                v1 = vec3f_to_float3(m_points[idx + 1] - m_points[idx - 1]);
            }

            // Widths

            // Hydra/USD treats widths as diameters so we halve before sending to cycles
            radius = 0.1f;

            width_idx = std::min(idx, static_cast<int>(m_widths.size() - 1));

            if (m_widthsInterpolation == HdInterpolationUniform)
                width_idx = std::min(i, m_widths.size() - 1);
            else if (m_widthsInterpolation == HdInterpolationConstant)
                width_idx = 0;

            if (m_widths.size() > 0)
                radius = m_widths[width_idx] / 2.0f;

            if (isCameraOriented) {
                if (is_ortho)
                    xbasis = normalize(cross(RotCam, v1));
                else
                    xbasis = ccl::normalize(ccl::cross(RotCam - ickey_loc, v1));
            } else {
                if (m_normals.size() > 0)
                    xbasis = ccl::normalize(vec3f_to_float3(m_normals[idx]));
                else
                    xbasis = ccl::normalize(ccl::cross(ickey_loc, v1));
            }
            ickey_loc_shfl = ickey_loc - radius * xbasis;
            ickey_loc_shfr = ickey_loc + radius * xbasis;
            m_cyclesMesh->add_vertex(ickey_loc_shfl);
            m_cyclesMesh->add_vertex(ickey_loc_shfr);
            m_cyclesMesh->add_triangle(vertexindex - 2, vertexindex, vertexindex - 1, 0, true);
            m_cyclesMesh->add_triangle(vertexindex + 1, vertexindex - 1, vertexindex, 0, true);
            vertexindex += 2;
        }
    }

    // TODO: Implement texcoords
}

void
HdCyclesBasisCurves::_CreateTubeMesh()
{
    m_cyclesMesh = new ccl::Mesh();
    m_cyclesGeometry = m_cyclesMesh;

    // Get USD Curve Metadata
    VtIntArray curveVertexCounts = m_topology.GetCurveVertexCounts();
    TfToken curveType = m_topology.GetCurveType();
    TfToken curveBasis = m_topology.GetCurveBasis();
    TfToken curveWrap = m_topology.GetCurveWrap();

    int num_vertices = 0;
    int num_tris = 0;
    for (size_t i = 0; i < curveVertexCounts.size(); i++) {
        num_vertices += curveVertexCounts[i] * m_curveResolution;
        num_tris += ((curveVertexCounts[i] - 1) * 2 * m_curveResolution);
    }

    // Start Cycles Mesh population
    int vertexindex = m_curveResolution;

    m_cyclesMesh->reserve_mesh(num_vertices, num_tris);

    // For every curve
    for (size_t i = 0; i < curveVertexCounts.size(); i++) {
        ccl::float3 firstxbasis = ccl::cross(ccl::make_float3(1.0f, 0.0f, 0.0f),
                                             vec3f_to_float3(m_points[1]) - vec3f_to_float3(m_points[0]));

        if (!ccl::is_zero(firstxbasis))
            firstxbasis = ccl::normalize(firstxbasis);
        else
            firstxbasis = ccl::normalize(ccl::cross(ccl::make_float3(0.0f, 1.0f, 0.0f),
                                                    vec3f_to_float3(m_points[1]) - vec3f_to_float3(m_points[0])));

        // For every section
        for (int j = 0; j < curveVertexCounts[i]; j++) {
            int first_idx = (static_cast<int>(i) * curveVertexCounts[i]);
            int idx = j + (static_cast<int>(i) * curveVertexCounts[i]);

            ccl::float3 xbasis = firstxbasis;
            ccl::float3 v1;
            ccl::float3 v2;

            if (j == 0) {
                // First curve point
                v1 = vec3f_to_float3(m_points[std::min(idx + 2, (curveVertexCounts[i] + curveVertexCounts[i] - 1))]);
                v2 = vec3f_to_float3(m_points[idx + 1] - m_points[idx]);
            } else if (j == (curveVertexCounts[i] - 1)) {
                // Last curve point
                v1 = vec3f_to_float3(m_points[idx] - m_points[idx - 1]);
                v2 = vec3f_to_float3(m_points[idx - 1] - m_points[std::max(idx - 2, first_idx)]);  // First key
            } else {
                v1 = vec3f_to_float3(m_points[idx + 1] - m_points[idx]);
                v2 = vec3f_to_float3(m_points[idx] - m_points[idx - 1]);
            }

            xbasis = ccl::cross(v1, v2);

            if (ccl::len_squared(xbasis) >= 0.05f * ccl::len_squared(v1) * ccl::len_squared(v2)) {
                firstxbasis = ccl::normalize(xbasis);
                break;
            }
        }

        // For every section
        for (int j = 0; j < curveVertexCounts[i]; j++) {
            int first_idx = (static_cast<int>(i) * curveVertexCounts[i]);
            int idx = j + (static_cast<int>(i) * curveVertexCounts[i]);
            ccl::float3 xbasis;
            ccl::float3 ybasis;
            ccl::float3 v1;
            ccl::float3 v2;

            ccl::float3 usd_location = vec3f_to_float3(m_points[idx]);

            if (j == 0) {
                // First curve point
                v1 = vec3f_to_float3(m_points[std::min(idx + 2, (curveVertexCounts[i] - 1))] - m_points[idx + 1]);
                v2 = vec3f_to_float3(m_points[idx + 1] - m_points[idx]);
            } else if (j == (curveVertexCounts[i] - 1)) {
                v1 = vec3f_to_float3(m_points[idx] - m_points[idx - 1]);
                v2 = vec3f_to_float3(m_points[idx - 1] - m_points[std::max(idx - 2, first_idx)]);
            } else {
                v1 = vec3f_to_float3(m_points[idx + 1] - m_points[idx]);
                v1 = vec3f_to_float3(m_points[idx] - m_points[idx - 1]);
            }

            // Add vertex in circle

            // Widths

            // Hydra/USD treats widths as diameters so we halve before sending to cycles
            float radius = 0.1f;

            size_t width_idx = std::min(static_cast<size_t>(idx), m_widths.size() - 1);

            if (m_widthsInterpolation == HdInterpolationUniform)
                width_idx = std::min(i, m_widths.size() - 1);
            else if (m_widthsInterpolation == HdInterpolationConstant)
                width_idx = 0;

            if (m_widths.size() > 0)
                radius = m_widths[width_idx] / 2.0f;

            float angle = M_2PI_F / static_cast<float>(m_curveResolution);

            xbasis = ccl::cross(v1, v2);

            if (ccl::len_squared(xbasis) >= 0.05f * ccl::len_squared(v1) * ccl::len_squared(v2)) {
                xbasis = ccl::normalize(xbasis);
                firstxbasis = xbasis;
            } else {
                xbasis = firstxbasis;
            }

            ybasis = ccl::normalize(ccl::cross(xbasis, v2));

            // Add vertices
            float segment_angle = 0.0f;
            for (int k = 0; k < m_curveResolution; k++) {
                ccl::float3 vertex_location = usd_location
                                              + radius * (cosf(segment_angle) * xbasis + sinf(segment_angle) * ybasis);
                segment_angle += angle;
                m_cyclesMesh->add_vertex(vertex_location);
            }

            if (j < curveVertexCounts[i] - 1) {
                for (int k = 0; k < m_curveResolution - 1; k++) {
                    int t1 = vertexindex - m_curveResolution + k;
                    int t2 = vertexindex + k;
                    int t3 = vertexindex - m_curveResolution + k + 1;

                    m_cyclesMesh->add_triangle(t1, t2, t3, 0, true);

                    t1 = vertexindex + k + 1;
                    t2 = vertexindex - m_curveResolution + k + 1;
                    t3 = vertexindex + k;

                    m_cyclesMesh->add_triangle(t1, t2, t3, 0, true);
                }
                int t1 = vertexindex - 1;
                int t2 = vertexindex + m_curveResolution - 1;
                int t3 = vertexindex - m_curveResolution;

                m_cyclesMesh->add_triangle(t1, t2, t3, 0, true);

                t1 = vertexindex;
                t2 = vertexindex - m_curveResolution;
                t3 = vertexindex + m_curveResolution - 1;

                m_cyclesMesh->add_triangle(t1, t2, t3, 0, true);
            }
            vertexindex += m_curveResolution;
        }
    }

    // TODO: Implement texcoords
}

HdDirtyBits
HdCyclesBasisCurves::GetInitialDirtyBitsMask() const
{
    return HdChangeTracker::DirtyTopology | HdChangeTracker::DirtyPoints | HdChangeTracker::DirtyNormals
           | HdChangeTracker::DirtyWidths | HdChangeTracker::DirtyPrimvar | HdChangeTracker::DirtyTransform
           | HdChangeTracker::DirtyVisibility | HdChangeTracker::DirtyMaterialId;
}

bool
HdCyclesBasisCurves::IsValid() const
{
    return true;
}

PXR_NAMESPACE_CLOSE_SCOPE
