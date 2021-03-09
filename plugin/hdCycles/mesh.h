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

#ifndef HD_CYCLES_MESH_H
#define HD_CYCLES_MESH_H

#include "utils.h"

#include "hdcycles.h"

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

/**
 * @brief HdCycles Mesh Rprim mapped to Cycles mesh
 * 
 */
class HdCyclesMesh final : public HdMesh {
public:
    HF_MALLOC_TAG_NEW("new HdCyclesMesh");

    /**
     * @brief Construct a new HdCycles Mesh object
     * 
     * @param id Path to the Mesh Primitive
     * @param instancerId If specified the HdInstancer at this id uses this mesh
     * as a prototype
     */
    HdCyclesMesh(SdfPath const& id, SdfPath const& instancerId,
                 HdCyclesRenderDelegate* a_renderDelegate);

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
    void Sync(HdSceneDelegate* sceneDelegate, HdRenderParam* renderParam,
              HdDirtyBits* dirtyBits, TfToken const& reprToken) override;

protected:
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
     * @brief Comptue Mikktspace tangents
     * 
     * @param needsign 
     */
    void _ComputeTangents(bool needsign);

    /**
     * @brief Add abitrary uv set
     * 
     * @param name 
     * @param uvs 
     * @param interpolation 
     */
    void _AddUVSet(TfToken name, VtValue uvs, ccl::Scene* scene,
                   HdInterpolation interpolation, 
                   bool& need_tangent, bool& need_sign);

    /**
     * @brief Add vertex/face normals (Not implemented)
     * 
     * @param normals 
     * @param interpolation 
     */
    void _AddNormals(VtVec3fArray& normals, HdInterpolation interpolation);

    /**
     * @brief Add vertex velocities (Not tested)
     * 
     * @param velocities 
     * @param interpolation 
     */
    void _AddVelocities(VtVec3fArray& velocities,
                        HdInterpolation interpolation);

    /**
     * @brief Add vertex/primitive colors
     * TODO: This handles more than just colors, we should probably refactor
     * 
     * @param name 
     * @param colors 
     * @param scene 
     * @param interpolation 
     */
    void _AddColors(TfToken name, TfToken role, VtValue colors,
                    ccl::Scene* scene, HdInterpolation interpolation);

protected:
    struct PrimvarSource {
        VtValue data;
        HdInterpolation interpolation;
    };
    TfHashMap<TfToken, PrimvarSource, TfToken::HashFunctor> _primvarSourceMap;

private:
    template<typename T>
    bool GetPrimvarData(TfToken const& name, HdSceneDelegate* sceneDelegate,
                        std::map<HdInterpolation, HdPrimvarDescriptorVector>
                            primvarDescsPerInterpolation,
                        VtArray<T>& out_data, VtIntArray& out_indices);

protected:
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
    void _PopulateVertices();

    void _PopulateMotion();

    /**
     * @brief Populate faces of cycles mesh
     * 
     * @param a_faceMaterials pregenerated array of subset materials
     * @param a_subdivide should faces be subdivided
     */
    void _PopulateFaces(const std::vector<int>& a_faceMaterials,
                        bool a_subdivide);

    /**
     * @brief Populate subdiv creases
     * 
     */
    void _PopulateCreases();

    /**
     * @brief Populate generated coordinates attribute
     * 
     */
    void _PopulateGenerated(ccl::Scene* scene);

    ccl::Mesh* m_cyclesMesh;
    ccl::Object* m_cyclesObject;
    std::vector<ccl::Object*> m_cyclesInstances;

    std::map<SdfPath, int> m_materialMap;

    size_t m_numMeshVerts = 0;
    size_t m_numMeshFaces = 0;

    SdfPath m_cachedMaterialId;
    int m_numTransformSamples;
    HdTimeSampleArray<GfMatrix4d, HD_CYCLES_MOTION_STEPS> m_transformSamples;

    HdMeshTopology m_topology;
    HdGeomSubsets m_geomSubsets;
    VtVec3fArray m_points;
    VtIntArray m_faceVertexCounts;
    VtIntArray m_faceVertexIndices;
    TfToken m_orientation;

    HdCyclesSampledPrimvarType m_pointSamples;

    float m_velocityScale;

    bool m_useSubdivision = false;
    bool m_subdivEnabled  = false;
    int m_maxSubdivision  = 12;
    float m_dicingRate    = 0.1f;

    int m_numNgons;
    int m_numCorners;

    int m_numTriFaces;

    Hd_VertexAdjacency m_adjacency;
    bool m_adjacencyValid = false;

    VtVec3fArray m_normals;
    VtIntArray m_normalIndices;
    bool m_normalsValid    = false;
    bool m_authoredNormals = false;
    bool m_smoothNormals   = false;

    VtIntArray m_cornerIndices;
    VtFloatArray m_cornerWeights;
    VtIntArray m_creaseIndices;
    VtIntArray m_creaseLengths;
    VtFloatArray m_creaseWeights;

    TfToken m_normalInterpolation;

    VtVec2fArray m_uvs;
    VtIntArray m_uvIndices;

    HdDisplayStyle m_displayStyle;
    int m_refineLevel  = 0;
    bool m_doubleSided = false;

    bool m_useMotionBlur;
    bool m_useDeformMotionBlur;
    int m_motionSteps;

    unsigned int m_visibilityFlags;

    bool m_visCamera;
    bool m_visDiffuse;
    bool m_visGlossy;
    bool m_visScatter;
    bool m_visShadow;
    bool m_visTransmission;

    bool m_hasVertexColors;

    ccl::vector<ccl::Shader*> m_usedShaders;

public:
    const VtIntArray& GetFaceVertexCounts() { return m_faceVertexCounts; }
    const VtIntArray& GetFaceVertexIndices() { return m_faceVertexIndices; }
    const TfToken& GetOrientation() { return m_orientation; }

private:
    HdCyclesRenderDelegate* m_renderDelegate;
};

PXR_NAMESPACE_CLOSE_SCOPE

#endif  // HD_CYCLES_MESH_H
