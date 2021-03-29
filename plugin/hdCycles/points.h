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

#ifndef HD_CYCLES_POINTS_H
#define HD_CYCLES_POINTS_H

#include "api.h"

#include "hdcycles.h"
#include "renderDelegate.h"

#include <util/util_transform.h>

#include <pxr/imaging/hd/points.h>
#include <pxr/pxr.h>

namespace ccl {
class Object;
class Mesh;
class Scene;
class PointCloud;
}  // namespace ccl

PXR_NAMESPACE_OPEN_SCOPE

class HdSceneDelegate;
class HdCyclesRenderDelegate;

enum HdCyclesPointStyle {
    POINT_DISCS,
    POINT_SPHERES,
};

/**
 * @brief HdCycles Points Rprim mapped to Cycles point cloud or mesh instances
 * 
 */
class HdCyclesPoints final : public HdPoints {
public:
    /**
     * @brief Construct a new HdCycles Point object
     * 
     * @param id Path to the Point Primitive
     * @param instancerId If specified the HdInstancer at this id uses this curve
     * as a prototype
     */
    HdCyclesPoints(SdfPath const& id, SdfPath const& instancerId,
                   HdCyclesRenderDelegate* a_renderDelegate);
    /**
     * @brief Destroy the HdCycles Points object
     * 
     */
    virtual ~HdCyclesPoints();

    /**
     * @brief Pull invalidated material data and prepare/update the core Cycles 
     * representation.
     * 
     * This must be thread safe.
     * 
     * @param sceneDelegate The data source for the Point
     * @param renderParam State
     * @param dirtyBits Which bits of scene data has changed
     */
    void Sync(HdSceneDelegate* sceneDelegate, HdRenderParam* renderParam,
              HdDirtyBits* dirtyBits, TfToken const& reprSelector) override;

    /**
     * @brief Inform the scene graph which state needs to be downloaded in
     * the first Sync() call
     * 
     * @return The initial dirty state this Point wants to query
     */
    HdDirtyBits GetInitialDirtyBitsMask() const override;

    /**
     * @return Return true if this light is valid.
     */
    bool IsValid() const;

    /**
     * @brief Not Implemented
     */
    void Finalize(HdRenderParam* renderParam) override;

protected:
    /**
     * @brief Initialize the cycles objects and adds them to the scene
     */
    void _InitializeNewCyclesPointCloud();

    /**
     * @brief Fills in the point positions
     */
    void _PopulatePoints(HdSceneDelegate* sceneDelegate, const SdfPath& id);

    /**
     * @brief Fill in the point widths
     */
    void _PopulateScales(HdSceneDelegate* sceneDelegate, const SdfPath& id);

    /**
     * @brief Fill in optional primvars
     */
    void _PopulatePrimvars(const HdDirtyBits* dirtyBits, HdSceneDelegate* sceneDelegate, const SdfPath& id);

    /**
     * @brief Flag the object for update in the scene
     */
    void _UpdateObject(ccl::Scene* scene, HdCyclesRenderParam* param, HdDirtyBits* dirtyBits);

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

private:
    /**
     * @brief Returns true if the cycles representation is a PointCloud
     * 
     */
    bool _usingPointCloud() const;

    /**
     * @brief Create the cycles points as discs mesh and object representation
     * 
     * @param resolution Resolution of the disc geometry
     * @return New allocated pointer to ccl::Mesh
     */
    void _CreateDiscMesh();

    /**
     * @brief Create the cycles points as spheres mesh and object representation
     * 
     * @param scene Cycles scene to add mesh to
     * @param transform Initial transform for object
     * @return New allocated pointer to ccl::Mesh
     */
    void _CreateSphereMesh();

    /**
     * @brief Create the cycles object for an individual point
     * 
     * @param transform Transform of the point
     * @param mesh Mesh to populate the point with
     * @return ccl::Object* 
     */
    ccl::Object* _CreatePointsObject(const ccl::Transform& transform,
                                     ccl::Mesh* mesh);


    /**
     * @brief Add velocities to the Cycles geometry
     * 
     * @param velocities
     */
    void _AddVelocities(const VtVec3fArray& velocities);

    /**
     * @brief Add accelerations to the Cycles geometry
     * 
     * @param accelerations
     */
    void _AddAccelerations(const VtVec3fArray& accelerations);


    void _AddColors(const VtVec3fArray& colors);
    void _AddAlphas(const VtFloatArray& colors);


    // Control the shape of the primitive
    int m_pointStyle;
    int m_pointResolution;

    // Used if the point style is POINT_SPHERES
    ccl::PointCloud* m_cyclesPointCloud;
    ccl::Object* m_cyclesObject;

    // Used if the point style is not POINT_SPHERES
    ccl::Mesh* m_cyclesMesh;
    std::vector<ccl::Object*> m_cyclesObjects;

    HdCyclesRenderDelegate* m_renderDelegate;

    ccl::Transform m_transform;

    bool m_useMotionBlur;
    int m_motionSteps;


    // -- Currently unused

    HdTimeSampleArray<GfMatrix4d, HD_CYCLES_MOTION_STEPS> m_transformSamples;
};

PXR_NAMESPACE_CLOSE_SCOPE

#endif  // HD_CYCLES_POINTS_H
