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
#include <render/integrator.h>
#include <render/scene.h>

#include <pxr/usd/usdGeom/tokens.h>

#ifdef USE_USD_CYCLES_SCHEMA
#    include <usdCycles/tokens.h>
#endif

PXR_NAMESPACE_OPEN_SCOPE

namespace {
template<typename T>
bool
EvalCameraParam(T* value, const TfToken& paramName, HdSceneDelegate* sceneDelegate, const SdfPath& primPath,
                T defaultValue)
{
    VtValue vtval = sceneDelegate->GetCameraParamValue(primPath, paramName);
    if (vtval.IsEmpty()) {
        *value = defaultValue;
        return false;
    }
    if (!vtval.IsHolding<T>()) {
        *value = defaultValue;
        TF_CODING_ERROR("%s: type mismatch - %s", paramName.GetText(), vtval.GetTypeName().c_str());
        return false;
    }

    *value = vtval.UncheckedGet<T>();
    return true;
}

template<typename T>
bool
EvalCameraParam(T* value, const TfToken& paramName, HdSceneDelegate* sceneDelegate, const SdfPath& primPath)
{
    return EvalCameraParam(value, paramName, sceneDelegate, primPath, std::numeric_limits<T>::quiet_NaN());
}
}  // namespace

#ifdef USE_USD_CYCLES_SCHEMA

std::map<TfToken, ccl::MotionPosition> MOTION_POSITION_CONVERSION = {
    { usdCyclesTokens->start, ccl::MOTION_POSITION_START },
    { usdCyclesTokens->center, ccl::MOTION_POSITION_CENTER },
    { usdCyclesTokens->end, ccl::MOTION_POSITION_END },
};

std::map<TfToken, ccl::Camera::RollingShutterType> ROLLING_SHUTTER_TYPE_CONVERSION = {
    { usdCyclesTokens->none, ccl::Camera::ROLLING_SHUTTER_NONE },
    { usdCyclesTokens->top, ccl::Camera::ROLLING_SHUTTER_TOP },
};

std::map<TfToken, ccl::PanoramaType> PANORAMA_TYPE_CONVERSION = {
    { usdCyclesTokens->equirectangular, ccl::PANORAMA_EQUIRECTANGULAR },
    { usdCyclesTokens->fisheye_equidistant, ccl::PANORAMA_FISHEYE_EQUIDISTANT },
    { usdCyclesTokens->fisheye_equisolid, ccl::PANORAMA_FISHEYE_EQUISOLID },
    { usdCyclesTokens->mirrorball, ccl::PANORAMA_MIRRORBALL },
};

std::map<TfToken, ccl::Camera::StereoEye> STEREO_EYE_CONVERSION = {
    { usdCyclesTokens->none, ccl::Camera::STEREO_NONE },
    { usdCyclesTokens->left, ccl::Camera::STEREO_LEFT },
    { usdCyclesTokens->right, ccl::Camera::STEREO_RIGHT },
};

#endif

