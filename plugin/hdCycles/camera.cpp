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

#include "camera.h"

#include "config.h"
#include "renderDelegate.h"
#include "renderParam.h"
#include "utils.h"

#include <render/camera.h>
#include <render/scene.h>

#include <pxr/usd/usdGeom/tokens.h>

PXR_NAMESPACE_OPEN_SCOPE

namespace {
template<typename T>
bool
EvalCameraParam(T* value, const TfToken& paramName,
                HdSceneDelegate* sceneDelegate, const SdfPath& primPath,
                T defaultValue)
{
    VtValue vtval = sceneDelegate->GetCameraParamValue(primPath, paramName);
    if (vtval.IsEmpty()) {
        *value = defaultValue;
        return false;
    }
    if (!vtval.IsHolding<T>()) {
        *value = defaultValue;
        TF_CODING_ERROR("%s: type mismatch - %s", paramName.GetText(),
                        vtval.GetTypeName().c_str());
        return false;
    }

    *value = vtval.UncheckedGet<T>();
    return true;
}

template<typename T>
bool
EvalCameraParam(T* value, const TfToken& paramName,
                HdSceneDelegate* sceneDelegate, const SdfPath& primPath)
{
    return EvalCameraParam(value, paramName, sceneDelegate, primPath,
                           std::numeric_limits<T>::quiet_NaN());
}
}  // namespace

HdCyclesCamera::HdCyclesCamera(SdfPath const& id,
                               HdCyclesRenderDelegate* a_renderDelegate)
    : HdCamera(id)
    , m_horizontalAperture(36.0f)
    , m_verticalAperture(24.0f)
    , m_horizontalApertureOffset(0.0f)
    , m_verticalApertureOffset(0.0f)
    , m_focalLength(50.0f)
    , m_fStop(2.8f)
    , m_focusDistance(10.0f)
    , m_shutterOpen(0.0f)
    , m_shutterClose(0.0f)
    , m_clippingRange(0.1f, 100000.0f)
    , m_renderDelegate(a_renderDelegate)
    , m_needsUpdate(false)
{
    m_cyclesCamera
        = m_renderDelegate->GetCyclesRenderParam()->GetCyclesScene()->camera;

    static const HdCyclesConfig& config = HdCyclesConfig::GetInstance();
    m_useDof                            = config.enable_dof;
    m_useMotionBlur                     = config.enable_motion_blur;
}

HdCyclesCamera::~HdCyclesCamera() {}

