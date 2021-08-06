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

#include "utils.h"

#include "config.h"
#include "mesh.h"

#include <render/nodes.h>
#include <subd/subd_dice.h>
#include <subd/subd_split.h>
#include <util/util_path.h>
#include <util/util_transform.h>

#include <pxr/base/tf/stringUtils.h>
#include <pxr/imaging/hd/extComputationUtils.h>
#include <pxr/usd/sdf/assetPath.h>

#ifdef USE_HBOOST
#    include <hboost/filesystem.hpp>
#else
#    include <boost/filesystem.hpp>
#endif

PXR_NAMESPACE_OPEN_SCOPE

/* =========- Texture ========== */

bool
HdCyclesPathIsUDIM(const ccl::string& a_filepath)
{
#ifndef USD_HAS_UDIM_RESOLVE_FIX
    // Added precheck to ensure no UDIM is accepted with relative path
    BOOST_NS::filesystem::path filepath(a_filepath);
    if (filepath.is_relative())
        return false;
#endif
    return a_filepath.find("<UDIM>") != std::string::npos;
}

// TODO: Investigate getting these tiles from uv data
// The cycles function ImageTextureNode::cull_tiles does not properly load tiles
// in an interactive session when not provided by Blender. We could assume these
// tiles based on uv primvars, but I have a feeling the material loading happens
// before the mesh syncing. More rnd needs to be done.
void
HdCyclesParseUDIMS(const ccl::string& a_filepath, ccl::vector<int>& a_tiles)
{
    BOOST_NS::filesystem::path filepath(a_filepath);

    size_t offset = filepath.stem().string().find("<UDIM>");
    std::string baseFileName = filepath.stem().string().substr(0, offset);

    std::vector<std::string> files;
    try {
        BOOST_NS::filesystem::path path(ccl::path_dirname(a_filepath));
        if (BOOST_NS::filesystem::is_directory(path)) {
            for (BOOST_NS::filesystem::directory_iterator it(path); it != BOOST_NS::filesystem::directory_iterator();
                 ++it) {
                try {
                    if (BOOST_NS::filesystem::is_regular_file(it->status())
                        || BOOST_NS::filesystem::is_symlink(it->status())) {
                        std::string foundFile = BOOST_NS::filesystem::basename(it->path().filename());

                        if (baseFileName == (foundFile.substr(0, offset))) {
                            files.push_back(foundFile);
                        }
                    }
                } catch (BOOST_NS::exception& e) {
                    TF_WARN("Filesystem error in HdCyclesParseUDIMS() when parsing file %s",it->path().filename().c_str());
                }
            }
        }
    } catch (BOOST_NS::exception& e) {
        TF_WARN("Filesystem error in HdCyclesParseUDIMS() when parsing directory %s", a_filepath.c_str());
    }

    a_tiles.clear();

    if (files.empty()) {
        TF_WARN("Could not find any tiles for UDIM texture %s", a_filepath.c_str());
        return;
    }

    for (std::string file : files) {
        a_tiles.push_back(atoi(file.substr(offset, offset + 3).c_str()));
    }
}

void
HdCyclesMeshTextureSpace(ccl::Geometry* a_geom, ccl::float3& a_loc, ccl::float3& a_size)
{
    // m_cyclesMesh->compute_bounds must be called before this
    a_loc = (a_geom->bounds.max + a_geom->bounds.min) / 2.0f;
    a_size = (a_geom->bounds.max - a_geom->bounds.min) / 2.0f;

    if (a_size.x != 0.0f)
        a_size.x = 0.5f / a_size.x;
    if (a_size.y != 0.0f)
        a_size.y = 0.5f / a_size.y;
    if (a_size.z != 0.0f)
        a_size.z = 0.5f / a_size.z;

    a_loc = a_loc * a_size - ccl::make_float3(0.5f, 0.5f, 0.5f);
}

/* ========== Material ========== */

ccl::Shader*
HdCyclesCreateDefaultShader()
{
    ccl::Shader* shader = new ccl::Shader();

    shader->graph = new ccl::ShaderGraph();

    ccl::VertexColorNode* vc = new ccl::VertexColorNode();
    vc->layer_name = ccl::ustring("displayColor");

    ccl::PrincipledBsdfNode* bsdf = new ccl::PrincipledBsdfNode();

    shader->graph->add(bsdf);
    shader->graph->add(vc);

    ccl::ShaderNode* out = shader->graph->output();
    shader->graph->connect(vc->output("Color"), bsdf->input("Base Color"));
    shader->graph->connect(bsdf->output("BSDF"), out->input("Surface"));

    return shader;
}

