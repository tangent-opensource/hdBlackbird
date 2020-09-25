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

#include "basisCurves.h"

#include "config.h"
#include "material.h"
#include "renderParam.h"
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

PXR_NAMESPACE_OPEN_SCOPE

// TODO: Read these from usdCycles schema
// clang-format off
TF_DEFINE_PRIVATE_TOKENS(_tokens,
    ((cyclesCurveStyle, "cycles:object:curve_style"))
    ((cyclesCurveResolution, "cycles:object:curve_resolution"))
);
// clang-format on

HdCyclesBasisCurves::HdCyclesBasisCurves(
    SdfPath const& id, SdfPath const& instancerId,
    HdCyclesRenderDelegate* a_renderDelegate)
    : HdBasisCurves(id, instancerId)
    , m_cyclesObject(nullptr)
    , m_cyclesMesh(nullptr)
    , m_cyclesGeometry(nullptr)
    , m_cyclesHair(nullptr)
    , m_curveStyle(CURVE_TUBE)
    , m_curveResolution(5)
    , m_renderDelegate(a_renderDelegate)
{
    static const HdCyclesConfig& config = HdCyclesConfig::GetInstance();
    m_useMotionBlur                     = config.enable_motion_blur;

    m_numTransformSamples = HD_CYCLES_MOTION_STEPS;

    if (m_useMotionBlur) {
        m_motionSteps = m_numTransformSamples;
    }

    m_cyclesObject = _CreateObject();
    m_renderDelegate->GetCyclesRenderParam()->AddObject(m_cyclesObject);
}