void
HdCyclesCamera::Sync(HdSceneDelegate* sceneDelegate, HdRenderParam* renderParam,
                     HdDirtyBits* dirtyBits)
{
    HD_TRACE_FUNCTION();
    HF_MALLOC_TAG_FUNCTION();

    if (!TF_VERIFY(sceneDelegate != nullptr)) {
        return;
    }

    SdfPath const& id = GetId();

    HdCyclesRenderParam* param = (HdCyclesRenderParam*)renderParam;

    ccl::Scene* scene = param->GetCyclesScene();

    if (*dirtyBits & HdCamera::DirtyClipPlanes) {
        bool has_clippingRange
            = EvalCameraParam(&m_clippingRange, HdCameraTokens->clippingRange,
                              sceneDelegate, id, GfRange1f(0.1f, 100000.0f));
    }

    if (*dirtyBits & HdCamera::DirtyParams) {
        m_needsUpdate = true;

        // TODO:
        // Offset (requires viewplane work)

        EvalCameraParam(&m_horizontalApertureOffset,
                        HdCameraTokens->horizontalApertureOffset, sceneDelegate,
                        id);
        EvalCameraParam(&m_verticalApertureOffset,
                        HdCameraTokens->verticalApertureOffset, sceneDelegate,
                        id);

        // TODO:
        // Shutter

        EvalCameraParam(&m_shutterOpen, HdCameraTokens->shutterOpen,
                        sceneDelegate, id);
        EvalCameraParam(&m_shutterClose, HdCameraTokens->shutterClose,
                        sceneDelegate, id);

        // TODO: Shutter time is somewhat undefined, the usdCycles schema can directly set this
        //float shutter = (std::abs(m_shutterOpen) + std::abs(m_shutterClose))
        //                / 2.0f;
        //if (m_shutterOpen == 0.0f && m_shutterClose == 0.0f)
        //    shutter = 0.5f;
        m_shutterTime = 0.5f;

        // Projection

        bool has_projection = EvalCameraParam(&m_projectionType,
                                              UsdGeomTokens->projection,
                                              sceneDelegate, id);

        // Aperture

        float horizontalAp, verticalAp;
        bool has_horizontalAp
            = EvalCameraParam(&horizontalAp, HdCameraTokens->horizontalAperture,
                              sceneDelegate, id);
        if (has_horizontalAp)
            m_horizontalAperture = horizontalAp * 10.0f;

        bool has_verticalAp = EvalCameraParam(&verticalAp,
                                              HdCameraTokens->verticalAperture,
                                              sceneDelegate, id);
        if (has_verticalAp)
            m_verticalAperture = verticalAp * 10.0f;

        // Focal Length

        float focalLength;
        bool has_focalLength = EvalCameraParam(&focalLength,
                                               HdCameraTokens->focalLength,
                                               sceneDelegate, id);
        if (has_focalLength)
            m_focalLength = focalLength * 10.0f;

        if (has_focalLength && has_horizontalAp && has_verticalAp) {
            float y1 = m_verticalAperture;
            if (m_horizontalAperture < y1)
                y1 = m_horizontalAperture;
            float fov = 2.0f
                        * atanf((m_verticalAperture / 2.0f) / m_focalLength);
            // TODO: This isn't always correct.
            // This is usually set in the renderpass from the proj matrix
            m_fov = fov;
        }

        bool has_fStop = EvalCameraParam(&m_fStop, HdCameraTokens->fStop,
                                         sceneDelegate, id);

        bool has_focusDistance = EvalCameraParam(&m_focusDistance,
                                                 HdCameraTokens->focusDistance,
                                                 sceneDelegate, id);

        if (std::isnan(m_focalLength)) {
            has_focalLength = false;
        }

        if (std::isnan(m_fStop) || m_fStop < 0.000001f) {
            has_fStop = false;
        }

        // Depth of field

        if (m_useDof && has_fStop) {
            if (has_focalLength) {
                if (m_projectionType == UsdGeomTokens->orthographic)
                    m_apertureSize = 1.0f / (2.0f * m_fStop);
                else
                    m_apertureSize = (m_focalLength * 1e-3f) / (2.0f * m_fStop);
            }
            // TODO: We will need custom usdCycles schema for these
            m_apertureRatio  = 1.0f;
            m_blades         = 0;
            m_bladesRotation = 0.0f;
        } else {
            m_apertureSize   = 0.0f;
            m_blades         = 0;
            m_bladesRotation = 0.0f;
            m_focusDistance  = 0.0f;
            m_apertureRatio  = 1.0f;
        }
    }

    if (*dirtyBits & HdCamera::DirtyProjMatrix) {
        EvalCameraParam(&m_projMtx, HdCameraTokens->projectionMatrix,
                        sceneDelegate, id);

        sceneDelegate->SampleTransform(id, &m_transformSamples);
        SetTransform(m_projMtx);
    }

    if (*dirtyBits & HdCamera::DirtyViewMatrix) {
        // Convert right-handed Y-up camera space (USD, Hydra) to
        // left-handed Y-up (Cycles) coordinates. This just amounts to
        // flipping the Z axis.

        sceneDelegate->SampleTransform(id, &m_transformSamples);
        SetTransform(m_projMtx);
    }

    if (m_needsUpdate) {
        m_cyclesCamera->tag_update();
        m_cyclesCamera->need_update = true;
        param->Interrupt();
    }

    HdCamera::Sync(sceneDelegate, renderParam, dirtyBits);

    *dirtyBits = HdChangeTracker::Clean;
}

bool
HdCyclesCamera::ApplyCameraSettings(ccl::Camera* a_camera)
{
    a_camera->matrix = mat4d_to_transform(m_transform);
    a_camera->fov    = m_fov;

    a_camera->aperturesize   = m_apertureSize;
    a_camera->blades         = m_blades;
    a_camera->bladesrotation = m_bladesRotation;
    a_camera->focaldistance  = m_focusDistance;
    a_camera->aperture_ratio = m_apertureRatio;

    a_camera->nearclip = m_clippingRange.GetMin();
    a_camera->farclip  = m_clippingRange.GetMax();

    a_camera->shuttertime = m_shutterTime;
    a_camera->motion_position
        = ccl::Camera::MotionPosition::MOTION_POSITION_CENTER;

    if (m_projectionType == UsdGeomTokens->orthographic) {
        a_camera->type = ccl::CameraType::CAMERA_ORTHOGRAPHIC;
    } else {
        a_camera->type = ccl::CameraType::CAMERA_PERSPECTIVE;
    }

    bool shouldUpdate = m_needsUpdate;

    if (shouldUpdate)
        m_needsUpdate = false;

    /*if (m_useMotionBlur) {
        a_camera->motion.clear();
        a_camera->motion.resize(m_transformSamples.count, a_camera->matrix);
        for (int i = 0; i < m_transformSamples.count; i++) {
            int idx = a_camera->motion_step(m_transformSamples.times.data()[i]);

            if (m_transformSamples.times.data()[i] == 0.0f) {
                a_camera->matrix = mat4d_to_transform(
                    m_transformSamples.values.data()[i]);
            } else {
                a_camera->motion[i] = mat4d_to_transform(
                    m_transformSamples.values.data()[i]);
            }
        }
    }*/

    return shouldUpdate;
}