ccl::Shader*
HdCyclesCreateObjectColorSurface()
{
    auto shader = new ccl::Shader();
    shader->graph = new ccl::ShaderGraph();

    auto oi = new ccl::ObjectInfoNode {};
    auto bsdf = new ccl::PrincipledBsdfNode();

    shader->graph->add(bsdf);
    shader->graph->add(oi);

    ccl::ShaderNode* out = shader->graph->output();
    shader->graph->connect(oi->output("Color"), bsdf->input("Base Color"));
    shader->graph->connect(bsdf->output("BSDF"), out->input("Surface"));

    return shader;
}

ccl::Shader*
HdCyclesCreateAttribColorSurface()
{
    auto shader = new ccl::Shader();
    shader->graph = new ccl::ShaderGraph();

    auto attrib = new ccl::AttributeNode {};
    attrib->attribute = "displayColor";

    auto bsdf = new ccl::PrincipledBsdfNode();

    shader->graph->add(bsdf);
    shader->graph->add(attrib);

    ccl::ShaderNode* out = shader->graph->output();
    shader->graph->connect(attrib->output("Color"), bsdf->input("Base Color"));
    shader->graph->connect(bsdf->output("BSDF"), out->input("Surface"));

    return shader;
}


// They should be mappable to usd geom tokens, but not sure
// if it's available in an hydra delegate
const char*
_HdInterpolationStr(const HdInterpolation& i)
{
    switch (i) {
    case HdInterpolationConstant: return "Constant";
    case HdInterpolationUniform: return "Uniform";
    case HdInterpolationVarying: return "Varying";
    case HdInterpolationFaceVarying: return "FaceVarying";
    case HdInterpolationVertex: return "Vertex";
    default: return "Unknown";
    }
}

bool
_DumpGraph(ccl::ShaderGraph* shaderGraph, const char* name)
{
    if (!shaderGraph)
        return false;

    static const HdCyclesConfig& config = HdCyclesConfig::GetInstance();

    if (config.cycles_shader_graph_dump_dir.size() > 0) {
        std::string dump_location = config.cycles_shader_graph_dump_dir + "/" + TfMakeValidIdentifier(name)
                                    + "_graph.txt";
        std::cout << "Dumping shader graph: " << dump_location << '\n';
        try {
            shaderGraph->dump_graph(dump_location.c_str());
            return true;
        } catch (...) {
            std::cout << "Couldn't dump shadergraph: " << dump_location << "\n";
        }
    }
    return false;
}

/* ========= Conversion ========= */