HdCyclesBasisCurves::~HdCyclesBasisCurves()
{
    if (m_cyclesHair) {
        m_renderDelegate->GetCyclesRenderParam()->RemoveCurve(m_cyclesHair);
        delete m_cyclesHair;
    }
    if (m_cyclesMesh) {
        m_renderDelegate->GetCyclesRenderParam()->RemoveMesh(m_cyclesMesh);
        delete m_cyclesMesh;
    }
    if (m_cyclesObject) {
        m_renderDelegate->GetCyclesRenderParam()->RemoveObject(m_cyclesObject);
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
    ccl::Scene* scene = ((HdCyclesRenderParam*)renderParam)->GetCyclesScene();

    static const HdCyclesConfig& config = HdCyclesConfig::GetInstance();

    if (config.use_old_curves) {
        if (m_curveStyle == CURVE_RIBBONS) {
            _CreateRibbons(scene->camera);
        } else {
            _CreateTubeMesh();
        }
    } else {
        _CreateCurves(scene);
    }
}


void
HdCyclesBasisCurves::_PopulateMotion()
{
    m_cyclesGeometry->use_motion_blur = true;

    m_cyclesGeometry->motion_steps = m_pointSamples.count + 1;

    ccl::Attribute* attr_mP = m_cyclesGeometry->attributes.find(
        ccl::ATTR_STD_MOTION_VERTEX_POSITION);

    //if(attr_mP)
    //m_cyclesMesh->attributes.remove(attr_mP);

    if (!attr_mP) {
        attr_mP = m_cyclesGeometry->attributes.add(
            ccl::ATTR_STD_MOTION_VERTEX_POSITION);
    }

    ccl::float3* mP = attr_mP->data_float3();
    for (size_t i = 0; i < m_pointSamples.count; ++i) {
        VtVec3fArray pp;
        pp = m_pointSamples.values.data()[i].Get<VtVec3fArray>();

        for (size_t j = 0; j < m_points.size(); ++j, ++mP) {
            *mP = vec3f_to_float3(pp[j]);
        }
    }
}

void
HdCyclesBasisCurves::_AddColors(TfToken name, VtValue value,
                                HdInterpolation interpolation)
{
    ccl::ustring attribName = ccl::ustring(name.GetString());

    int vecSize   = 0;
    int numColors = 0;

    VtFloatArray colors1f;
    VtVec2fArray colors2f;
    VtVec3fArray colors3f;
    VtVec4fArray colors4f;

    if (value.IsHolding<VtArray<GfVec3f>>()) {
        colors3f  = value.UncheckedGet<VtArray<GfVec3f>>();
        vecSize   = 3;
        numColors = colors3f.size();
    } else if (value.IsHolding<VtArray<GfVec4f>>()) {
        colors4f  = value.UncheckedGet<VtArray<GfVec4f>>();
        vecSize   = 4;
        numColors = colors4f.size();
    } else if (value.IsHolding<VtArray<GfVec2f>>()) {
        colors2f  = value.UncheckedGet<VtArray<GfVec2f>>();
        vecSize   = 2;
        numColors = colors2f.size();
    } else if (value.IsHolding<VtArray<float>>()) {
        colors1f  = value.UncheckedGet<VtArray<float>>();
        vecSize   = 1;
        numColors = colors1f.size();
    }

    if (vecSize == 0)
        return;

    if (interpolation == HdInterpolationUniform) {
        if (m_cyclesHair) {
            ccl::Attribute* attr_vcol = m_cyclesHair->attributes.add(
                attribName, ccl::TypeDesc::TypeColor, ccl::ATTR_ELEMENT_CURVE);

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

                    fdata[i++] = ccl::color_srgb_to_linear_v3(color);
                }
            }
        } else {
            // @TODO: Unhandled support for deprecated curve mesh geo
            ccl::Attribute* attr_vcol
                = m_cyclesMesh->attributes.add(attribName,
                                               ccl::TypeDesc::TypeColor,
                                               ccl::ATTR_ELEMENT_CORNER_BYTE);

            ccl::uchar4* cdata = attr_vcol->data_uchar4();
        }
    } else if (interpolation == HdInterpolationVertex) {
        VtIntArray curveVertexCounts = m_topology.GetCurveVertexCounts();
        if (m_cyclesHair) {
            // Support for vertex varying attributes is not supported in Cycles hair.
            // For now we just get the root value and apply to the whole strand...
            ccl::Attribute* attr_vcol = m_cyclesHair->attributes.add(
                attribName, ccl::TypeDesc::TypeColor, ccl::ATTR_ELEMENT_CURVE);

            ccl::float3* fdata = attr_vcol->data_float3();

            if (fdata) {
                int curveOffset = 0;
                for (int i = 0; i < curveVertexCounts.size(); i++) {
                    ccl::float3 color;

                    switch (vecSize) {
                    case 1:
                        color = float_to_float3(colors1f[curveOffset]);
                        break;
                    case 2:
                        color = vec2f_to_float3(colors2f[curveOffset]);
                        break;
                    case 3:
                        color = vec3f_to_float3(colors3f[curveOffset]);
                        break;
                    case 4:
                        color = vec4f_to_float3(colors4f[curveOffset]);
                        break;
                    }

                    fdata[i] = ccl::color_srgb_to_linear_v3(color);

                    curveOffset += curveVertexCounts[i];
                }
            }
        } else {
            // @TODO: Unhandled support for deprecated curve mesh geo
            ccl::Attribute* attr_vcol
                = m_cyclesMesh->attributes.add(attribName,
                                               ccl::TypeDesc::TypeColor,
                                               ccl::ATTR_ELEMENT_CORNER_BYTE);

            ccl::uchar4* cdata = attr_vcol->data_uchar4();
        }
    }
}

