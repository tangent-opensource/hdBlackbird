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

/// @file utils.h
///
/// General utilities for Hydra Cycles
#ifndef HD_CYCLES_UTILS_H
#define HD_CYCLES_UTILS_H

#include "api.h"

#include "hdcycles.h"

#include "Mikktspace/mikktspace.h"

#include <render/mesh.h>
#include <render/object.h>
#include <render/scene.h>
#include <render/session.h>
#include <render/shader.h>
#include <util/util_math_float2.h>
#include <util/util_math_float3.h>
#include <util/util_transform.h>

#include <pxr/base/gf/matrix4d.h>
#include <pxr/base/gf/matrix4f.h>
#include <pxr/base/vt/value.h>
#include <pxr/imaging/hd/mesh.h>
#include <pxr/imaging/hd/sceneDelegate.h>
#include <pxr/imaging/hd/timeSampleArray.h>
#include <pxr/pxr.h>

namespace ccl {
class Mesh;
}

PXR_NAMESPACE_OPEN_SCOPE

/* =========- Texture ========== */

HDCYCLES_API
bool
HdCyclesPathIsUDIM(const ccl::string& a_filepath);

HDCYCLES_API
void
HdCyclesParseUDIMS(const ccl::string& a_filepath, ccl::vector<int>& a_tiles);

/* ========== Material ========== */

ccl::Shader*
HdCyclesCreateDefaultShader();

/* ========= Conversion ========= */

/**
 * @brief Create Cycles Transform from given HdSceneDelegate and SdfPath
 *
 * @param delegate
 * @param id
 * @return Cycles Transform
 */
HDCYCLES_API
HdTimeSampleArray<GfMatrix4d, HD_CYCLES_MOTION_STEPS>
HdCyclesSetTransform(ccl::Object* object, HdSceneDelegate* delegate,
                     const SdfPath& id, bool use_motion);

ccl::Transform
HdCyclesExtractTransform(HdSceneDelegate* delegate, const SdfPath& id);

/**
 * @brief Convert GfMatrix4d to Cycles Transform representation
 *
 * @param mat
 * @return ccl::Transform
 */
ccl::Transform
mat4d_to_transform(const GfMatrix4d& mat);

/**
 * @brief Convert GfMatrix4f to Cycles Transform representation
 *
 * @param mat
 * @return ccl::Transform
 */
ccl::Transform
mat4f_to_transform(const GfMatrix4f& mat);

/**
 * @brief Convert GfVec2f to Cycles float2 representation
 *
 * @param a_vec
 * @return Cycles float2
 */
ccl::float2
vec2f_to_float2(const GfVec2f& a_vec);

/**
 * @brief Convert GfVec3f to Cycles float3 representation
 *
 * @param a_vec
 * @return Cycles float3
 */
ccl::float3
vec3f_to_float3(const GfVec3f& a_vec);

/**
 * @brief Lossy convert GfVec4f to Cycles float3 representation
 *
 * @param a_vec
 * @return Cycles float3
 */
ccl::float3
vec4f_to_float3(const GfVec4f& a_vec);

/**
 * @brief Convert GfVec3f to Cycles float4 representation with alpha option
 *
 * @param a_vec
 * @param a_alpha
 * @return Cycles float4
 */
ccl::float4
vec3f_to_float4(const GfVec3f& a_vec, float a_alpha = 1.0f);

/**
 * @brief Convert GfVec4f to Cycles float4 representation
 *
 * @param a_vec
 * @return Cycles float4
 */
ccl::float4
vec4f_to_float4(const GfVec4f& a_vec);

/* ========= Primvars ========= */

// HdCycles primvar handling. Designed reference based on HdArnold implementation

struct HdCyclesPrimvar {
    VtValue value;                  // Copy-on-write value of the primvar
    TfToken role;                   // Role of the primvar
    HdInterpolation interpolation;  // Type of interpolation used for the value
    bool dirtied;                   // If the primvar has been dirtied

    HdCyclesPrimvar(const VtValue& a_value, const TfToken& a_role,
                    HdInterpolation a_interpolation)
        : value(a_value)
        , role(a_role)
        , interpolation(a_interpolation)
        , dirtied(true)
    {
    }
};

using HdCyclesPrimvarMap
    = std::unordered_map<TfToken, HdCyclesPrimvar, TfToken::HashFunctor>;

// Get Computed primvars
bool
HdCyclesGetComputedPrimvars(HdSceneDelegate* a_delegate, const SdfPath& a_id,
                            HdDirtyBits a_dirtyBits,
                            HdCyclesPrimvarMap& a_primvars);

// Get Non-computed primvars
bool
HdCyclesGetPrimvars(HdSceneDelegate* a_delegate, const SdfPath& a_id,
                    HdDirtyBits a_dirtyBits, bool a_multiplePositionKeys,
                    HdCyclesPrimvarMap& a_primvars);

typedef std::map<HdInterpolation, HdPrimvarDescriptorVector> HdCyclesPDPIMap;

void
HdCyclesPopulatePrimvarDescsPerInterpolation(
    HdSceneDelegate* a_sceneDelegate, SdfPath const& a_id,
    HdCyclesPDPIMap* a_primvarDescsPerInterpolation);