HdCyclesCamera::HdCyclesCamera(SdfPath const& id, HdCyclesRenderDelegate* a_renderDelegate)
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
    //  , m_projectionType
    //  , m_projMtx
    //  , m_fov
    //  , m_transform
    , m_shutterTime(1.0f)
    , m_rollingShutterTime(0.1f)
    , m_motionPosition(ccl::MOTION_POSITION_CENTER)
    , m_rollingShutterType(ccl::Camera::ROLLING_SHUTTER_NONE)
    , m_panoramaType(ccl::PANORAMA_EQUIRECTANGULAR)
    , m_stereoEye(ccl::Camera::STEREO_NONE)
    , m_offscreenDicingScale(0.0f)
    //  , m_shutterCurve
    , m_fisheyeFov(M_PI_F)
    , m_fisheyeLens(10.5f)
    , m_latMin(-M_PI_2_F)
    , m_latMax(M_PI_2_F)
    , m_longMin(-M_PI_F)
    , m_longMax(M_PI_F)
    , m_useSphericalStereo(false)
    , m_interocularDistance(0.065f)
    , m_convergenceDistance(30.0f * 0.065f)
    , m_usePoleMerge(false)
    , m_poleMergeAngleFrom(60.0f * M_PI_F / 180.0f)
    , m_poleMergeAngleTo(75.0f * M_PI_F / 180.0f)
    , m_useDof(false)
    , m_useMotionBlur(false)
    , m_fps(24.f)
    //  , m_transformSamples
    , m_cyclesCamera(nullptr)
    , m_renderDelegate(a_renderDelegate)
    , m_needsUpdate(false)
{
    m_cyclesCamera = m_renderDelegate->GetCyclesRenderParam()->GetCyclesScene()->camera;

    static const HdCyclesConfig& config = HdCyclesConfig::GetInstance();
    config.enable_dof.eval(m_useDof, true);
    config.enable_motion_blur.eval(m_useMotionBlur, true);

    bool use_motion_blur = m_renderDelegate->GetCyclesRenderParam()->GetCyclesScene()->integrator->get_motion_blur();
    if (use_motion_blur) {
        m_useMotionBlur = true;
    }
}

HdCyclesCamera::~HdCyclesCamera() {}

