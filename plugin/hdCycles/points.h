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
class Shader;
}  // namespace ccl

PXR_NAMESPACE_OPEN_SCOPE

class HdSceneDelegate;
class HdCyclesRenderDelegate;

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
    HdCyclesPoints(SdfPath const& id, SdfPath const& instancerId, HdCyclesRenderDelegate* a_renderDelegate);
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
    void Sync(HdSceneDelegate* sceneDelegate, HdRenderParam* renderParam, HdDirtyBits* dirtyBits,
              TfToken const& reprSelector) override;

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
     * @brief Reads various object flags before setting other geometric data
     */
    void _ReadObjectFlags(HdSceneDelegate* sceneDelegate, const SdfPath& id, HdDirtyBits* dirtyBits);

    /**
     * @brief Fills in the point positions
     */
    void _PopulatePoints(HdSceneDelegate* sceneDelegate, const SdfPath& id, bool styleHasChanged, bool& sizeHasChanged);

    /**
     * @brief Fill in the point widths
     */
    void _PopulateWidths(HdSceneDelegate* sceneDelegate, const SdfPath& id, const HdInterpolation& interpolation,
                         VtValue value);

    /**
     * @brief Fill in the point colors
     */
    void _PopulateColors(HdSceneDelegate* sceneDelegate, const SdfPath& id, const HdInterpolation& interpolation,
                         VtValue value);

    /**
     * @brief Fill in the point alpha
     */
    void _PopulateOpacities(HdSceneDelegate* sceneDelegate, const SdfPath& id, const HdInterpolation& interpolation,
                            VtValue value);

    /**
     * @brief Fill in the point normals
     */
    void _PopulateNormals(HdSceneDelegate* sceneDelegate, const SdfPath& id, const HdInterpolation& interpolation,
                          VtValue value);

    /**
     * @brief Fill in the point normals
     */
    void _PopulateVelocities(HdSceneDelegate* sceneDelegate, const SdfPath& id, const HdInterpolation& interpolation,
                             VtValue value);

    /**
     * @brief Fill in the point accelerations if velocities
     */
    void _PopulateAccelerations(HdSceneDelegate* sceneDelegate, const SdfPath& id, const HdInterpolation& interpolation,
                                VtValue value);


    /**
     * @brief Fills in the point positions
     */
    void _PopulateGenerated(ccl::Scene* scene, const SdfPath& id);


    /**
     * @brief Flag the object for update in the scene
     */
    void _UpdateObject(ccl::Scene* scene, HdCyclesRenderParam* param, HdDirtyBits* dirtyBits, bool rebuildBVH = false);

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
     * @brief Check that the combination of object attributes matches
     * the Cycles specification. If it doesn't, it notifies the user
     * and reverts the object to a state where it doesn't crash
     * the renderer internally.
    */
    void _CheckIntegrity(HdCyclesRenderParam* param);

    ccl::PointCloud* m_cyclesPointCloud;
    ccl::Object* m_cyclesObject;

    ccl::Shader* m_point_display_color_shader;

    int m_pointResolution;  // ?

    unsigned int m_visibilityFlags;

    HdCyclesObjectSourceSharedPtr m_objectSource;
    HdCyclesRenderDelegate* m_renderDelegate;

    bool m_useMotionBlur;
    int m_motionSteps;

    // -- Currently unused

    HdTimeSampleArray<GfMatrix4d, HD_CYCLES_MOTION_STEPS> m_transformSamples;
};

PXR_NAMESPACE_CLOSE_SCOPE

#endif  // HD_CYCLES_POINTS_H