// TODO: Make this function more robust
// Along with making point sampling more robust
// UPDATE:
// This causes a known slowdown to deforming motion blur renders
// This will be addressed in an upcoming PR
// UPDATE:
// The function now resamples the transforms at uniform intervals
// rendering more correctly.
HdTimeSampleArray<GfMatrix4d, HD_CYCLES_MOTION_STEPS>
HdCyclesSetTransform(ccl::Object* object, HdSceneDelegate* delegate, const SdfPath& id, bool use_motion)
{
    if (!object)
        return {};

    HdTimeSampleArray<GfMatrix4d, HD_CYCLES_MOTION_STEPS> xf {};

    // Assumes that they are ordered
    delegate->SampleTransform(id, &xf);
    size_t sampleCount = xf.count;

    if (sampleCount == 0) {
        object->tfm = ccl::transform_identity();
        return xf;
    }

    object->tfm = mat4d_to_transform(xf.values.data()[0]);
    if (sampleCount == 1) {
        return xf;
    }

    if (!use_motion) {
        return xf;
    }

    if (object->geometry && object->geometry->motion_steps == sampleCount) {
        object->geometry->use_motion_blur = true;

        if (object->geometry->type == ccl::Geometry::MESH) {
            auto mesh = dynamic_cast<ccl::Mesh*>(object->geometry);
            if (mesh->transform_applied)
                mesh->need_update = true;
        }

        // Rounding to odd number of samples to have one in the center
        const size_t sampleOffset = (sampleCount % 2) ? 0 : 1;
        const size_t numMotionSteps = sampleCount + static_cast<size_t>(sampleOffset);
        const float motionStepSize = (xf.times.back() - xf.times.front()) / static_cast<float>((numMotionSteps - 1));
        object->motion.resize(numMotionSteps, ccl::transform_empty());

        // For each step, we use the available data from the neighbors
        // to calculate the transforms at uniform steps
        for (size_t i = 0; i < numMotionSteps; ++i) {
            const float stepTime = xf.times.front() + motionStepSize * static_cast<float>(i);

            // We always have the transforms at the boundaries
            if (i == 0 || i == numMotionSteps - 1) {
                object->motion[i] = mat4d_to_transform(xf.values.data()[i]);
                continue;
            }

            // Find closest left/right neighbors
            float prevTimeDiff = -INFINITY, nextTimeDiff = INFINITY;
            int iXfPrev = -1, iXfNext = -1;
            for (int j = 0; j < sampleCount; ++j) {
                // If we only have three samples, we prefer to recalculate
                // the intermediate one as the left/right are calculated
                // using linear interpolation, leading to artifacts
                if (i != 1 && (xf.times.data()[j] - stepTime) < 1e-5f) {
                    iXfPrev = iXfNext = j;
                    break;
                }

                const float stepTimeDiff = xf.times.data()[j] - stepTime;
                if (stepTimeDiff < 0 && stepTimeDiff > prevTimeDiff) {
                    iXfPrev = j;
                    prevTimeDiff = stepTimeDiff;
                } else if (stepTimeDiff > 0 && stepTimeDiff < nextTimeDiff) {
                    iXfNext = j;
                    nextTimeDiff = stepTimeDiff;
                }
            }
            assert(iXfPrev != -1 && iXfNext != -1);

            // If there is an authored sample for this specific timestep
            // we copy it.
            if (iXfPrev == iXfNext) {
                object->motion[i] = mat4d_to_transform(xf.values.data()[iXfPrev]);
            }
            // Otherwise we interpolate the neighboring matrices
            else {
                // Should the type conversion be precomputed?
                ccl::Transform xfPrev = mat4d_to_transform(xf.values.data()[iXfPrev]);
                ccl::Transform xfNext = mat4d_to_transform(xf.values.data()[iXfNext]);

                ccl::DecomposedTransform dxf[2];
                transform_motion_decompose(dxf + 0, &xfPrev, 1);
                transform_motion_decompose(dxf + 1, &xfNext, 1);

                // Preferring the smaller rotation difference
                if (ccl::len_squared(dxf[0].x - dxf[1].x) > ccl::len_squared(dxf[0].x + dxf[1].x)) {
                    dxf[1].x = -dxf[1].x;
                }

                // Weighting by distance to sample
                const float timeDiff = xf.times.data()[iXfNext] - xf.times.data()[iXfPrev];
                const float t = (stepTime - xf.times.data()[iXfPrev]) / timeDiff;

                transform_motion_array_interpolate(&object->motion[i], dxf, 2, t);
            }

            if (::std::fabs(stepTime) < 1e-5f) {
                object->tfm = object->motion[i];
            }
        }
    }

    return xf;
}

ccl::Transform
HdCyclesExtractTransform(HdSceneDelegate* delegate, const SdfPath& id)
{
    constexpr size_t maxSamples = 2;
    HdTimeSampleArray<GfMatrix4d, maxSamples> xf {};

    delegate->SampleTransform(id, &xf);

    return mat4d_to_transform(xf.values[0]);
}

GfMatrix4d
ConvertCameraTransform(const GfMatrix4d& a_cameraTransform)
{
    GfMatrix4d viewToWorldCorrectionMatrix(1.0);

    GfMatrix4d flipZ(1.0);
    flipZ[2][2] = -1.0;
    viewToWorldCorrectionMatrix = flipZ * viewToWorldCorrectionMatrix;

    return viewToWorldCorrectionMatrix * a_cameraTransform;
}

ccl::Transform
mat4d_to_transform(const GfMatrix4d& mat)
{
    ccl::Transform outTransform = ccl::transform_identity();

    outTransform.x.x = static_cast<float>(mat[0][0]);
    outTransform.x.y = static_cast<float>(mat[1][0]);
    outTransform.x.z = static_cast<float>(mat[2][0]);
    outTransform.x.w = static_cast<float>(mat[3][0]);

    outTransform.y.x = static_cast<float>(mat[0][1]);
    outTransform.y.y = static_cast<float>(mat[1][1]);
    outTransform.y.z = static_cast<float>(mat[2][1]);
    outTransform.y.w = static_cast<float>(mat[3][1]);

    outTransform.z.x = static_cast<float>(mat[0][2]);
    outTransform.z.y = static_cast<float>(mat[1][2]);
    outTransform.z.z = static_cast<float>(mat[2][2]);
    outTransform.z.w = static_cast<float>(mat[3][2]);

    return outTransform;
}