HdDirtyBits
HdCyclesCamera::GetInitialDirtyBitsMask() const
{
    return HdCamera::AllDirty;
}

template<class T>
static const T*
_GetDictItem(const VtDictionary& dict, const TfToken& key)
{
    const VtValue* v = TfMapLookupPtr(dict, key.GetString());
    return v && v->IsHolding<T>() ? &v->UncheckedGet<T>() : nullptr;
}

bool
HdCyclesCamera::GetApertureSize(GfVec2f* v) const
{
    if (!std::isnan(m_horizontalAperture) && !std::isnan(m_verticalAperture)) {
        *v = { m_horizontalAperture, m_verticalAperture };
        return true;
    }
    return false;
}

bool
HdCyclesCamera::GetApertureOffset(GfVec2f* v) const
{
    if (!std::isnan(m_horizontalApertureOffset)
        && !std::isnan(m_verticalApertureOffset)) {
        *v = { m_horizontalApertureOffset, m_verticalApertureOffset };
        return true;
    }
    return false;
}

bool
HdCyclesCamera::GetFocalLength(float* v) const
{
    if (!std::isnan(m_focalLength)) {
        *v = m_focalLength;
        return true;
    }
    return false;
}

bool
HdCyclesCamera::GetFStop(float* v) const
{
    if (!std::isnan(m_fStop)) {
        *v = m_fStop;
        return true;
    }
    return false;
}

bool
HdCyclesCamera::GetFocusDistance(float* v) const
{
    if (!std::isnan(m_focusDistance)) {
        *v = m_focusDistance;
        return true;
    }
    return false;
}

bool
HdCyclesCamera::GetShutterOpen(double* v) const
{
    if (!std::isnan(m_shutterOpen)) {
        *v = m_shutterOpen;
        return true;
    }
    return false;
}

bool
HdCyclesCamera::GetShutterClose(double* v) const
{
    if (!std::isnan(m_shutterClose)) {
        *v = m_shutterClose;
        return true;
    }
    return false;
}

bool
HdCyclesCamera::GetClippingRange(GfRange1f* v) const
{
    if (!std::isnan(m_clippingRange.GetMin())
        && !std::isnan(m_clippingRange.GetMax())) {
        *v = m_clippingRange;
        return true;
    }
    return false;
}

bool
HdCyclesCamera::GetProjectionType(TfToken* v) const
{
    if (!m_projectionType.IsEmpty()) {
        *v = m_projectionType;
        return true;
    }
    return false;
}

void
HdCyclesCamera::SetFOV(const float& a_value)
{
    m_fov = a_value;
}

void
HdCyclesCamera::SetTransform(const GfMatrix4d a_projectionMatrix)
{
    GfMatrix4d viewToWorldCorrectionMatrix(1.0);

    if (m_projectionType == UsdGeomTokens->orthographic) {
        double left = -(1 + a_projectionMatrix[3][0])
                      / a_projectionMatrix[0][0];
        double right = (1 - a_projectionMatrix[3][0])
                       / a_projectionMatrix[0][0];
        double bottom = -(1 - a_projectionMatrix[3][1])
                        / a_projectionMatrix[1][1];
        double top = (1 + a_projectionMatrix[3][1]) / a_projectionMatrix[1][1];
        double w   = (right - left) / 2;
        double h   = (top - bottom) / 2;
        GfMatrix4d scaleMatrix;
        scaleMatrix.SetScale(GfVec3d(w, h, 1));
        viewToWorldCorrectionMatrix = scaleMatrix;
    }

    GfMatrix4d flipZ(1.0);
    flipZ[2][2]                 = -1.0;
    viewToWorldCorrectionMatrix = flipZ * viewToWorldCorrectionMatrix;

    GfMatrix4d matrix = viewToWorldCorrectionMatrix
                        * m_transformSamples.values.data()[0];

    m_transform = (matrix);
}

PXR_NAMESPACE_CLOSE_SCOPE
