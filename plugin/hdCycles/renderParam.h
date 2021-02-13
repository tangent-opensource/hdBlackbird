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

#ifndef HD_CYCLES_RENDER_PARAM_H
#define HD_CYCLES_RENDER_PARAM_H

#include "api.h"

#include <device/device.h>
#include <render/buffers.h>
#include <render/camera.h>
#include <render/session.h>
#include <render/tile.h>

#include <pxr/imaging/hd/renderDelegate.h>
#include <pxr/pxr.h>

namespace ccl {
class Session;
class Scene;
class Mesh;
class RenderTile;
class Shader;
}  // namespace ccl

PXR_NAMESPACE_OPEN_SCOPE

/**
 * @brief The proposed main interface to the cycles session and scene
 * Very much under construction.
 * 
 */
class HdCyclesRenderParam : public HdRenderParam {
public:
    /**
    * @brief Construct a new HdCycles Render Param object
    * 
    */
    HdCyclesRenderParam();

    /**
     * @brief Destroy the HdCycles Render Param object
     * 
     */
    ~HdCyclesRenderParam() = default;

    /**
     * @brief Start cycles render session
     * 
     */
    HDCYCLES_API
    void StartRender();

    /**
     * @brief Stop the current render and close cycles instance
     * 
     * @return HDCYCLES_API StopRender 
     */
    HDCYCLES_API
    void StopRender();

    /**
     * @brief Completely restart a cycles render
     * Currently unused, likely broken
     *  
     */
    HDCYCLES_API
    void RestartRender();

    /**
     * @brief Restarts the current cycles render
     * 
     */
    HDCYCLES_API
    void Interrupt(bool a_forceUpdate = false);

    /**
     * @brief Initialize cycles renderer
     * Core first time initialization of HdCycles
     */
    bool Initialize(HdRenderSettingsMap const& settingsMap);

    /**
     * @brief Pause cycles render session
     * 
     */
    void PauseRender();

    /**
     * @brief Resume cycles render session
     * 
     */
    void ResumeRender();

    /**
     * @return Progress completed of render
     */
    HDCYCLES_API
    float GetProgress();

    HDCYCLES_API
    bool IsConverged();

    /**
     * @brief Key access point to set a HdCycles render setting via key and value
     * Handles SessionParams, SceneParams, Integrator, Film, and Background intelligently.
     * 
     * This has some inherent performance overheads (runs multiple times, unecessary)
     * however for now, this works the most clearly due to Cycles restrictions
     * 
     * @param key 
     * @param value 
     * @return true 
     * @return false 
     */
    bool SetRenderSetting(const TfToken& key, const VtValue& valuekey);

protected:

    /**
     * @brief Start a cycles render
     * 
     */
    void _CyclesStart();

    /**
     * @brief Main exit logic of cycles render
     * 
     */
    void _CyclesExit();

    /**
     * @brief Callback when cycles session updated
     * 
     */
    void _SessionUpdateCallback();

    void _WriteRenderTile(ccl::RenderTile& rtile);
    void _UpdateRenderTile(ccl::RenderTile& rtile, bool highlight);

public:
    // Up Axis. Z and Y currently supported.
    enum UpAxis : uint8_t {
        Z = 0,
        Y = 1,
    };

    /**
     * @brief Cycles general reset
     * 
     * @param a_forceUpdate Should force update of cycles managers
     */
    void CyclesReset(bool a_forceUpdate = false);

    /**
     * @brief Set "viewport" based on width and height
     * TODO: Add support for render regions
     * 
     * @param w Width of new render
     * @param h Height of new render
     */
    void SetViewport(int w, int h);

    /**
     * @brief Slightly hacky workaround to directly reset the session
     * 
     */
    void DirectReset();

    /**
     * @brief Helper to set the background shader 
     * 
     * @param a_shader Shader to use
     * @param a_emissive Should the default bg be emissive
     */
    void SetBackgroundShader(ccl::Shader* a_shader = nullptr,
                             bool a_emissive       = true);

    /* ======= Cycles Settings ======= */

    /**
     * @brief Set Cycles render device type
     * 
     * @param a_deviceType Device type as type
     * @param params Specific params
     * @return Returns true if could set the device type
     */
    bool SetDeviceType(ccl::DeviceType a_deviceType,
                       ccl::SessionParams& params);

    /**
     * @brief Set Cycles render device type
     * 
     * @param a_deviceType Device type as string
     * @param params Specific params
     * @return Returns true if could set the device type
     */
    bool SetDeviceType(const std::string& a_deviceType,
                       ccl::SessionParams& params);

    /**
     * @brief Set Cycles render device type
     * 
     * @param a_deviceType Device Type as string
     * @return Returns true if could set the device type
     */
    bool SetDeviceType(const std::string& a_deviceType);

    /* ====== HdCycles Settings ====== */

    /**
     * @brief Add light to scene
     * 
     * @param a_light Light to add
     */
    void AddLight(ccl::Light* a_light);

    /**
     * @brief Add geometry to scene
     * 
     * @param a_geometry Geometry to add
     */
    void AddGeometry(ccl::Geometry* a_geometry);

    /**
     * @brief Add mesh to scene
     * 
     * @param a_geometry Mesh to add
     */
    void AddMesh(ccl::Mesh* a_mesh);

    /**
     * @brief Add geometry to scene
     * 
     * @param a_geometry Geometry to add
     */
    void AddCurve(ccl::Geometry* a_curve);