void
HdCyclesBasisCurves::_AddUVS(TfToken name, VtValue value,
                             HdInterpolation interpolation)
{
    ccl::ustring attribName = ccl::ustring(name.GetString());

    // Add hair uvs
    ccl::Attribute* attr_uv;
    ccl::float2* uv;

    if (interpolation == HdInterpolationUniform) {
        if (value.IsHolding<VtArray<GfVec2f>>()) {
            VtVec2fArray uvs;
            uvs = value.UncheckedGet<VtArray<GfVec2f>>();
            if (m_cyclesHair) {
                attr_uv = m_cyclesHair->attributes.add(ccl::ATTR_STD_UV,
                                                       attribName);

                uv = attr_uv->data_float2();
                if (uv) {
                    size_t i = 0;

                    for (size_t curve = 0; curve < uvs.size(); curve++) {
                        uv[i++] = vec2f_to_float2(uvs[curve]);
                    }
                }
            } else {
                // @TODO: Unhandled support for deprecated curve mesh geo
                attr_uv = m_cyclesMesh->attributes.add(ccl::ATTR_STD_UV,
                                                       attribName);

                uv = attr_uv->data_float2();
            }
        } else if (value.IsHolding<VtArray<GfVec3f>>()) {
            VtVec3fArray uvs;
            uvs = value.UncheckedGet<VtArray<GfVec3f>>();
            if (m_cyclesHair) {
                attr_uv = m_cyclesHair->attributes.add(ccl::ATTR_STD_UV,
                                                       attribName);

                uv = attr_uv->data_float2();
                if (uv) {
                    size_t i = 0;

                    for (size_t curve = 0; curve < uvs.size(); curve++) {
                        uv[i++] = vec3f_to_float2(uvs[curve]);
                    }
                }
            } else {
                // @TODO: Unhandled support for deprecated curve mesh geo
                attr_uv = m_cyclesMesh->attributes.add(ccl::ATTR_STD_UV,
                                                       attribName);

                uv = attr_uv->data_float2();
            }
        }
    } else if (interpolation == HdInterpolationVertex) {
        VtIntArray curveVertexCounts = m_topology.GetCurveVertexCounts();

        if (value.IsHolding<VtArray<GfVec2f>>()) {
            VtVec2fArray uvs;
            uvs = value.UncheckedGet<VtArray<GfVec2f>>();
            if (m_cyclesHair) {
                // Support for vertex varying attributes is not supported in Cycles hair.
                // For now we just get the root value and apply to the whole strand...
                attr_uv = m_cyclesHair->attributes.add(ccl::ATTR_STD_UV,
                                                       attribName);

                uv = attr_uv->data_float2();
                if (uv) {
                    int curveOffset = 0;
                    for (int i = 0; i < curveVertexCounts.size(); i++) {
                        uv[i] = vec2f_to_float2(uvs[curveOffset]);
                        curveOffset += curveVertexCounts[i];
                    }
                }
            } else {
                // @TODO: Unhandled support for deprecated curve mesh geo
                attr_uv = m_cyclesMesh->attributes.add(ccl::ATTR_STD_UV,
                                                       attribName);

                uv = attr_uv->data_float2();
            }
        } else if (value.IsHolding<VtArray<GfVec3f>>()) {
            VtVec3fArray uvs;
            uvs = value.UncheckedGet<VtArray<GfVec3f>>();
            if (m_cyclesHair) {
                // Support for vertex varying attributes is not supported in Cycles hair.
                // For now we just get the root value and apply to the whole strand...
                attr_uv = m_cyclesHair->attributes.add(ccl::ATTR_STD_UV,
                                                       attribName);

                uv = attr_uv->data_float2();
                if (uv) {
                    int curveOffset = 0;
                    for (int i = 0; i < curveVertexCounts.size(); i++) {
                        uv[i] = vec3f_to_float2(uvs[curveOffset]);
                        curveOffset += curveVertexCounts[i];
                    }
                }
            } else {
                // @TODO: Unhandled support for deprecated curve mesh geo
                attr_uv = m_cyclesMesh->attributes.add(ccl::ATTR_STD_UV,
                                                       attribName);

                uv = attr_uv->data_float2();
            }
        }
    }
}

void
HdCyclesBasisCurves::_AddGenerated()
{
    if (!m_cyclesObject)
        return;

    ccl::float3 loc, size;

    // @TODO: The implementation of this function is broken
    HdCyclesMeshTextureSpace(ccl::transform_inverse(m_cyclesObject->tfm), loc, size);

    if (m_cyclesMesh) {
        ccl::Attribute* attr_generated = m_cyclesMesh->attributes.add(
            ccl::ATTR_STD_GENERATED);
        ccl::float3* generated = attr_generated->data_float3();

        for (size_t i = 0; i < m_cyclesMesh->verts.size(); i++)
            generated[i] = m_cyclesMesh->verts[i] * size - loc;
    } else {
        ccl::Attribute* attr_generated = m_cyclesHair->attributes.add(
            ccl::ATTR_STD_GENERATED);
        ccl::float3* generated = attr_generated->data_float3();

        for (size_t i = 0; i < m_cyclesHair->num_curves(); i++) {
            ccl::float3 co
                = m_cyclesHair->curve_keys[m_cyclesHair->get_curve(i).first_key];
            generated[i] = co * size - loc;
        }
    }
}