ccl::Transform
mat4f_to_transform(const GfMatrix4f& mat)
{
    ccl::Transform outTransform = ccl::transform_identity();

    outTransform.x.x = static_cast<float>(mat[0][0]);
    outTransform.x.y = static_cast<float>(mat[1][0]);
    outTransform.x.z = static_cast<float>(mat[2][0]);
    outTransform.x.w = static_cast<float>(mat[3][0]);

    outTransform.y.x = static_cast<float>(mat[0][1]);
    outTransform.y.y = static_cast<float>(mat[1][1]);
    outTransform.y.z = static_cast<float>(mat[2][1]);
    outTransform.y.w = static_cast<float>(mat[3][1]);

    outTransform.z.x = static_cast<float>(mat[0][2]);
    outTransform.z.y = static_cast<float>(mat[1][2]);
    outTransform.z.z = static_cast<float>(mat[2][2]);
    outTransform.z.w = static_cast<float>(mat[3][2]);

    return outTransform;
}

ccl::int2
vec2i_to_int2(const GfVec2i& a_vec)
{
    return ccl::make_int2(a_vec[0], a_vec[1]);
}

GfVec2i
int2_to_vec2i(const ccl::int2& a_int)
{
    return GfVec2i(a_int.x, a_int.y);
}

GfVec2f
int2_to_vec2f(const ccl::int2& a_int)
{
    return GfVec2f(static_cast<float>(a_int.x), static_cast<float>(a_int.y));
}

ccl::float2
vec2f_to_float2(const GfVec2f& a_vec)
{
    return ccl::make_float2(a_vec[0], a_vec[1]);
}

ccl::int2
vec2f_to_int2(const GfVec2f& a_vec)
{
    return ccl::make_int2(static_cast<int>(a_vec[0]), static_cast<int>(a_vec[1]));
}

ccl::float2
vec2i_to_float2(const GfVec2i& a_vec)
{
    return ccl::make_float2(static_cast<float>(a_vec[0]), static_cast<float>(a_vec[1]));
}

ccl::float2
vec2d_to_float2(const GfVec2d& a_vec)
{
    return ccl::make_float2(static_cast<float>(a_vec[0]), static_cast<float>(a_vec[1]));
}

ccl::float2
vec3f_to_float2(const GfVec3f& a_vec)
{
    return ccl::make_float2(a_vec[0], a_vec[1]);
}

ccl::float3
float_to_float3(const float& a_vec)
{
    return ccl::make_float3(a_vec, a_vec, a_vec);
}

ccl::float3
vec2f_to_float3(const GfVec2f& a_vec)
{
    return ccl::make_float3(a_vec[0], a_vec[1], 0.0f);
}

ccl::float3
vec3f_to_float3(const GfVec3f& a_vec)
{
    return ccl::make_float3(a_vec[0], a_vec[1], a_vec[2]);
}

ccl::float3
vec3i_to_float3(const GfVec3i& a_vec)
{
    return ccl::make_float3(static_cast<float>(a_vec[0]), static_cast<float>(a_vec[1]), static_cast<float>(a_vec[2]));
}

ccl::float3
vec3d_to_float3(const GfVec3d& a_vec)
{
    return ccl::make_float3(static_cast<float>(a_vec[0]), static_cast<float>(a_vec[1]), static_cast<float>(a_vec[2]));
}

ccl::float3
vec4f_to_float3(const GfVec4f& a_vec)
{
    return ccl::make_float3(a_vec[0], a_vec[1], a_vec[2]);
}

ccl::float4
vec1f_to_float4(const float& a_val)
{
    return ccl::make_float4(a_val, a_val, a_val, a_val);
}

ccl::float4
vec2f_to_float4(const GfVec2f& a_vec, float a_z, float a_alpha)
{
    return ccl::make_float4(a_vec[0], a_vec[1], a_z, a_alpha);
}

ccl::float4
vec3f_to_float4(const GfVec3f& a_vec, float a_alpha)
{
    return ccl::make_float4(a_vec[0], a_vec[1], a_vec[2], a_alpha);
}

ccl::float4
vec4f_to_float4(const GfVec4f& a_vec)
{
    return ccl::make_float4(a_vec[0], a_vec[1], a_vec[2], a_vec[3]);
}

ccl::float4
vec4i_to_float4(const GfVec4i& a_vec)
{
    return ccl::make_float4(static_cast<float>(a_vec[0]), static_cast<float>(a_vec[1]), static_cast<float>(a_vec[2]),
                            static_cast<float>(a_vec[3]));
}

