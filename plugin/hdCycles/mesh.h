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

#ifndef HD_BLACKBIRD_MESH_H
#define HD_BLACKBIRD_MESH_H

#include "utils.h"

#include "hdcycles.h"
#include "meshRefiner.h"
#include "objectSource.h"

#include <util/util_transform.h>

#include <pxr/base/gf/matrix4d.h>
#include <pxr/base/gf/matrix4f.h>
#include <pxr/base/gf/vec2f.h>
#include <pxr/base/gf/vec3f.h>
#include <pxr/base/gf/vec3i.h>
#include <pxr/imaging/hd/enums.h>
#include <pxr/imaging/hd/mesh.h>
#include <pxr/imaging/hd/meshUtil.h>
#include <pxr/imaging/hd/vertexAdjacency.h>
#include <pxr/pxr.h>

namespace ccl {
class Scene;
class Mesh;
class Object;
}  // namespace ccl

PXR_NAMESPACE_OPEN_SCOPE

class HdCyclesRenderDelegate;
class HdCyclesMeshRefiner;
class HdCyclesRenderParam;

/**
 * @brief HdCycles Mesh Rprim mapped to Cycles mesh
 * 
 */
class HdCyclesMesh final : public HdMesh {
public:
    HF_MALLOC_TAG_NEW("new HdCyclesMesh")

    /**
     * @brief Construct a new HdCycles Mesh object
     * 
     * @param id Path to the Mesh Primitive
     * @param instancerId If specified the HdInstancer at this id uses this mesh
     * as a prototype
     */
    HdCyclesMesh(SdfPath const& id, SdfPath const& instancerId, HdCyclesRenderDelegate* a_renderDelegate);

    /**
     * @brief Destroy the HdCycles Mesh object
     * 
     */
    virtual ~HdCyclesMesh();

    /**
     * @brief Inform the scene graph which state needs to be downloaded in
     * the first Sync() call
     * 
     * @return The initial dirty state this mesh wants to query
     */
    HdDirtyBits GetInitialDirtyBitsMask() const override;

    /**
     * @brief Pull invalidated mesh data and prepare/update the core Cycles 
     * representation.
     * 
     * This must be thread safe.
     * 
     * @param sceneDelegate The data source for the mesh
     * @param renderParam State
     * @param dirtyBits Which bits of scene data has changed
     * @param reprToken Which representation to draw with
     */
    void Sync(HdSceneDelegate* sceneDelegate, HdRenderParam* renderParam, HdDirtyBits* dirtyBits,
              TfToken const& reprToken) override;

protected:
    void _InitializeNewCyclesMesh();

    /**
     * @brief Create the cycles mesh representation
     * 
     * @return New allocated pointer to ccl::Mesh
     */
    ccl::Mesh* _CreateCyclesMesh();

    /**
     * @brief Create the cycles object representation
     * 
     * @return ccl::Object* 
     */
    ccl::Object* _CreateCyclesObject();

    /**
     * @brief Perform final mesh computations (bounds, tangents, etc)
     * 
     * @param scene 
     */
    void _FinishMesh(ccl::Scene* scene);

    /**
     * @brief Add abitrary uv set
     * 
     * @param name 
     * @param uvs 
     * @param interpolation 
     */
    void _AddUVSet(const TfToken& name, const VtValue& uvs, ccl::Scene* scene, HdInterpolation interpolation);

    /**
     * @brief Add vertex/face normals (Not implemented)
     * 
     * @param normals 
     * @param interpolation 
     */

    /**
     * @brief Add vertex velocities
     * 
     * @param velocities 
     * @param interpolation 
     */
    void _AddVelocities(const SdfPath& id, const VtValue& value, HdInterpolation interpolation);


    /**
     * @brief Add vertex accelerations
     * 
     * @param accelerations 
     * @param interpolation 
     */
    void _AddAccelerations(const SdfPath& id, const VtValue& value, HdInterpolation interpolation);