void
HdCyclesBasisCurves::Sync(HdSceneDelegate* sceneDelegate,
                          HdRenderParam* renderParam, HdDirtyBits* dirtyBits,
                          TfToken const& reprSelector)
{
    SdfPath const& id = GetId();

    HdCyclesRenderParam* param = (HdCyclesRenderParam*)renderParam;

    ccl::Scene* scene = param->GetCyclesScene();

    HdCyclesPDPIMap pdpi;
    bool generate_new_curve = false;
    bool update_curve       = false;

    if (*dirtyBits & HdChangeTracker::DirtyPoints) {
        HdCyclesPopulatePrimvarDescsPerInterpolation(sceneDelegate, id, &pdpi);
        if (HdCyclesIsPrimvarExists(HdTokens->points, pdpi)) {
            m_points
                = sceneDelegate->Get(id, HdTokens->points).Get<VtVec3fArray>();
            generate_new_curve = true;

            sceneDelegate->SamplePrimvar(id, HdTokens->points, &m_pointSamples);
        } else {
            m_points = VtVec3fArray();
        }
    }

    if (*dirtyBits & HdChangeTracker::DirtyNormals) {
        HdCyclesPopulatePrimvarDescsPerInterpolation(sceneDelegate, id, &pdpi);
        if (HdCyclesIsPrimvarExists(HdTokens->normals, pdpi)) {
            m_normals
                = sceneDelegate->Get(id, HdTokens->normals).Get<VtVec3fArray>();
            generate_new_curve = true;
        } else {
            m_normals = VtVec3fArray();
        }
    }

    if (*dirtyBits & HdChangeTracker::DirtyTopology) {
        m_topology = sceneDelegate->GetBasisCurvesTopology(id);
        m_indices  = VtIntArray();
        if (m_topology.HasIndices()) {
            m_indices = m_topology.GetCurveIndices();
        }
        generate_new_curve = true;
    }

    if (*dirtyBits & HdChangeTracker::DirtyWidths) {
        HdCyclesPopulatePrimvarDescsPerInterpolation(sceneDelegate, id, &pdpi);
        if (HdCyclesIsPrimvarExists(HdTokens->widths, pdpi,
                                    &m_widthsInterpolation)) {
            m_widths
                = sceneDelegate->Get(id, HdTokens->widths).Get<VtFloatArray>();
        } else {
            m_widths              = VtFloatArray(1, 0.1f);
            m_widthsInterpolation = HdInterpolationConstant;
            TF_WARN(
                "[%s] Curve do not have widths. Fallback value is 1.0f with a constant interpolation",
                id.GetText());
        }
        generate_new_curve = true;
    }

    if (*dirtyBits & HdChangeTracker::DirtyPrimvar) {
        HdCyclesPopulatePrimvarDescsPerInterpolation(sceneDelegate, id, &pdpi);
        if (HdCyclesIsPrimvarExists(_tokens->cyclesCurveStyle, pdpi)) {
            VtIntArray type = sceneDelegate->Get(id, _tokens->cyclesCurveStyle)
                                  .Get<VtIntArray>();
            if (type.size() > 0) {
                if (type[0] == 0)
                    m_curveStyle = CURVE_RIBBONS;
                else
                    m_curveStyle = CURVE_TUBE;
            }
        } else {
            m_curveStyle = CURVE_TUBE;
        }

        if (HdCyclesIsPrimvarExists(_tokens->cyclesCurveResolution, pdpi)) {
            VtIntArray resolution
                = sceneDelegate->Get(id, _tokens->cyclesCurveResolution)
                      .Get<VtIntArray>();
            if (resolution.size() > 0) {
                m_curveResolution = resolution[0];
            }
        } else {
            m_curveResolution = 5;
        }
    }

    if (*dirtyBits & HdChangeTracker::DirtyVisibility) {
        update_curve = true;
        if (sceneDelegate->GetVisible(id)) {
            m_cyclesObject->visibility |= ccl::PATH_RAY_ALL_VISIBILITY;
        } else {
            m_cyclesObject->visibility &= ~ccl::PATH_RAY_ALL_VISIBILITY;
        }
    }

    if (*dirtyBits & HdChangeTracker::DirtyTransform) {
        m_transformSamples = HdCyclesSetTransform(m_cyclesObject, sceneDelegate,
                                                  id, m_useMotionBlur);

        generate_new_curve = true;
    }

    if (generate_new_curve) {
        _PopulateCurveMesh(param);

        if (m_cyclesGeometry) {
            m_cyclesObject->geometry = m_cyclesGeometry;

            m_cyclesGeometry->compute_bounds();

            _AddGenerated();

            param->AddCurve(m_cyclesGeometry);
        }

        _PopulateMotion();
    }

    if (*dirtyBits & HdChangeTracker::DirtyPrimvar) {
        HdCyclesPopulatePrimvarDescsPerInterpolation(sceneDelegate, id, &pdpi);

        for (auto& primvarDescsEntry : pdpi) {
            for (auto& pv : primvarDescsEntry.second) {
                if (HdChangeTracker::IsPrimvarDirty(*dirtyBits, id, pv.name)) {
                    VtValue value = GetPrimvar(sceneDelegate, pv.name);
                    if (pv.role == HdPrimvarRoleTokens->textureCoordinate) {
                        _AddUVS(pv.name, value, primvarDescsEntry.first);
                    } else {
                        _AddColors(pv.name, value, primvarDescsEntry.first);
                    }
                }
            }
        }
    }

    if (*dirtyBits & HdChangeTracker::DirtyMaterialId) {
        if (m_cyclesGeometry) {
            // Add default shader
            const SdfPath& materialId = sceneDelegate->GetMaterialId(GetId());
            const HdCyclesMaterial* material
                = static_cast<const HdCyclesMaterial*>(
                    sceneDelegate->GetRenderIndex().GetSprim(
                        HdPrimTypeTokens->material, materialId));

            if (material && material->GetCyclesShader()) {
                m_cyclesGeometry->used_shaders.push_back(
                    material->GetCyclesShader());
                material->GetCyclesShader()->tag_update(scene);
            } else {
                m_cyclesGeometry->used_shaders.push_back(
                    scene->default_surface);
            }
            update_curve = true;
        }
    }

    if (generate_new_curve || update_curve) {
        m_cyclesGeometry->tag_update(scene, true);
        param->Interrupt();
    }

    *dirtyBits = HdChangeTracker::Clean;
}

