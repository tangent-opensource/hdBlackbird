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

#include "config.h"

#include "points.h"

#include <pxr/base/tf/envSetting.h>
#include <pxr/base/tf/getenv.h>
#include <pxr/base/tf/instantiateSingleton.h>

#include <render/pointcloud.h>

PXR_NAMESPACE_OPEN_SCOPE

template<>
HdCyclesEnvValue<bool>::HdCyclesEnvValue(const char* a_envName, bool a_default)
{
    envName     = std::string(a_envName);
    value       = TfGetenvBool(envName, a_default);
    hasOverride = TfGetenv(envName) != "";
}

template<>
HdCyclesEnvValue<int>::HdCyclesEnvValue(const char* a_envName, int a_default)
{
    envName     = std::string(a_envName);
    value       = TfGetenvInt(envName, a_default);
    hasOverride = TfGetenv(envName) != "";
}

template<>
HdCyclesEnvValue<double>::HdCyclesEnvValue(const char* a_envName,
                                           double a_default)
{
    envName     = std::string(a_envName);
    value       = TfGetenvDouble(envName, a_default);
    hasOverride = TfGetenv(envName) != "";
}

template<>
HdCyclesEnvValue<float>::HdCyclesEnvValue(const char* a_envName,
                                          float a_default)
{
    envName     = std::string(a_envName);
    value       = (float)TfGetenvDouble(envName, (double)a_default);
    hasOverride = TfGetenv(envName) != "";
}

template<>
HdCyclesEnvValue<std::string>::HdCyclesEnvValue(const char* a_envName,
                                                std::string a_default)
{
    envName     = std::string(a_envName);
    value       = TfGetenv(envName, a_default);
    hasOverride = TfGetenv(envName) != "";
}

TF_INSTANTIATE_SINGLETON(HdCyclesConfig);

/* ====== HdCycles Settings ====== */

// For distinct generic delegate settings we still use the pixar TF_DEFINE_ENV_SETTING

TF_DEFINE_ENV_SETTING(CYCLES_ENABLE_LOGGING, false, "Enable HdCycles Logging");

TF_DEFINE_ENV_SETTING(CYCLES_LOGGING_SEVERITY, 1,
                      "Enable HdCycles progress reporting");

TF_DEFINE_ENV_SETTING(
    CYCLES_DUMP_SHADER_GRAPH_DIR, "",
    "Valid, existing directory to dump shader graphs for render");

TF_DEFINE_ENV_SETTING(HD_CYCLES_ENABLE_LOGGING, false,
                      "Enable HdCycles Logging");

TF_DEFINE_ENV_SETTING(HD_CYCLES_ENABLE_PROGRESS, false,
                      "Enable HdCycles progress reporting");

TF_DEFINE_ENV_SETTING(HD_CYCLES_USE_TILED_RENDERING, false,
                      "Use Tiled Rendering (Experimental)");

TF_DEFINE_ENV_SETTING(HD_CYCLES_UP_AXIS, "Z",
                      "Set custom up axis (Z or Y currently supported)");