    /**
     * @brief Add shader to scene
     * 
     * @param a_shader Shader to add
     */
    void AddShader(ccl::Shader* a_shader);

    /**
     * @brief Add object to scene
     * 
     * @param a_object Object to add
     */
    void AddObject(ccl::Object* a_object);

    /**
     * @brief Remove hair geometry from cycles scene
     * 
     * @param a_hair Hair to remove
     */
    void RemoveCurve(ccl::Hair* a_hair);

    /**
     * @brief Remove light from cycles scene
     * 
     * @param a_light Light to remove
     */
    void RemoveLight(ccl::Light* a_light);

    /**
     * @brief Remove shader from cycles scene
     * 
     * @param a_shader Shader to remove
     */
    void RemoveShader(ccl::Shader* a_shader);

    /**
     * @brief Remove mesh geometry from cycles scene
     * 
     * @param a_mesh Mesh to remove
     */
    void RemoveMesh(ccl::Mesh* a_mesh);

    /**
     * @brief Remove object from cycles scene
     * 
     * @param a_object Object to remove
     */
    void RemoveObject(ccl::Object* a_object);

private:
    bool _CreateSession();

    /**
     * @brief Creates the base Cycles scene
     * 
     * This paradigm does cause unecessary loops through settingsMap for each feature. 
     * This should be addressed in the future. For the moment, the flexibility of setting
     * order of operations is more important.
     * 
     * @return true 
     * @return false 
     */
    bool _CreateScene();

    void _UpdateDelegateFromConfig(bool a_forceInit = false);
    void
    _UpdateDelegateFromRenderSettings(HdRenderSettingsMap const& settingsMap);
    bool _HandleDelegateRenderSetting(const TfToken& key, const VtValue& value);

    void _UpdateSessionFromConfig(bool a_forceInit = false);
    void
    _UpdateSessionFromRenderSettings(HdRenderSettingsMap const& settingsMap);
    bool _HandleSessionRenderSetting(const TfToken& key, const VtValue& value);

    void _UpdateSceneFromConfig(bool a_forceInit = false);
    void _UpdateSceneFromRenderSettings(HdRenderSettingsMap const& settingsMap);
    bool _HandleSceneRenderSetting(const TfToken& key, const VtValue& value);

    void _UpdateFilmFromConfig(bool a_forceInit = false);
    void _UpdateFilmFromRenderSettings(HdRenderSettingsMap const& settingsMap);
    bool _HandleFilmRenderSetting(const TfToken& key, const VtValue& value);

    void _UpdateIntegratorFromConfig(bool a_forceInit = false);
    void
    _UpdateIntegratorFromRenderSettings(HdRenderSettingsMap const& settingsMap);
    bool _HandleIntegratorRenderSetting(const TfToken& key,
                                        const VtValue& value);

    void _UpdateBackgroundFromConfig(bool a_forceInit = false);
    void
    _UpdateBackgroundFromRenderSettings(HdRenderSettingsMap const& settingsMap);
    bool _HandleBackgroundRenderSetting(const TfToken& key,
                                        const VtValue& value);

    void _HandlePasses();

    /**
     * @brief Initialize member values based on config
     * TODO: Refactor this
     * 
     */
    void _InitializeDefaults();

    bool _SetDevice(const ccl::DeviceType& a_deviceType,
                    ccl::SessionParams& params);

    ccl::SessionParams m_sessionParams;
    ccl::SceneParams m_sceneParams;
    ccl::BufferParams m_bufferParams;

    int m_renderPercent;
    float m_renderProgress;

    double m_totalTime;
    double m_renderTime;

    ccl::DeviceType m_deviceType;
    std::string m_deviceName;

    bool m_useTiledRendering;

    bool m_aovBindingsNeedValidation;

    int m_width;
    int m_height;

    bool m_objectsUpdated;
    bool m_geometryUpdated;
    bool m_curveUpdated;
    bool m_meshUpdated;
    bool m_lightsUpdated;
    bool m_shadersUpdated;

    bool m_shouldUpdate;

    int m_numDomeLights;

    bool m_useSquareSamples;

    UpAxis m_upAxis;

public:
    const bool& IsTiledRender() const { return m_useTiledRendering; }

    void CommitResources();
    /**
     * @brief Get the active Cycles Session 
     * 
     * @return ccl::Session* Cycles Session
     */
    ccl::Session* GetCyclesSession() { return m_cyclesSession; }

    /**
     * @brief Get theactive Cycles Scene
     * 
     * @return ccl::Scene* Cycles Scene
     */
    ccl::Scene* GetCyclesScene() { return m_cyclesScene; }

    /**
     * @brief Replacement default surface shader for vertex color meshes
     * TODO: Refactor this somewhere else
     * 
     */
    ccl::Shader* default_vertex_display_color_surface;
    ccl::Shader* default_object_display_color_surface;

    VtDictionary GetRenderStats() const;

    /**
     * @brief Get the up-axis that is set.
     * 
     */
    UpAxis GetUpAxis() const { return m_upAxis; }

private:
    ccl::Session* m_cyclesSession;
    ccl::Scene* m_cyclesScene;


    HdRenderPassAovBindingVector m_aovs;

public:
    void SetAovBindings(HdRenderPassAovBindingVector const& a_aovs)
    {
        m_aovs = a_aovs;
    }

    HdRenderPassAovBindingVector const& GetAovBindings() const
    {
        return m_aovs;
    }
};

PXR_NAMESPACE_CLOSE_SCOPE

#endif  // HD_CYCLES_RENDER_PARAM_H
