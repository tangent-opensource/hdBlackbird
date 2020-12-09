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

#ifndef HD_CYCLES_CAMERA_H
#define HD_CYCLES_CAMERA_H

#include "api.h"

#include "hdcycles.h"

#include <pxr/base/gf/range1f.h>
#include <pxr/base/gf/vec2f.h>
#include <pxr/base/vt/dictionary.h>
#include <pxr/imaging/hd/camera.h>
#include <pxr/imaging/hd/sceneDelegate.h>
#include <pxr/imaging/hd/timeSampleArray.h>
#include <pxr/pxr.h>

namespace ccl {
class Camera;
}

PXR_NAMESPACE_OPEN_SCOPE

class HdSceneDelegate;
class HdCyclesRenderDelegate;

/**
 * @brief Cycles Camera Sprim mapped to Cycles Camera
 * 
 */
class HdCyclesCamera final : public HdCamera {
public:
    /**
     * @brief Construct a new HdCycles Camera object
     * 
     * @param id Path to the Camera Primitive
     */
    HdCyclesCamera(SdfPath const& id, HdCyclesRenderDelegate* a_renderDelegate);
    virtual ~HdCyclesCamera();

    /**
     * @brief Pull invalidated camera data and prepare/update the core Cycles 
     * representation.
     * 
     * This must be thread safe.
     * 
     * @param sceneDelegate The data source for the mesh
     * @param renderParam State
     * @param dirtyBits Which bits of scene data has changed
     */
    virtual void Sync(HdSceneDelegate* sceneDelegate,
                      HdRenderParam* renderParam,
                      HdDirtyBits* dirtyBits) override;

    /**
     * @brief Inform the scene graph which state needs to be downloaded in
     * the first Sync() call
     * 
     * @return The initial dirty state this camera wants to query
     */
    virtual HdDirtyBits GetInitialDirtyBitsMask() const override;

    /**
     * @return Return time sampled xforms that were quereied during Sync
     */
    HDCYCLES_API
    HdTimeSampleArray<GfMatrix4d, HD_CYCLES_MOTION_STEPS> const&
    GetTimeSampleXforms() const
    {
        return m_transformSamples;
    }

    /**
     * @brief Get the HdCyclesCamera Aperture Size 
     * 
     * @param value Value of Aperture Size
     * @return Return true if found 
     */
    bool GetApertureSize(GfVec2f* value) const;

    /**
     * @brief Get the HdCyclesCamera Aperture Offset 
     * 
     * @param value Value of Aperture Offset
     * @return Return true if found 
     */
    bool GetApertureOffset(GfVec2f* value) const;

    /**
     * @brief Get the HdCyclesCamera Focal Lenth 
     * 
     * @param value Value of Focal Lenth
     * @return Return true if found 
     */
    bool GetFocalLength(float* value) const;

    /**
     * @brief Get the HdCyclesCamera FStop 
     * 
     * @param value Value of FStop
     * @return Return true if found 
     */
    bool GetFStop(float* value) const;

    /**
     * @brief Get the HdCyclesCamera Focus Distance 
     * 
     * @param value Value of Focus Distance
     * @return Return true if found 
     */
    bool GetFocusDistance(float* value) const;

    /**
     * @brief Get the HdCyclesCamera Shutter Open 
     * 
     * @param value Value of Shutter Open
     * @return Return true if found 
     */
    bool GetShutterOpen(double* value) const;

    /**
     * @brief Get the HdCyclesCamera Shutter Close 
     * 
     * @param value Value of Shutter Close
     * @return Return true if found 
     */
    bool GetShutterClose(double* value) const;

    /**
     * @brief Get the HdCyclesCamera Clipping Range 
     * 
     * @param value Value of Clipping Range
     * @return Return true if found 
     */
    bool GetClippingRange(GfRange1f* value) const;

    /**
     * @brief Get the HdCyclesCamera Projection Type 
     * 
     * @param value Value of Projection Type
     * @return Return true if found 
     */
    bool GetProjectionType(TfToken* value) const;

    /**
     * @brief Get the Cycles Camera object
     * 
     * @return ccl::Camera* Camera
     */
    ccl::Camera* GetCamera() { return m_cyclesCamera; }

    /**
     * @brief Set value of cycles field of view
     * 
     * @param a_value FOV
     */
    void SetFOV(const float& a_value);

    /**
     * @brief Set the transform based on projection matrix
     * 
     * @param a_projectionMatrix 
     */
    void SetTransform(const GfMatrix4d a_projectionMatrix);

    /**
     * @brief Apply this cameras stored/synced settings 
     * to the given cycles camera
     * 
     * @param a_camera 
     * @return Return true if sync has incurred an update
     */
    bool ApplyCameraSettings(ccl::Camera* a_camera);

    const bool& IsDirty() { return m_needsUpdate; }

private:
    float m_horizontalAperture;
    float m_verticalAperture;
    float m_horizontalApertureOffset;
    float m_verticalApertureOffset;
    float m_focalLength;
    float m_fStop;
    float m_focusDistance;
    double m_shutterOpen;
    double m_shutterClose;
    GfRange1f m_clippingRange;
    TfToken m_projectionType;

    GfMatrix4d m_projMtx;

    // Cycles camera specifics
    float m_fov;
    GfMatrix4d m_transform;
    float m_shutterTime;
    float m_apertureRatio;
    int m_blades;
    float m_bladesRotation;
    float m_apertureSize;

    bool m_useDof;

    bool m_useMotionBlur;

    HdTimeSampleArray<GfMatrix4d, HD_CYCLES_MOTION_STEPS> m_transformSamples;

    ccl::Camera* m_cyclesCamera;

    HdCyclesRenderDelegate* m_renderDelegate;

    bool m_needsUpdate;
};

PXR_NAMESPACE_CLOSE_SCOPE

#endif  // HD_CYCLES_CAMERA_H