void
HdCyclesCamera::Sync(HdSceneDelegate* sceneDelegate, HdRenderParam* renderParam, HdDirtyBits* dirtyBits)
{
    HD_TRACE_FUNCTION();
    HF_MALLOC_TAG_FUNCTION();

    if (!TF_VERIFY(sceneDelegate != nullptr)) {
        return;
    }

    SdfPath const& id = GetId();

    HdCyclesRenderParam* param = static_cast<HdCyclesRenderParam*>(renderParam);

    if (*dirtyBits & HdCamera::DirtyClipPlanes) {
        bool has_clippingRange = EvalCameraParam(&m_clippingRange, HdCameraTokens->clippingRange, sceneDelegate, id,
                                                 GfRange1f(0.1f, 100000.0f));

        // TODO: has_clippingRange
        (void)has_clippingRange;
    }

    if (*dirtyBits & HdCamera::DirtyParams) {
        m_needsUpdate = true;

        // TODO:
        // Offset (requires viewplane work)

        EvalCameraParam(&m_horizontalApertureOffset, HdCameraTokens->horizontalApertureOffset, sceneDelegate, id);
        EvalCameraParam(&m_verticalApertureOffset, HdCameraTokens->verticalApertureOffset, sceneDelegate, id);

        // TODO:
        // Shutter

        EvalCameraParam(&m_shutterOpen, HdCameraTokens->shutterOpen, sceneDelegate, id);
        EvalCameraParam(&m_shutterClose, HdCameraTokens->shutterClose, sceneDelegate, id);

        // TODO: Shutter time is somewhat undefined, the usdCycles schema can directly set this
        //float shutter = (std::abs(m_shutterOpen) + std::abs(m_shutterClose))
        //                / 2.0f;
        //if (m_shutterOpen == 0.0f && m_shutterClose == 0.0f)
        //    shutter = 0.5f;
        m_shutterTime = 0.5f;

        // Projection

        bool has_projection = EvalCameraParam(&m_projectionType, UsdGeomTokens->projection, sceneDelegate, id);
        // TODO: has_projection
        (void)has_projection;

        // Aperture

        float horizontalAp, verticalAp;
        bool has_horizontalAp = EvalCameraParam(&horizontalAp, HdCameraTokens->horizontalAperture, sceneDelegate, id);
        if (has_horizontalAp)
            m_horizontalAperture = horizontalAp * 10.0f;

        bool has_verticalAp = EvalCameraParam(&verticalAp, HdCameraTokens->verticalAperture, sceneDelegate, id);
        if (has_verticalAp)
            m_verticalAperture = verticalAp * 10.0f;

        // Focal Length

        float focalLength;
        bool has_focalLength = EvalCameraParam(&focalLength, HdCameraTokens->focalLength, sceneDelegate, id);
        if (has_focalLength)
            m_focalLength = focalLength * 10.0f;

        if (has_focalLength && has_horizontalAp && has_verticalAp) {
            float y1 = m_verticalAperture;
            if (m_horizontalAperture < y1)
                y1 = m_horizontalAperture;
            float fov = 2.0f * atanf((m_verticalAperture / 2.0f) / m_focalLength);
            // TODO: This isn't always correct.
            // This is usually set in the renderpass from the proj matrix
            m_fov = fov;
        }

        bool has_fStop = EvalCameraParam(&m_fStop, HdCameraTokens->fStop, sceneDelegate, id);

        bool has_focusDistance = EvalCameraParam(&m_focusDistance, HdCameraTokens->focusDistance, sceneDelegate, id);
        // TODO: has_focusDistance
        (void)has_focusDistance;

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

#ifdef USE_USD_CYCLES_SCHEMA

        // Motion Position
        TfToken motionPosition = _HdCyclesGetCameraParam<TfToken>(sceneDelegate, id,
                                                                  usdCyclesTokens->cyclesCameraMotion_position,
                                                                  usdCyclesTokens->center);

        if (m_motionPosition != MOTION_POSITION_CONVERSION[motionPosition]) {
            m_motionPosition = MOTION_POSITION_CONVERSION[motionPosition];
        }

        // rolling shutter type
        TfToken rollingShutterType = _HdCyclesGetCameraParam<TfToken>(sceneDelegate, id,
                                                                      usdCyclesTokens->cyclesCameraRolling_shutter_type,
                                                                      usdCyclesTokens->none);

        if (m_rollingShutterType != ROLLING_SHUTTER_TYPE_CONVERSION[rollingShutterType]) {
            m_rollingShutterType = ROLLING_SHUTTER_TYPE_CONVERSION[rollingShutterType];
        }

        // panorama type
        TfToken panoramaType = _HdCyclesGetCameraParam<TfToken>(sceneDelegate, id,
                                                                usdCyclesTokens->cyclesCameraPanorama_type,
                                                                usdCyclesTokens->equirectangular);

        if (m_panoramaType != PANORAMA_TYPE_CONVERSION[panoramaType]) {
            m_panoramaType = PANORAMA_TYPE_CONVERSION[panoramaType];
        }

        // stereo eye
        TfToken stereoEye = _HdCyclesGetCameraParam<TfToken>(sceneDelegate, id, usdCyclesTokens->cyclesCameraStereo_eye,
                                                             usdCyclesTokens->none);

        if (m_stereoEye != STEREO_EYE_CONVERSION[stereoEye]) {
            m_stereoEye = STEREO_EYE_CONVERSION[stereoEye];
        }

        // Others

        VtFloatArray shutterCurve;

        shutterCurve = _HdCyclesGetCameraParam<VtFloatArray>(sceneDelegate, id,
                                                             usdCyclesTokens->cyclesCameraShutter_curve, shutterCurve);

        if (shutterCurve.size() > 0) {
            m_shutterCurve.resize(shutterCurve.size());

            for (size_t i = 0; i < shutterCurve.size(); i++) {
                m_shutterCurve[i] = shutterCurve[i];
            }
        }

        m_shutterTime = _HdCyclesGetCameraParam<float>(sceneDelegate, id, usdCyclesTokens->cyclesCameraShutter_time,
                                                       m_shutterTime);

        m_rollingShutterTime = _HdCyclesGetCameraParam<float>(sceneDelegate, id,
                                                              usdCyclesTokens->cyclesCameraRolling_shutter_duration,
                                                              m_rollingShutterTime);

        m_blades = _HdCyclesGetCameraParam<int>(sceneDelegate, id, usdCyclesTokens->cyclesCameraBlades, m_blades);

        m_bladesRotation = _HdCyclesGetCameraParam<float>(sceneDelegate, id,
                                                          usdCyclesTokens->cyclesCameraBlades_rotation,
                                                          m_bladesRotation);

        m_offscreenDicingScale = _HdCyclesGetCameraParam<float>(sceneDelegate, id,
                                                                usdCyclesTokens->cyclesCameraOffscreen_dicing_scale,
                                                                m_offscreenDicingScale);

        // Fisheye

        m_fisheyeFov = _HdCyclesGetCameraParam<float>(sceneDelegate, id, usdCyclesTokens->cyclesCameraFisheye_fov,
                                                      m_fisheyeFov);

        m_fisheyeLens = _HdCyclesGetCameraParam<float>(sceneDelegate, id, usdCyclesTokens->cyclesCameraFisheye_lens,
                                                       m_fisheyeLens);

        // Panorama

        m_latMin = _HdCyclesGetCameraParam<float>(sceneDelegate, id, usdCyclesTokens->cyclesCameraLatitude_min,
                                                  m_latMin);

        m_latMax = _HdCyclesGetCameraParam<float>(sceneDelegate, id, usdCyclesTokens->cyclesCameraLatitude_max,
                                                  m_latMax);

        m_longMin = _HdCyclesGetCameraParam<float>(sceneDelegate, id, usdCyclesTokens->cyclesCameraLongitude_min,
                                                   m_longMin);

        m_longMax = _HdCyclesGetCameraParam<float>(sceneDelegate, id, usdCyclesTokens->cyclesCameraLongitude_max,
                                                   m_longMax);

        // Stereo

        m_useSphericalStereo = _HdCyclesGetCameraParam<bool>(sceneDelegate, id,
                                                             usdCyclesTokens->cyclesCameraUse_spherical_stereo,
                                                             m_useSphericalStereo);

        m_interocularDistance = _HdCyclesGetCameraParam<float>(sceneDelegate, id,
                                                               usdCyclesTokens->cyclesCameraInterocular_distance,
                                                               m_interocularDistance);

        m_convergenceDistance = _HdCyclesGetCameraParam<float>(sceneDelegate, id,
                                                               usdCyclesTokens->cyclesCameraConvergence_distance,
                                                               m_convergenceDistance);

        // Pole merge

        m_usePoleMerge = _HdCyclesGetCameraParam<bool>(sceneDelegate, id, usdCyclesTokens->cyclesCameraUse_pole_merge,
                                                       m_usePoleMerge);

        m_poleMergeAngleFrom = _HdCyclesGetCameraParam<float>(sceneDelegate, id,
                                                              usdCyclesTokens->cyclesCameraPole_merge_angle_from,
                                                              m_poleMergeAngleFrom);

        m_poleMergeAngleTo = _HdCyclesGetCameraParam<float>(sceneDelegate, id,
                                                            usdCyclesTokens->cyclesCameraPole_merge_angle_to,
                                                            m_poleMergeAngleTo);
#endif
    }

    if (*dirtyBits & HdCamera::DirtyProjMatrix) {
        EvalCameraParam(&m_projMtx, HdCameraTokens->projectionMatrix, sceneDelegate, id);

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
        param->Interrupt();
    }

    HdCamera::Sync(sceneDelegate, renderParam, dirtyBits);

    *dirtyBits = HdChangeTracker::Clean;
}

