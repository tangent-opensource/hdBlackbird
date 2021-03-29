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

#ifndef HD_CYCLES_VOLUME_H
#define HD_CYCLES_VOLUME_H

#include "api.h"

#include "utils.h"
#include "hdcycles.h"
#include "renderDelegate.h"

#include <util/util_transform.h>

#include <pxr/base/gf/matrix4f.h>
#include <pxr/imaging/hd/volume.h>
#include <pxr/pxr.h>

#ifdef WITH_OPENVDB
#include <openvdb/openvdb.h>
#endif

namespace ccl {
class Mesh;
class Scene;
class Object;
class Geometry;
}  // namespace ccl

PXR_NAMESPACE_OPEN_SCOPE

class HdSceneDelegate;
class HdCyclesRenderDelegate;

/**
 * @brief USD Volume mapped to Cycles Volume
 * 
 */
class HdCyclesVolume final : public HdVolume {
public:
    /**
     * @brief Construct a new HdCycles Volume object
     * 
     * @param id Path to the Volume Primitive
     * @param instancerId If specified the HdInstancer at this id uses this volume
     * as a prototype
     */
    HdCyclesVolume(SdfPath const& id, SdfPath const& instancerId,
                   HdCyclesRenderDelegate* a_renderDelegate);
    /**
     * @brief Destroy the HdCycles Volume object
     * 
     */
    virtual ~HdCyclesVolume();

    /**
     * @brief Pull invalidated material data and prepare/update the core Cycles 
     * representation.
     * 
     * This must be thread safe.
     * 
     * @param sceneDelegate The data source for the volume
     * @param renderParam State
     * @param dirtyBits Which bits of scene data has changed
     */
    void Sync(HdSceneDelegate* sceneDelegate, HdRenderParam* renderParam,
              HdDirtyBits* dirtyBits, TfToken const& reprSelector) override;

    /**
     * @brief Inform the scene graph which state needs to be downloaded in
     * the first Sync() call
     * 
     * @return The initial dirty state this volume wants to query
     */
    HdDirtyBits GetInitialDirtyBitsMask() const override;

    /**
     * @return Return true if this volume is valid.
     */
    bool IsValid() const;

    /**
     * @brief Not Implemented
     */
    void Finalize(HdRenderParam* renderParam) override;

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

protected:
    GfMatrix4f m_transform;

    bool m_useMotionBlur;

private:
    /**
     * @brief Create the cycles object representation
     * 
     * @return New allocated pointer to ccl::Object
     */
    ccl::Object* _CreateObject();

    /**
     * @brief Create the cycles volume mesh representation
     * 
     * @return New allocated pointer to ccl::Mesh
     */
    ccl::Mesh* _CreateVolume();

    /**
     * @brief Populate the Cycles mesh representation from delegate's data
     */
    void _PopulateVolume(const SdfPath& id, HdSceneDelegate* delegate,
                         ccl::Scene* scene);

    /**
     * @brief Update the OpenVDB loader grid for mesh builder  
     */
    void _UpdateGrids();

    ccl::Object* m_cyclesObject;

    ccl::Mesh* m_cyclesVolume;

    HdCyclesRenderDelegate* m_renderDelegate;

    HdTimeSampleArray<GfMatrix4d, HD_CYCLES_MOTION_STEPS> m_transformSamples;

    ccl::vector<ccl::Shader *> m_usedShaders;

    //openvdb::VolumeGridVector* grids;
};

PXR_NAMESPACE_CLOSE_SCOPE

#endif  // HD_CYCLES_VOLUME_H
