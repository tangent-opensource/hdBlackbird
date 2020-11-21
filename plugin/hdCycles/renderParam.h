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
     * 
     */
    bool Initialize();

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

    /**
     * @brief Start a cycles render
     * TODO: Refactor this
     * 
     */
    void CyclesStart();

protected:
    /**
     * @brief Main initialization logic of cycles render
     * 
     * @return Returns true if successful 
     */
    bool _CyclesInitialize();

    /**
     * @brief Main exit logic of cycles render
     * 
     */
    void _CyclesExit();

    /**
     * @brief Debug callback to print the status of cycles
     * 
     */
    void _SessionPrintStatus();

public:
    /**
     * @brief Cycles general reset
     * 
     * @param a_forceUpdate Should force update of cycles managers
     */
    void CyclesReset(bool a_forceUpdate = false);

    /**
     * @brief Cycles reset based on width and height
     * TODO: Refactor these
     * 
     * @param w Width of new render
     * @param h Height of new render
     */
    void CyclesReset(int w, int h);

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
     * @brief Get should cycles be run in experimental mode
     * 
     * @return Returns true if cycles will run in experimental mode
     */
    const bool& GetUseExperimental();

    /**
     * @brief Set should cycles be run in experimental mode
     * 
     * @param a_value Should use experimental mode
     */
    void SetUseExperimental(const bool& a_value);

    /**
     * @brief Get the maximum samples to be used in a render
     * 
     * @return const int& Maximum number of samples
     */
    const int& GetMaxSamples();

    /**
     * @brief Set the maximum samples to be used in a render
     * 
     * @param a_value Maximum number of samples
     */
    void SetMaxSamples(const int& a_value);

    /**
     * @brief Get the number of threads to be used when rendering
     * 
     * @return const int& Number of threads
     */
    const int& GetNumThreads();

    /**
     * @brief Set the number of threads to be used when rendering
     * 0 is automatic
     * 
     * @param a_value Number of threads 
     */
    void SetNumThreads(const int& a_value);

    /**
     * @brief Get the individual Pixel Size of cycles render
     * 
     * @return const int& Pixel Size
     */
    const int& GetPixelSize();

    /**
     * @brief Set the individual Pixel Size of cycles render
     * 
     * @param a_value Pixel Size
     */
    void SetPixelSize(const int& a_value);

    /**
     * @brief Get the Tile Size of cycles tiled render
     * 
     * @return const pxr::GfVec2i& Tile width and height
     */
    const pxr::GfVec2i GetTileSize();

    /**
     * @brief Set the Tile Size of cycles tiled render
     * 
     * @param a_value Tile width and height
     */
    void SetTileSize(const pxr::GfVec2i& a_value);

    /**
     * @brief Set the Tile Size of cycles tiled render
     * 
     * @param a_x Tile width
     * @param a_y Tile hieght
     */
    void SetTileSize(int a_x, int a_y);

    /**
     * @brief Get the Start Resolution of cycles render
     * 
     * @return const int& Start Resolution
     */
    const int& GetStartResolution();

    /**
     * @brief Set the Start Resolution of cycles render
     * 
     * @param a_value Start Resolution
     */
    void SetStartResolution(const int& a_value);

    /**
     * @brief Get the Exposure of final render
     * 
     * @return const float& Exposure
     */
    const float& GetExposure();

    /**
     * @brief Set the Exposure of final render
     * 
     * @param a_exposure Exposure
     */
    void SetExposure(float a_exposure);

    /**
     * @brief Get the current Device Type 
     * 
     * @return const ccl::DeviceType& Device Type
     */
    const ccl::DeviceType& GetDeviceType();

    /**
     * @brief Get the Device Type Name as string
     * 
     * @return const std::string& Device Type Name
     */
    const std::string& GetDeviceTypeName();

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

    /**
     * @brief Get the camera's motion position
     * TODO: Move this to HdCyclesCamera
     * 
     * @return const ccl::Camera::MotionPosition& Motion Position 
     */
    const ccl::Camera::MotionPosition& GetShutterMotionPosition();

    /**
     * @brief Set the camera's motion position
     * TODO: Move this to HdCyclesCamera
     * 
     * @param a_value Motion Position
     */
    void SetShutterMotionPosition(const int& a_value);

    /**
     * @brief Set the camera's motion position
     * TODO: Move this to HdCyclesCamera
     * 
     * @param a_value Motion Position
     */
    void SetShutterMotionPosition(const ccl::Camera::MotionPosition& a_value);

    /* ====== HdCycles Settings ====== */

    /**
     * @brief Set should use motion blur
     * 
     * @param a_value Should use motion blur
     */
    void SetUseMotionBlur(const bool& a_value) { m_useMotionBlur = a_value; }

    /**
     * @brief Get if should use motion blur
     * 
     * @return Return true if motion blur should be used
     */
    const bool& GetUseMotionBlur() { return m_useMotionBlur; }

    /**
     * @brief Set the Motion Steps
     * 
     * @param a_value Motion Steps
     */
    void SetMotionSteps(const int& a_value) { m_motionSteps = a_value; }

    /**
     * @brief Get Motion Steps
     * 
     * @return const int& Motion Steps
     */
    const int& GetMotionSteps() { return m_motionSteps; }

    /**
     * @brief Get the Width of render
     * 
     * @return Width in pixels
     */
    const float& GetWidth() { return m_width; }

    /**
     * @brief Get the Height of render
     * 
     * @return Height in pixels
     */
    const float& GetHeight() { return m_height; }

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
    /**
     * @brief Initialize member values based on config
     * TODO: Refactor this
     * 
     */
    void _InitializeDefaults();

    bool _SetDevice(const ccl::DeviceType& a_deviceType,
                    ccl::SessionParams& params);

    ccl::BufferParams m_bufferParams;

    // These values are not explicitly defined in cycles api
    // They are stored here to allow for easy retrieval
    bool m_useMotionBlur;
    int m_motionSteps;

    int m_renderProgress;

    ccl::DeviceType m_deviceType;
    std::string m_deviceName;

    int m_width;
    int m_height;

    bool m_objectsUpdated;
    bool m_geometryUpdated;
    bool m_curveUpdated;
    bool m_meshUpdated;
    bool m_lightsUpdated;
    bool m_shadersUpdated;

    bool m_shouldUpdate;

    bool m_hasDomeLight;

public:
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
    ccl::Shader* default_vcol_surface;

private:
    ccl::Session* m_cyclesSession;
    ccl::Scene* m_cyclesScene;
};

PXR_NAMESPACE_CLOSE_SCOPE

#endif  // HD_CYCLES_RENDER_PARAM_H