void
HdCyclesBasisCurves::_CreateCurves(ccl::Scene* a_scene)
{
    m_cyclesHair     = new ccl::Hair();
    m_cyclesGeometry = m_cyclesHair;

    // Get USD Curve Metadata
    VtIntArray curveVertexCounts = m_topology.GetCurveVertexCounts();
    TfToken curveType            = m_topology.GetCurveType();
    TfToken curveBasis           = m_topology.GetCurveBasis();
    TfToken curveWrap            = m_topology.GetCurveWrap();

    int num_curves = curveVertexCounts.size();
    int num_keys   = 0;

    for (int i = 0; i < num_curves; i++) {
        num_keys += curveVertexCounts[i];
    }

    ccl::Attribute* attr_intercept = NULL;
    ccl::Attribute* attr_random    = NULL;

    attr_intercept = m_cyclesHair->attributes.add(
        ccl::ATTR_STD_CURVE_INTERCEPT);

    attr_random = m_cyclesHair->attributes.add(ccl::ATTR_STD_CURVE_RANDOM);

    m_cyclesHair->reserve_curves(num_curves, num_keys);

    num_curves = 0;
    num_keys   = 0;

    int currentPointCount = 0;

    // For every curve
    for (int i = 0; i < curveVertexCounts.size(); i++) {
        size_t num_curve_keys = 0;

        // For every section
        for (int j = 0; j < curveVertexCounts[i]; j++) {
            int idx = j + currentPointCount;

            const float time = (float)j / (float)curveVertexCounts[i];

            if (idx > m_points.size()) {
                TF_WARN("Attempted to access invalid point. Continuing");
                continue;
            }

            ccl::float3 usd_location = vec3f_to_float3(m_points[idx]);

            float radius = 0.1f;
            if (idx < m_widths.size())
                radius = m_widths[idx];

            m_cyclesHair->add_curve_key(usd_location, radius);

            if (attr_intercept)
                attr_intercept->add(time);

            num_curve_keys++;
        }

        if (attr_random != NULL) {
            attr_random->add(ccl::hash_uint2_to_float(num_curves, 0));
        }

        m_cyclesHair->add_curve(num_keys, 0);
        num_keys += num_curve_keys;
        currentPointCount += curveVertexCounts[i];
        num_curves++;
    }

    if ((m_cyclesHair->curve_keys.size() != num_keys)
        || (m_cyclesHair->num_curves() != num_curves)) {
        TF_WARN("Allocation failed. Clearing data");

        m_cyclesHair->clear();
    }
}