ccl::float4
vec4d_to_float4(const GfVec4d& a_vec)
{
    return ccl::make_float4(static_cast<float>(a_vec[0]), static_cast<float>(a_vec[1]), static_cast<float>(a_vec[2]),
                            static_cast<float>(a_vec[3]));
}

/* ========= Primvars ========= */

const std::array<HdInterpolation, HdInterpolationCount> interpolations {
    HdInterpolationConstant, HdInterpolationUniform,     HdInterpolationVarying,
    HdInterpolationVertex,   HdInterpolationFaceVarying, HdInterpolationInstance,
};

inline void
_HdCyclesInsertPrimvar(HdCyclesPrimvarMap& primvars, const TfToken& name, const TfToken& role,
                       HdInterpolation interpolation, const VtValue& value)
{
    auto it = primvars.find(name);
    if (it == primvars.end()) {
        primvars.insert({ name, { value, role, interpolation } });
    } else {
        it->second.value = value;
        it->second.role = role;
        it->second.interpolation = interpolation;
        it->second.dirtied = true;
    }
}

// Get Computed primvars
bool
HdCyclesGetComputedPrimvars(HdSceneDelegate* a_delegate, const SdfPath& a_id, HdDirtyBits a_dirtyBits,
                            HdCyclesPrimvarMap& a_primvars)
{
    // First we are querying which primvars need to be computed, and storing them in a list to rely
    // on the batched computation function in HdExtComputationUtils.
    HdExtComputationPrimvarDescriptorVector dirtyPrimvars;
    for (HdInterpolation interpolation : interpolations) {
        auto computedPrimvars = a_delegate->GetExtComputationPrimvarDescriptors(a_id, interpolation);
        for (const auto& primvar : computedPrimvars) {
            if (HdChangeTracker::IsPrimvarDirty(a_dirtyBits, a_id, primvar.name)) {
                dirtyPrimvars.emplace_back(primvar);
            }
        }
    }

    // Early exit.
    if (dirtyPrimvars.empty()) {
        return false;
    }

    auto changed = false;
    auto valueStore = HdExtComputationUtils::GetComputedPrimvarValues(dirtyPrimvars, a_delegate);
    for (const auto& primvar : dirtyPrimvars) {
        const auto itComputed = valueStore.find(primvar.name);
        if (itComputed == valueStore.end()) {
            continue;
        }
        changed = true;
        _HdCyclesInsertPrimvar(a_primvars, primvar.name, primvar.role, primvar.interpolation, itComputed->second);
    }

    return changed;
}

// Get Non-computed primvars
bool
HdCyclesGetPrimvars(HdSceneDelegate* a_delegate, const SdfPath& a_id, HdDirtyBits a_dirtyBits,
                    bool a_multiplePositionKeys, HdCyclesPrimvarMap& a_primvars)
{
    for (auto interpolation : interpolations) {
        const auto primvarDescs = a_delegate->GetPrimvarDescriptors(a_id, interpolation);
        for (const auto& primvarDesc : primvarDescs) {
            if (primvarDesc.name == HdTokens->points) {
                continue;
            }
            // The number of motion keys has to be matched between points and normals, so
            _HdCyclesInsertPrimvar(a_primvars, primvarDesc.name, primvarDesc.role, primvarDesc.interpolation,
                                   (a_multiplePositionKeys && primvarDesc.name == HdTokens->normals)
                                       ? VtValue {}
                                       : a_delegate->Get(a_id, primvarDesc.name));
        }
    }

    return true;
}


void
HdCyclesPopulatePrimvarDescsPerInterpolation(HdSceneDelegate* a_sceneDelegate, SdfPath const& a_id,
                                             HdCyclesPDPIMap* a_primvarDescsPerInterpolation)
{
    if (!a_primvarDescsPerInterpolation->empty()) {
        return;
    }

    auto hd_interpolations = {
        HdInterpolationConstant, HdInterpolationUniform,     HdInterpolationVarying,
        HdInterpolationVertex,   HdInterpolationFaceVarying, HdInterpolationInstance,
    };
    for (auto& interpolation : hd_interpolations) {
        a_primvarDescsPerInterpolation->emplace(interpolation,
                                                a_sceneDelegate->GetPrimvarDescriptors(a_id, interpolation));
    }
}