// HdCycles Constructor
HdCyclesConfig::HdCyclesConfig()
{
    // -- Cycles Settings
    use_tiled_rendering = TfGetEnvSetting(HD_CYCLES_USE_TILED_RENDERING);

    cycles_enable_logging   = TfGetEnvSetting(CYCLES_ENABLE_LOGGING);
    cycles_logging_severity = TfGetEnvSetting(CYCLES_LOGGING_SEVERITY);

    cycles_shader_graph_dump_dir = TfGetEnvSetting(
        CYCLES_DUMP_SHADER_GRAPH_DIR);

    // -- HdCycles Settings
    enable_logging  = TfGetEnvSetting(HD_CYCLES_ENABLE_LOGGING);
    enable_progress = TfGetEnvSetting(HD_CYCLES_ENABLE_PROGRESS);

    up_axis = TfGetEnvSetting(HD_CYCLES_UP_AXIS);

    enable_motion_blur = HdCyclesEnvValue<bool>("HD_CYCLES_ENABLE_MOTION_BLUR",
                                                false);
    motion_steps       = HdCyclesEnvValue<int>("HD_CYCLES_MOTION_STEPS", 3);
    enable_subdivision = HdCyclesEnvValue<bool>("HD_CYCLES_ENABLE_SUBDIVISION",
                                                false);
    subdivision_dicing_rate
        = HdCyclesEnvValue<float>("HD_CYCLES_SUBDIVISION_DICING_RATE", 1.0);
    max_subdivision = HdCyclesEnvValue<int>("HD_CYCLES_MAX_SUBDIVISION", 12);
    enable_dof      = HdCyclesEnvValue<bool>("HD_CYCLES_ENABLE_DOF", true);

    render_width   = HdCyclesEnvValue<int>("HD_CYCLES_RENDER_WIDTH", 1280);
    render_height  = HdCyclesEnvValue<int>("HD_CYCLES_RENDER_HEIGHT", 720);
    use_old_curves = HdCyclesEnvValue<bool>("HD_CYCLES_USE_OLD_CURVES", false);

    enable_transparent_background
        = HdCyclesEnvValue<bool>("HD_CYCLES_USE_TRANSPARENT_BACKGROUND", false);
    use_square_samples = HdCyclesEnvValue<bool>("HD_CYCLES_USE_SQUARE_SAMPLES",
                                                false);

    // -- Cycles Settings
    enable_experimental
        = HdCyclesEnvValue<bool>("HD_CYCLES_ENABLE_EXPERIMENTAL", false);
    bvh_type = HdCyclesEnvValue<std::string>("HD_CYCLES_BVH_TYPE", "DYNAMIC");
    device_name = HdCyclesEnvValue<std::string>("HD_CYCLES_DEVICE_NAME", "CPU");
    shading_system = HdCyclesEnvValue<std::string>("HD_CYCLES_SHADING_SYSTEM",
                                                   "SVM");
    display_buffer_linear
        = HdCyclesEnvValue<bool>("HD_CYCLES_DISPLAY_BUFFER_LINEAR", true);

    max_samples = HdCyclesEnvValue<int>("HD_CYCLES_MAX_SAMPLES", 512);

    num_threads      = HdCyclesEnvValue<int>("HD_CYCLES_NUM_THREADS", 0);
    pixel_size       = HdCyclesEnvValue<int>("HD_CYCLES_PIXEL_SIZE", 1);
    tile_size_x      = HdCyclesEnvValue<int>("HD_CYCLES_TILE_SIZE_X", 64);
    tile_size_y      = HdCyclesEnvValue<int>("HD_CYCLES_TILE_SIZE_Y", 64);
    start_resolution = HdCyclesEnvValue<int>("HD_CYCLES_START_RESOLUTION", 8);
    shutter_motion_position
        = HdCyclesEnvValue<int>("HD_CYCLES_SHUTTER_MOTION_POSITION", 1);

    default_point_style
        = HdCyclesEnvValue<int>("HD_CYCLES_DEFAULT_POINT_STYLE",
                                ccl::POINT_CLOUD_POINT_SPHERE);
    default_point_resolution
        = HdCyclesEnvValue<int>("HD_CYCLES_DEFAULT_POINT_RESOLUTION", 16);


    // -- Curve Settings

    curve_subdivisions = HdCyclesEnvValue<int>("HD_CYCLES_CURVE_SUBDIVISIONS",
                                               3);

    // -- Film
    exposure = HdCyclesEnvValue<float>("HD_CYCLES_EXPOSURE", 1.0);

    // -- Integrator Settings
    integrator_method
        = HdCyclesEnvValue<std::string>("HD_CYCLES_INTEGRATOR_METHOD", "PATH");

    diffuse_samples = HdCyclesEnvValue<int>("HD_CYCLES_DIFFUSE_SAMPLES", 1);
    glossy_samples  = HdCyclesEnvValue<int>("HD_CYCLES_GLOSSY_SAMPLES", 1);
    transmission_samples
        = HdCyclesEnvValue<int>("HD_CYCLES_TRANSMISSION_SAMPLES", 1);
    ao_samples           = HdCyclesEnvValue<int>("HD_CYCLES_AO_SAMPLES", 1);
    mesh_light_samples   = HdCyclesEnvValue<int>("HD_CYCLES_MESH_LIGHT_SAMPLES",
                                               1);
    subsurface_samples   = HdCyclesEnvValue<int>("HD_CYCLES_SUBSURFACE_SAMPLES",
                                               1);
    volume_samples       = HdCyclesEnvValue<int>("HD_CYCLES_VOLUME_SAMPLES", 1);
    adaptive_min_samples = HdCyclesEnvValue<int>("HD_CYCLES_VOLUME_SAMPLES", 1);
}

const HdCyclesConfig&
HdCyclesConfig::GetInstance()
{
    return TfSingleton<HdCyclesConfig>::GetInstance();
}

PXR_NAMESPACE_CLOSE_SCOPE