void
HdCyclesBasisCurves::_CreateRibbons(ccl::Camera* a_camera)
{
    m_cyclesMesh     = new ccl::Mesh();
    m_cyclesGeometry = m_cyclesMesh;

    bool isCameraOriented = false;

    ccl::float3 RotCam;
    bool is_ortho;
    if (m_normals.size() <= 0) {
        if (a_camera != nullptr) {
            isCameraOriented     = true;
            ccl::Transform& ctfm = a_camera->matrix;
            if (a_camera->type == ccl::CAMERA_ORTHOGRAPHIC) {
                RotCam = -ccl::make_float3(ctfm.x.z, ctfm.y.z, ctfm.z.z);
            } else {
                ccl::Transform tfm  = m_cyclesObject->tfm;
                ccl::Transform itfm = ccl::transform_quick_inverse(tfm);
                RotCam              = ccl::transform_point(
                    &itfm, ccl::make_float3(ctfm.x.w, ctfm.y.w, ctfm.z.w));
            }
            is_ortho = a_camera->type == ccl::CAMERA_ORTHOGRAPHIC;
        }
    }

    // Get USD Curve Metadata
    VtIntArray curveVertexCounts = m_topology.GetCurveVertexCounts();
    TfToken curveType            = m_topology.GetCurveType();
    TfToken curveBasis           = m_topology.GetCurveBasis();
    TfToken curveWrap            = m_topology.GetCurveWrap();

    int num_vertices = 0;
    int num_tris     = 0;
    for (int i = 0; i < curveVertexCounts.size(); i++) {
        num_vertices += curveVertexCounts[i] * 2;
        num_tris += ((curveVertexCounts[i] - 1) * 2);
    }

    // Start Cycles Mesh population
    int vertexindex = 0;

    m_cyclesMesh->reserve_mesh(num_vertices, num_tris);

    // For every curve
    for (int i = 0; i < curveVertexCounts.size(); i++) {
        ccl::float3 xbasis;
        ccl::float3 v1;

        ccl::float3 ickey_loc = vec3f_to_float3(m_points[0]);

        float radius = 0.1f;
        if (m_widths.size() > 0)
            radius = m_widths[0];

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
            int first_idx = (i * curveVertexCounts[i]);
            int idx       = j + (i * curveVertexCounts[i]);

            ickey_loc = vec3f_to_float3(m_points[idx]);

            if (j == 0) {
                // subv = 0;
                // First curve point
                v1 = vec3f_to_float3(m_points[idx]
                                     - m_points[std::max(idx - 1, first_idx)]);
            } else {
                v1 = vec3f_to_float3(m_points[idx + 1] - m_points[idx - 1]);
            }


            float radius = 0.1f;
            if (idx < m_widths.size())
                radius = m_widths[idx];

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
            ccl::float3 ickey_loc_shfl = ickey_loc - radius * xbasis;
            ccl::float3 ickey_loc_shfr = ickey_loc + radius * xbasis;
            m_cyclesMesh->add_vertex(ickey_loc_shfl);
            m_cyclesMesh->add_vertex(ickey_loc_shfr);
            m_cyclesMesh->add_triangle(vertexindex - 2, vertexindex,
                                       vertexindex - 1, 0, true);
            m_cyclesMesh->add_triangle(vertexindex + 1, vertexindex - 1,
                                       vertexindex, 0, true);
            vertexindex += 2;
        }
    }

    // TODO: Implement texcoords
}