bool
HdCyclesIsPrimvarExists(TfToken const& a_name, HdCyclesPDPIMap const& a_primvarDescsPerInterpolation,
                        HdInterpolation* a_interpolation)
{
    for (auto& entry : a_primvarDescsPerInterpolation) {
        for (auto& pv : entry.second) {
            if (pv.name == a_name) {
                if (a_interpolation) {
                    *a_interpolation = entry.first;
                }
                return true;
            }
        }
    }
    return false;
}

template<>
inline float
to_cycles<float>(const float& v) noexcept
{
    return v;
}
template<>
inline float
to_cycles<double>(const double& v) noexcept
{
    return static_cast<float>(v);
}
template<>
inline float
to_cycles<int>(const int& v) noexcept
{
    return static_cast<float>(v);
}


template<>
inline ccl::float2
to_cycles<GfVec2f>(const GfVec2f& v) noexcept
{
    return ccl::make_float2(v[0], v[1]);
}
template<>
inline ccl::float2
to_cycles<GfVec2h>(const GfVec2h& v) noexcept
{
    return ccl::make_float2(static_cast<float>(v[0]), static_cast<float>(v[1]));
}
template<>
inline ccl::float2
to_cycles<GfVec2d>(const GfVec2d& v) noexcept
{
    return ccl::make_float2(static_cast<float>(v[0]), static_cast<float>(v[1]));
}
template<>
inline ccl::float2
to_cycles<GfVec2i>(const GfVec2i& v) noexcept
{
    return ccl::make_float2(static_cast<float>(v[0]), static_cast<float>(v[1]));
}

template<>
inline ccl::float3
to_cycles<GfVec3f>(const GfVec3f& v) noexcept
{
    return ccl::make_float3(v[0], v[1], v[2]);
}
template<>
inline ccl::float3
to_cycles<GfVec3h>(const GfVec3h& v) noexcept
{
    return ccl::make_float3(static_cast<float>(v[0]), static_cast<float>(v[1]), static_cast<float>(v[2]));
}
template<>
inline ccl::float3
to_cycles<GfVec3d>(const GfVec3d& v) noexcept
{
    return ccl::make_float3(static_cast<float>(v[0]), static_cast<float>(v[1]), static_cast<float>(v[2]));
}
template<>
inline ccl::float3
to_cycles<GfVec3i>(const GfVec3i& v) noexcept
{
    return ccl::make_float3(static_cast<float>(v[0]), static_cast<float>(v[1]), static_cast<float>(v[2]));
}

template<>
inline ccl::float4
to_cycles<GfVec4f>(const GfVec4f& v) noexcept
{
    return ccl::make_float4(v[0], v[1], v[2], v[3]);
}
template<>
inline ccl::float4
to_cycles<GfVec4h>(const GfVec4h& v) noexcept
{
    return ccl::make_float4(static_cast<float>(v[0]), static_cast<float>(v[1]), static_cast<float>(v[2]),
                            static_cast<float>(v[3]));
}
template<>
inline ccl::float4
to_cycles<GfVec4d>(const GfVec4d& v) noexcept
{
    return ccl::make_float4(static_cast<float>(v[0]), static_cast<float>(v[1]), static_cast<float>(v[2]),
                            static_cast<float>(v[3]));
}
template<>
inline ccl::float4
to_cycles<GfVec4i>(const GfVec4i& v) noexcept
{
    return ccl::make_float4(static_cast<float>(v[0]), static_cast<float>(v[1]), static_cast<float>(v[2]),
                            static_cast<float>(v[3]));
}

/* ========= MikkTSpace ========= */

struct MikkUserData {
    MikkUserData(const char* layer_name, ccl::Mesh* mesh_in, ccl::float3* tangent_in, float* tangent_sign_in)
        : mesh(mesh_in)
        , corner_normal(NULL)
        , vertex_normal(NULL)
        , texface(NULL)
        , tangent(tangent_in)
        , tangent_sign(tangent_sign_in)
    {
        const ccl::AttributeSet& attributes = (mesh->subd_faces.size()) ? mesh->subd_attributes : mesh->attributes;

        ccl::Attribute* attr_vN = attributes.find(ccl::ATTR_STD_VERTEX_NORMAL);
        ccl::Attribute* attr_cN = attributes.find(ccl::ATTR_STD_CORNER_NORMAL);

        if (!attr_vN && !attr_cN) {
            mesh->add_face_normals();
            mesh->add_vertex_normals();
            attr_vN = attributes.find(ccl::ATTR_STD_VERTEX_NORMAL);
        }

        // This preference depends on what Cycles does inside the hood.
        // Works for now, but there should be a more clear way of knowing
        // which normals are used for rendering.
        if (attr_cN) {
            corner_normal = attr_cN->data_float3();
        } else {
            vertex_normal = attr_vN->data_float3();
        }

        ccl::Attribute* attr_uv = attributes.find(ccl::ustring(layer_name));
        if (attr_uv != NULL) {
            texface = attr_uv->data_float2();
        }
    }