bool
HdCyclesIsPrimvarExists(TfToken const& a_name,
                        HdCyclesPDPIMap const& a_primvarDescsPerInterpolation,
                        HdInterpolation* a_interpolation = nullptr);

/* ======== VtValue Utils ========= */


template<typename F>
void
_CheckForBoolValue(const VtValue& value, F&& f)
{
    if (value.IsHolding<bool>()) {
        f(value.UncheckedGet<bool>());
    } else if (value.IsHolding<int>()) {
        f(value.UncheckedGet<int>() != 0);
    } else if (value.IsHolding<long>()) {
        f(value.UncheckedGet<long>() != 0);
    }
}

template<typename F>
void
_CheckForIntValue(const VtValue& value, F&& f)
{
    if (value.IsHolding<int>()) {
        f(value.UncheckedGet<int>());
    } else if (value.IsHolding<long>()) {
        f(static_cast<int>(value.UncheckedGet<long>()));
    }
}

template<typename F>
void
_CheckForFloatValue(const VtValue& value, F&& f)
{
    if (value.IsHolding<float>()) {
        f(value.UncheckedGet<float>());
    }
}

template<typename F>
void
_CheckForDoubleValue(const VtValue& value, F&& f)
{
    if (value.IsHolding<double>()) {
        f(value.UncheckedGet<double>());
    }
}

template<typename F>
void
_CheckForStringValue(const VtValue& value, F&& f)
{
    if (value.IsHolding<std::string>()) {
        f(value.UncheckedGet<std::string>());
    }
}

template<typename F>
void
_CheckForVec2iValue(const VtValue& value, F&& f)
{
    if (value.IsHolding<pxr::GfVec2i>()) {
        f(value.UncheckedGet<pxr::GfVec2i>());
    }
}

// Get value

template<typename T>
T
_HdCyclesGetVtValue(VtValue a_value, T a_default, bool* a_hasChanged = nullptr,
                    bool a_checkWithDefault = false)
{
    if (!a_value.IsEmpty()) {
        if (a_value.IsHolding<T>()) {
            T val = a_value.UncheckedGet<T>();
            if (a_checkWithDefault && val != a_default)
                *a_hasChanged = true;
            else
                *a_hasChanged = true;
            return val;
        }
    }
    return a_default;
}

// Bool specialization

template<>
bool
_HdCyclesGetVtValue<bool>(VtValue a_value, bool a_default, bool* a_hasChanged,
                          bool a_checkWithDefault);

// Get abitrary param

template<typename T>
T
_HdCyclesGetParam(HdSceneDelegate* a_scene, SdfPath a_id, TfToken a_token,
                  T a_default)
{
    VtValue val = a_scene->Get(a_id, a_token);
    return _HdCyclesGetVtValue<T>(val, a_default);
}

// Get mesh param

template<typename T>
T
_HdCyclesGetMeshParam(HdDirtyBits* a_dirtyBits, const SdfPath& a_id,
                      HdMesh* a_mesh, HdSceneDelegate* a_scene, TfToken a_token,
                      T a_default)
{
    if (HdChangeTracker::IsPrimvarDirty(*a_dirtyBits, a_id, a_token)) {
        VtValue v;
        v = a_mesh->GetPrimvar(a_scene, a_token);
        return _HdCyclesGetVtValue<T>(v, a_default);
    }
    return a_default;
}

// Get light param

template<typename T>
T
_HdCyclesGetLightParam(const SdfPath& a_id, HdSceneDelegate* a_scene,
                       TfToken a_token, T a_default)
{
    VtValue v = a_scene->GetLightParamValue(a_id, a_token);
    return _HdCyclesGetVtValue<T>(v, a_default);
}

// Get camera param

template<typename T>
T
_HdCyclesGetCameraParam(HdSceneDelegate* a_scene, SdfPath a_id, TfToken a_token,
                        T a_default)
{
    VtValue v = a_scene->GetCameraParamValue(a_id, a_token);
    return _HdCyclesGetVtValue<T>(v, a_default);
}

/* ========= MikkTSpace ========= */

int
mikk_get_num_faces(const SMikkTSpaceContext* context);

int
mikk_get_num_verts_of_face(const SMikkTSpaceContext* context,
                           const int face_num);

int
mikk_vertex_index(const ccl::Mesh* mesh, const int face_num,
                  const int vert_num);

int
mikk_corner_index(const ccl::Mesh* mesh, const int face_num,
                  const int vert_num);

void
mikk_get_position(const SMikkTSpaceContext* context, float P[3],
                  const int face_num, const int vert_num);

void
mikk_get_texture_coordinate(const SMikkTSpaceContext* context, float uv[2],
                            const int face_num, const int vert_num);

void
mikk_get_normal(const SMikkTSpaceContext* context, float N[3],
                const int face_num, const int vert_num);

void
mikk_set_tangent_space(const SMikkTSpaceContext* context, const float T[],
                       const float sign, const int face_num,
                       const int vert_num);

void
mikk_compute_tangents(const char* layer_name, ccl::Mesh* mesh, bool need_sign,
                      bool active_render);

PXR_NAMESPACE_CLOSE_SCOPE

#endif  // HD_CYCLES_UTILS_H