void
HdCyclesBasisCurves::_CreateTubeMesh()
{
    m_cyclesMesh     = new ccl::Mesh();
    m_cyclesGeometry = m_cyclesMesh;

    // Get USD Curve Metadata
    VtIntArray curveVertexCounts = m_topology.GetCurveVertexCounts();
    TfToken curveType            = m_topology.GetCurveType();
    TfToken curveBasis           = m_topology.GetCurveBasis();
    TfToken curveWrap            = m_topology.GetCurveWrap();

    int num_vertices = 0;
    int num_tris     = 0;
    for (int i = 0; i < curveVertexCounts.size(); i++) {
        num_vertices += curveVertexCounts[i] * m_curveResolution;
        num_tris += ((curveVertexCounts[i] - 1) * 2 * m_curveResolution);
    }

    // Start Cycles Mesh population
    int vertexindex = m_curveResolution;

    m_cyclesMesh->reserve_mesh(num_vertices, num_tris);

    // For every curve
    for (int i = 0; i < curveVertexCounts.size(); i++) {
        int subv = 1;

        ccl::float3 firstxbasis = ccl::cross(ccl::make_float3(1.0f, 0.0f, 0.0f),
                                             vec3f_to_float3(m_points[1])
                                                 - vec3f_to_float3(m_points[0]));

        if (!ccl::is_zero(firstxbasis))
            firstxbasis = ccl::normalize(firstxbasis);
        else
            firstxbasis = ccl::normalize(
                ccl::cross(ccl::make_float3(0.0f, 1.0f, 0.0f),
                           vec3f_to_float3(m_points[1])
                               - vec3f_to_float3(m_points[0])));

        // For every section
        for (int j = 0; j < curveVertexCounts[i]; j++) {
            int first_idx = (i * curveVertexCounts[i]);
            int idx       = j + (i * curveVertexCounts[i]);

            ccl::float3 xbasis = firstxbasis;
            ccl::float3 v1;
            ccl::float3 v2;

            if (j == 0) {
                // First curve point
                v1 = vec3f_to_float3(
                    m_points[std::min(idx + 2, (curveVertexCounts[i]
                                                + curveVertexCounts[i] - 1))]);
                v2 = vec3f_to_float3(m_points[idx + 1] - m_points[idx]);
            } else if (j == (curveVertexCounts[i] - 1)) {
                // Last curve point
                v1 = vec3f_to_float3(m_points[idx] - m_points[idx - 1]);
                v2 = vec3f_to_float3(
                    m_points[idx - 1]
                    - m_points[std::max(idx - 2, first_idx)]);  // First key
            } else {
                v1 = vec3f_to_float3(m_points[idx + 1] - m_points[idx]);
                v2 = vec3f_to_float3(m_points[idx] - m_points[idx - 1]);
            }

            xbasis = ccl::cross(v1, v2);

            if (ccl::len_squared(xbasis)
                >= 0.05f * ccl::len_squared(v1) * ccl::len_squared(v2)) {
                firstxbasis = ccl::normalize(xbasis);
                break;
            }
        }

        // For every section
        for (int j = 0; j < curveVertexCounts[i]; j++) {
            int first_idx = (i * curveVertexCounts[i]);
            int idx       = j + (i * curveVertexCounts[i]);
            ccl::float3 xbasis;
            ccl::float3 ybasis;
            ccl::float3 v1;
            ccl::float3 v2;

            ccl::float3 usd_location = vec3f_to_float3(m_points[idx]);

            if (j == 0) {
                // First curve point
                v1 = vec3f_to_float3(
                    m_points[std::min(idx + 2, (curveVertexCounts[i] - 1))]
                    - m_points[idx + 1]);
                v2 = vec3f_to_float3(m_points[idx + 1] - m_points[idx]);
            } else if (j == (curveVertexCounts[i] - 1)) {
                v1 = vec3f_to_float3(m_points[idx] - m_points[idx - 1]);
                v2 = vec3f_to_float3(m_points[idx - 1]
                                     - m_points[std::max(idx - 2, first_idx)]);
            } else {
                v1 = vec3f_to_float3(m_points[idx + 1] - m_points[idx]);
                v1 = vec3f_to_float3(m_points[idx] - m_points[idx - 1]);
            }

            // Add vertex in circle
            float radius = 0.1f;
            if (idx < m_widths.size())
                radius = m_widths[idx];

            float angle = M_2PI_F / (float)m_curveResolution;

            xbasis = ccl::cross(v1, v2);

            if (ccl::len_squared(xbasis)
                >= 0.05f * ccl::len_squared(v1) * ccl::len_squared(v2)) {
                xbasis      = ccl::normalize(xbasis);
                firstxbasis = xbasis;
            } else {
                xbasis = firstxbasis;
            }

            ybasis = ccl::normalize(ccl::cross(xbasis, v2));

            // Add vertices
            for (int k = 0; k < m_curveResolution; k++) {
                ccl::float3 vertex_location = usd_location
                                              + radius
                                                    * (cosf(angle * k) * xbasis
                                                       + sinf(angle * k)
                                                             * ybasis);

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
    return HdChangeTracker::DirtyTopology | HdChangeTracker::DirtyPoints
           | HdChangeTracker::DirtyNormals | HdChangeTracker::DirtyWidths
           | HdChangeTracker::DirtyPrimvar | HdChangeTracker::DirtyTransform
           | HdChangeTracker::DirtyVisibility
           | HdChangeTracker::DirtyMaterialId;
}

bool
HdCyclesBasisCurves::IsValid() const
{
    return true;
}

PXR_NAMESPACE_CLOSE_SCOPE