    ccl::Mesh* mesh;
    int num_faces;

    ccl::float3* corner_normal;
    ccl::float3* vertex_normal;
    ccl::float2* texface;

    ccl::float3* tangent;
    float* tangent_sign;
};

int
mikk_get_num_faces(const SMikkTSpaceContext* context)
{
    auto userdata = static_cast<const MikkUserData*>(context->m_pUserData);
    if (userdata->mesh->subd_faces.size()) {
        return static_cast<int>(userdata->mesh->subd_faces.size());
    } else {
        return static_cast<int>(userdata->mesh->num_triangles());
    }
}

int
mikk_get_num_verts_of_face(const SMikkTSpaceContext* context, const int face_num)
{
    auto userdata = static_cast<const MikkUserData*>(context->m_pUserData);
    if (userdata->mesh->subd_faces.size()) {
        const ccl::Mesh* mesh = userdata->mesh;
        return mesh->subd_faces[face_num].num_corners;
    } else {
        return 3;
    }
}

int
mikk_vertex_index(const ccl::Mesh* mesh, const int face_num, const int vert_num)
{
    if (mesh->subd_faces.size()) {
        const ccl::Mesh::SubdFace& face = mesh->subd_faces[face_num];
        return mesh->subd_face_corners[face.start_corner + vert_num];
    } else {
        return mesh->triangles[face_num * 3 + vert_num];
    }
}

int
mikk_corner_index(const ccl::Mesh* mesh, const int face_num, const int vert_num)
{
    if (mesh->subd_faces.size()) {
        const ccl::Mesh::SubdFace& face = mesh->subd_faces[face_num];
        return face.start_corner + vert_num;
    } else {
        return face_num * 3 + vert_num;
    }
}

void
mikk_get_position(const SMikkTSpaceContext* context, float P[3], const int face_num, const int vert_num)
{
    const MikkUserData* userdata = static_cast<const MikkUserData*>(context->m_pUserData);
    const ccl::Mesh* mesh = userdata->mesh;
    const int vertex_index = mikk_vertex_index(mesh, face_num, vert_num);
    const ccl::float3 vP = mesh->verts[vertex_index];
    P[0] = vP.x;
    P[1] = vP.y;
    P[2] = vP.z;
}

void
mikk_get_texture_coordinate(const SMikkTSpaceContext* context, float uv[2], const int face_num, const int vert_num)
{
    const MikkUserData* userdata = static_cast<const MikkUserData*>(context->m_pUserData);
    const ccl::Mesh* mesh = userdata->mesh;
    if (userdata->texface != NULL) {
        const int corner_index = mikk_corner_index(mesh, face_num, vert_num);
        ccl::float2 tfuv = userdata->texface[corner_index];
        uv[0] = tfuv.x;
        uv[1] = tfuv.y;
    } else {
        uv[0] = 0.0f;
        uv[1] = 0.0f;
    }
}

void
mikk_get_normal(const SMikkTSpaceContext* context, float N[3], const int face_num, const int vert_num)
{
    const MikkUserData* userdata = static_cast<const MikkUserData*>(context->m_pUserData);
    const ccl::Mesh* mesh = userdata->mesh;
    ccl::float3 vN;

    if (mesh->subd_faces.size()) {
        const ccl::Mesh::SubdFace& face = mesh->subd_faces[face_num];
        if (userdata->corner_normal) {
            vN = userdata->corner_normal[face.start_corner + vert_num];
        } else if (face.smooth) {
            const int vertex_index = mikk_vertex_index(mesh, face_num, vert_num);
            vN = userdata->vertex_normal[vertex_index];
        } else {
            vN = face.normal(mesh);
        }
    } else {
        if (userdata->corner_normal) {
            vN = userdata->corner_normal[face_num * 3 + vert_num];
        } else if (mesh->smooth[face_num]) {
            const int vertex_index = mikk_vertex_index(mesh, face_num, vert_num);
            vN = userdata->vertex_normal[vertex_index];
        } else {
            const ccl::Mesh::Triangle tri = mesh->get_triangle(face_num);
            vN = tri.compute_normal(&mesh->verts[0]);
        }
    }
    N[0] = vN.x;
    N[1] = vN.y;
    N[2] = vN.z;
}