bool
HdCyclesCamera::ApplyCameraSettings(ccl::Camera* a_camera)
{
    a_camera->set_matrix(mat4d_to_transform(m_transform));
    a_camera->set_fov(m_fov);

    a_camera->set_aperturesize(m_apertureSize);
    a_camera->set_blades(m_blades);
    a_camera->set_bladesrotation(m_bladesRotation);
    a_camera->set_focaldistance(m_focusDistance);
    a_camera->set_aperture_ratio(m_apertureRatio);

    a_camera->set_shutter_curve(m_shutterCurve);

    a_camera->set_offscreen_dicing_scale(m_offscreenDicingScale);

    a_camera->set_fisheye_fov(m_fisheyeFov);
    a_camera->set_fisheye_lens(m_fisheyeLens);

    a_camera->set_latitude_min(m_latMin);
    a_camera->set_latitude_max(m_latMin);
    a_camera->set_longitude_min(m_latMax);
    a_camera->set_longitude_max(m_longMax);

    a_camera->set_use_spherical_stereo(m_useSphericalStereo);

    a_camera->set_interocular_distance(m_interocularDistance);
    a_camera->set_convergence_distance(m_convergenceDistance);
    a_camera->set_use_pole_merge(m_usePoleMerge);

    a_camera->set_pole_merge_angle_from(m_poleMergeAngleFrom);
    a_camera->set_pole_merge_angle_to(m_poleMergeAngleTo);

    a_camera->set_nearclip(m_clippingRange.GetMin());
    a_camera->set_farclip(m_clippingRange.GetMax());

    a_camera->set_fps(m_fps);
    a_camera->set_shuttertime(m_shutterTime);
    a_camera->set_motion_position(ccl::MotionPosition::MOTION_POSITION_CENTER);

    a_camera->set_rolling_shutter_duration(m_rollingShutterTime);

    a_camera->set_rolling_shutter_type(static_cast<ccl::Camera::RollingShutterType>(m_rollingShutterType));
    a_camera->set_panorama_type(static_cast<ccl::PanoramaType>(m_panoramaType));
    a_camera->set_motion_position(static_cast<ccl::MotionPosition>(m_motionPosition));
    a_camera->set_stereo_eye(static_cast<ccl::Camera::StereoEye>(m_stereoEye));

    if (m_projectionType == UsdGeomTokens->orthographic) {
        a_camera->set_camera_type(ccl::CameraType::CAMERA_ORTHOGRAPHIC);
    } else {
        a_camera->set_camera_type(ccl::CameraType::CAMERA_PERSPECTIVE);
    }

    bool shouldUpdate = m_needsUpdate;

    if (shouldUpdate)
        m_needsUpdate = false;

    // TODO:
    // We likely need to ensure motion_position is respected when
    // populating the camera->motion array.
    if (m_useMotionBlur) {
        ccl::array<ccl::Transform> motion;
        motion.resize(m_transformSamples.count, ccl::transform_identity());

        for (size_t i = 0; i < m_transformSamples.count; i++) {
            if (m_transformSamples.times.data()[i] == 0.0f) {
                a_camera->set_matrix(mat4d_to_transform(ConvertCameraTransform(m_transformSamples.values.data()[i])));
            }

            motion[i] = mat4d_to_transform(ConvertCameraTransform(m_transformSamples.values.data()[i]));
        }
        a_camera->set_motion(motion);
    }

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
    if (!std::isnan(m_horizontalApertureOffset) && !std::isnan(m_verticalApertureOffset)) {
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
    if (!std::isnan(m_clippingRange.GetMin()) && !std::isnan(m_clippingRange.GetMax())) {
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
        double left   = -(1 + a_projectionMatrix[3][0]) / a_projectionMatrix[0][0];
        double right  = (1 - a_projectionMatrix[3][0]) / a_projectionMatrix[0][0];
        double bottom = -(1 - a_projectionMatrix[3][1]) / a_projectionMatrix[1][1];
        double top    = (1 + a_projectionMatrix[3][1]) / a_projectionMatrix[1][1];
        double w      = (right - left) / 2;
        double h      = (top - bottom) / 2;
        GfMatrix4d scaleMatrix;
        scaleMatrix.SetScale(GfVec3d(w, h, 1));
        viewToWorldCorrectionMatrix = scaleMatrix;
    }

    GfMatrix4d flipZ(1.0);
    flipZ[2][2]                 = -1.0;
    viewToWorldCorrectionMatrix = flipZ * viewToWorldCorrectionMatrix;

    GfMatrix4d matrix = viewToWorldCorrectionMatrix * m_transformSamples.values.data()[0];

    m_transform = (matrix);
}

PXR_NAMESPACE_CLOSE_SCOPE