    /**
     * @brief Add vertex/primitive colors
     * TODO: This handles more than just colors, we should probably refactor
     * 
     * @param name 
     * @param colors 
     * @param scene 
     * @param interpolation 
     */
    void _PopulateColors(const TfToken& name, const TfToken& role, const VtValue& data, ccl::Scene* scene,
                         HdInterpolation interpolation, const SdfPath& id);

private:
    struct PrimvarSource {
        VtValue data;
        HdInterpolation interpolation;
    };
    TfHashMap<TfToken, PrimvarSource, TfToken::HashFunctor> _primvarSourceMap;

    /**
     * @brief Initialize the given representation of this Rprim.
     * This is called prior to syncing the prim.
     * 
     * @param reprToken The name of the repr to initialize
     * @param dirtyBits In/Out dirty values
     */
    void _InitRepr(TfToken const& reprToken, HdDirtyBits* dirtyBits) override;

    /**
     * @brief Set additional dirty bits
     * 
     * @param bits 
     * @return New value of dirty bits
     */
    HdDirtyBits _PropagateDirtyBits(HdDirtyBits bits) const override;

    /**
     * @brief Do not allow this class to be copied
     * 
     */
    HdCyclesMesh(const HdCyclesMesh&) = delete;
    /**
     * @brief Do not allow this class to be assigned
     * 
     */
    HdCyclesMesh& operator=(const HdCyclesMesh&) = delete;

    /**
     * @brief Populate vertices of cycles mesh
     * 
     */

    void _PopulateMotion(HdSceneDelegate* sceneDelegate, const SdfPath& id);

    void _PopulateTopology(HdSceneDelegate* sceneDelegate, const SdfPath& id);
    void _PopulateVertices(HdSceneDelegate* sceneDelegate, const SdfPath& id, HdDirtyBits* dirtyBits);
    void _PopulateNormals(HdSceneDelegate* sceneDelegate, const SdfPath& id);
    void _PopulateTangents(HdSceneDelegate* sceneDelegate, const SdfPath& id, ccl::Scene* scene);

    void _PopulateMaterials(HdSceneDelegate* sceneDelegate, HdCyclesRenderParam* renderParam,
                            ccl::Shader* default_surface, const SdfPath& id);
    void _PopulateObjectMaterial(HdSceneDelegate* sceneDelegate, const SdfPath& id);
    void _PopulateSubSetsMaterials(HdSceneDelegate* sceneDelegate, const SdfPath& id);

    void _PopulatePrimvars(HdSceneDelegate* sceneDelegate, ccl::Scene* scene, const SdfPath& id,
                           HdDirtyBits* dirtyBits);


    void _UpdateObject(ccl::Scene* scene, HdCyclesRenderParam* param, HdDirtyBits* dirtyBits, bool rebuildBvh);

    /**
     * @brief Populate generated coordinates attribute
     * 
     */
    void _PopulateGenerated(ccl::Scene* scene);

    enum DirtyBits : HdDirtyBits {
        DirtyTangents = HdChangeTracker::CustomBitsBegin,
    };

    HdCyclesObjectSourceSharedPtr m_object_source;

    ccl::Mesh* m_cyclesMesh;
    ccl::Object* m_cyclesObject;
    std::vector<ccl::Object> m_cyclesInstances;

    ccl::Shader* m_object_display_color_shader;
    ccl::Shader* m_attrib_display_color_shader;

    HdTimeSampleArray<GfMatrix4d, HD_BLACKBIRD_MOTION_STEPS> m_transformSamples;

    int m_refineLevel;
    std::shared_ptr<HdBbMeshTopology> m_topology;

    float m_velocityScale;

    bool m_motionBlur;
    int m_motionTransformSteps;
    int m_motionDeformSteps;

    unsigned int m_visibilityFlags;

    bool m_visCamera;
    bool m_visDiffuse;
    bool m_visGlossy;
    bool m_visScatter;
    bool m_visShadow;
    bool m_visTransmission;

    std::vector<ccl::ustring> m_texture_names;
    VtFloat3Array m_limit_us;
    VtFloat3Array m_limit_vs;

    HdCyclesRenderDelegate* m_renderDelegate;
};

PXR_NAMESPACE_CLOSE_SCOPE

#endif  // HD_BLACKBIRD_MESH_H