void
mikk_set_tangent_space(const SMikkTSpaceContext* context, const float T[], const float sign, const int face_num,
                       const int vert_num)
{
    MikkUserData* userdata = static_cast<MikkUserData*>(context->m_pUserData);
    const ccl::Mesh* mesh = userdata->mesh;
    const int corner_index = mikk_corner_index(mesh, face_num, vert_num);
    userdata->tangent[corner_index] = ccl::make_float3(T[0], T[1], T[2]);
    if (userdata->tangent_sign != NULL) {
        userdata->tangent_sign[corner_index] = sign;
    }
}

void
mikk_compute_tangents(const char* layer_name, ccl::Mesh* mesh, bool need_sign, bool active_render)
{
    /* Create tangent attributes. */
    ccl::AttributeSet& attributes = (mesh->subd_faces.size()) ? mesh->subd_attributes : mesh->attributes;
    ccl::Attribute* attr;
    ccl::ustring name;

    if (layer_name != NULL) {
        name = ccl::ustring((std::string(layer_name) + ".tangent").c_str());
    } else {
        name = ccl::ustring("orco.tangent");
    }

    if (active_render) {
        attr = attributes.add(ccl::ATTR_STD_UV_TANGENT, name);
    } else {
        attr = attributes.add(name, ccl::TypeDesc::TypeVector, ccl::ATTR_ELEMENT_CORNER);
    }
    ccl::float3* tangent = attr->data_float3();
    /* Create bitangent sign attribute. */
    float* tangent_sign = NULL;
    if (need_sign) {
        ccl::Attribute* attr_sign;
        ccl::ustring name_sign;

        if (layer_name != NULL) {
            name_sign = ccl::ustring((std::string(layer_name) + ".tangent_sign").c_str());
        } else {
            name_sign = ccl::ustring("orco.tangent_sign");
        }

        if (active_render) {
            attr_sign = attributes.add(ccl::ATTR_STD_UV_TANGENT_SIGN, name_sign);
        } else {
            attr_sign = attributes.add(name_sign, ccl::TypeDesc::TypeFloat, ccl::ATTR_ELEMENT_CORNER);
        }
        tangent_sign = attr_sign->data_float();
    }
    /* Setup userdata. */
    MikkUserData userdata(layer_name, mesh, tangent, tangent_sign);
    /* Setup interface. */
    SMikkTSpaceInterface sm_interface;
    memset(&sm_interface, 0, sizeof(sm_interface));
    sm_interface.m_getNumFaces = mikk_get_num_faces;
    sm_interface.m_getNumVerticesOfFace = mikk_get_num_verts_of_face;
    sm_interface.m_getPosition = mikk_get_position;
    sm_interface.m_getTexCoord = mikk_get_texture_coordinate;
    sm_interface.m_getNormal = mikk_get_normal;
    sm_interface.m_setTSpaceBasic = mikk_set_tangent_space;
    /* Setup context. */
    SMikkTSpaceContext context;
    memset(&context, 0, sizeof(context));
    context.m_pUserData = &userdata;
    context.m_pInterface = &sm_interface;
    /* Compute tangents. */
    genTangSpaceDefault(&context);
}

template<>
bool
_HdCyclesGetVtValue<bool>(VtValue a_value, bool a_default, bool* a_hasChanged, bool a_checkWithDefault)
{
    bool val = a_default;
    if (!a_value.IsEmpty()) {
        if (a_value.IsHolding<bool>()) {
            if (!a_checkWithDefault && a_hasChanged)
                *a_hasChanged = true;
            val = a_value.UncheckedGet<bool>();
        } else if (a_value.IsHolding<int>()) {
            if (!a_checkWithDefault && a_hasChanged)
                *a_hasChanged = true;
            val = static_cast<bool>(a_value.UncheckedGet<int>());
        } else if (a_value.IsHolding<float>()) {
            if (!a_checkWithDefault && a_hasChanged)
                val = (a_value.UncheckedGet<float>() == 1.0f);
        } else if (a_value.IsHolding<double>()) {
            if (!a_checkWithDefault && a_hasChanged)
                *a_hasChanged = true;
            val = (a_value.UncheckedGet<double>() == 1.0);
        }
    }
    if (a_hasChanged && a_checkWithDefault && val != a_default)
        *a_hasChanged = true;
    return val;
}

PXR_NAMESPACE_CLOSE_SCOPE
