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
//  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
//  See the License for the specific language governing permissions and
//  limitations under the License.

#include "config.h"

#include <pxr/base/tf/envSetting.h>
#include <pxr/base/tf/instantiateSingleton.h>

PXR_NAMESPACE_OPEN_SCOPE

TF_INSTANTIATE_SINGLETON(HdCyclesConfig);

/* ====== HdCycles Settings ====== */

TF_DEFINE_ENV_SETTING(HD_CYCLES_ENABLE_LOGGING, false,
                      "Enable HdCycles Logging");

TF_DEFINE_ENV_SETTING(HD_CYCLES_ENABLE_MOTION_BLUR, false,
                      "Enable HdCycles motion blur support");

TF_DEFINE_ENV_SETTING(HD_CYCLES_MOTION_STEPS, 3,
                      "Number of frames to populate motion for");

TF_DEFINE_ENV_SETTING(HD_CYCLES_ENABLE_SUBDIVISION, false,
                      "Enable HdCycles subdiv support");

TF_DEFINE_ENV_SETTING(HD_CYCLES_SUBDIVISION_DICING_RATE, "0.1",
                      "Mesh subdivision dicing rate");

TF_DEFINE_ENV_SETTING(HD_CYCLES_MAX_SUBDIVISION, 12,
                      "Maximum levels of subdivision");

TF_DEFINE_ENV_SETTING(HD_CYCLES_ENABLE_DOF, true,
                      "Enable hdCycles depth of field support");

TF_DEFINE_ENV_SETTING(HD_CYCLES_RENDER_WIDTH, 1280,
                      "Width of a non interactive HdCycles render");

TF_DEFINE_ENV_SETTING(HD_CYCLES_RENDER_HEIGHT, 720,
                      "Width of a non interactive HdCycles render");

TF_DEFINE_ENV_SETTING(
    HD_CYCLES_USE_OLD_CURVES, false,
    "If enabled, curves will be created manually with regular mesh geometry");


TF_DEFINE_ENV_SETTING(
    HD_CYCLES_USE_TRANSPARENT_BACKGROUND, false,
    "If enabled, the background will be transparent in renders");

/* ======= Cycles Settings ======= */

TF_DEFINE_ENV_SETTING(HD_CYCLES_ENABLE_EXPERIMENTAL, false,
                      "Experimental cycles support.");

TF_DEFINE_ENV_SETTING(HD_CYCLES_BVH_TYPE, "DYNAMIC", "Cycles BVH Type ");

TF_DEFINE_ENV_SETTING(HD_CYCLES_DEVICE_NAME, "CPU",
                      "Device cycles will use to render");

TF_DEFINE_ENV_SETTING(HD_CYCLES_SHADING_SYSTEM, "SVM",
                      "Shading system cycles will use");

TF_DEFINE_ENV_SETTING(HD_CYCLES_DISPLAY_BUFFER_LINEAR, true,
                      "Format of display buffer. False: byte. True: half.");

TF_DEFINE_ENV_SETTING(HD_CYCLES_MAX_SAMPLES, 512,
                      "Number of samples to render per pixel");

TF_DEFINE_ENV_SETTING(HD_CYCLES_NUM_THREADS, 0, "Number of threads to use");

TF_DEFINE_ENV_SETTING(HD_CYCLES_PIXEL_SIZE, 1, "Size of pixel");

TF_DEFINE_ENV_SETTING(HD_CYCLES_TILE_SIZE_X, 64, "Size of tile x");

TF_DEFINE_ENV_SETTING(HD_CYCLES_TILE_SIZE_Y, 64, "Size of tile y");

TF_DEFINE_ENV_SETTING(HD_CYCLES_START_RESOLUTION, 8,
                      "Maximum start Resolution of render");

TF_DEFINE_ENV_SETTING(HD_CYCLES_EXPOSURE, "1.0", "Exposure of cycles film");

TF_DEFINE_ENV_SETTING(
    HD_CYCLES_SHUTTER_MOTION_POSITION, 1,
    "Position of shutter motion position. (0: Start, 1: Center, 2: End)");

TF_DEFINE_ENV_SETTING(HD_CYCLES_DEFAULT_POINT_STYLE, 0,
                      "Default point style. (0: Discs, 1: Spheres)");

TF_DEFINE_ENV_SETTING(HD_CYCLES_DEFAULT_POINT_RESOLUTION, 16,
                      "Default point resolution");

/* ===== Curve Settings ===== */

TF_DEFINE_ENV_SETTING(HD_CYCLES_CURVE_RESOLUTION, 3, "Resolution of curve");
TF_DEFINE_ENV_SETTING(HD_CYCLES_CURVE_SUBDIVISIONS, 3, "Curve subdvisions");

TF_DEFINE_ENV_SETTING(HD_CYCLES_CURVE_USE_BACKFACES, false,
                      "Should curve geometry have backfaces");
TF_DEFINE_ENV_SETTING(HD_CYCLES_CURVE_USE_ENCASING, true,
                      "Should curve be encased");
TF_DEFINE_ENV_SETTING(HD_CYCLES_CURVE_USE_TANGENT_NORMAL_GEO, false,
                      "Should curve be encased");

TF_DEFINE_ENV_SETTING(HD_CYCLES_CURVE_SHAPE, "CURVE_THICK",
                      "Shape of curves [CURVE_RIBBON, CURVE_THICK]");
TF_DEFINE_ENV_SETTING(
    HD_CYCLES_CURVE_PRIMITIVE, "CURVE_SEGMENTS",
    "Curve primitive: [CURVE_TRIANGLES, CURVE_LINE_SEGMENTS, CURVE_SEGMENTS, CURVE_RIBBONS]");
TF_DEFINE_ENV_SETTING(
    HD_CYCLES_CURVE_TRIANGLE_METHOD, "CURVE_TESSELATED_TRIANGLES",
    "Curve triangle method: [CURVE_CAMERA_TRIANGLES, CURVE_TESSELATED_TRIANGLES]");
TF_DEFINE_ENV_SETTING(HD_CYCLES_CURVE_LINE_METHOD, "CURVE_ACCURATE",
                      "Curve line method: [CURVE_ACCURATE, CURVE_UNCORRECTED]");

/* ===== Integrator Settings ===== */

TF_DEFINE_ENV_SETTING(HD_CYCLES_INTEGRATOR_METHOD, "PATH",
                      "Method of path tracing. [PATH, BRANCHED_PATH]");

TF_DEFINE_ENV_SETTING(HD_CYCLES_DIFFUSE_SAMPLES, 1,
                      "Number of diffuse samples for cycles integrator");

TF_DEFINE_ENV_SETTING(HD_CYCLES_GLOSSY_SAMPLES, 1,
                      "Number of glossy samples for cycles integrator");

TF_DEFINE_ENV_SETTING(HD_CYCLES_TRANSMISSION_SAMPLES, 1,
                      "Number of transmission samples for cycles integrator");

TF_DEFINE_ENV_SETTING(
    HD_CYCLES_AO_SAMPLES, 1,
    "Number of ambient occlusion samples for cycles integrator");

TF_DEFINE_ENV_SETTING(HD_CYCLES_MESH_LIGHT_SAMPLES, 1,
                      "Number of mesh light samples for cycles integrator");

TF_DEFINE_ENV_SETTING(HD_CYCLES_SUBSURFACE_SAMPLES, 1,
                      "Number of subsurface samples for cycles integrator");

TF_DEFINE_ENV_SETTING(HD_CYCLES_VOLUME_SAMPLES, 1,
                      "Number of volume samples for cycles integrator");

// HdCycles Constructor
HdCyclesConfig::HdCyclesConfig()
{
    // -- HdCycles Settings
    enable_logging          = TfGetEnvSetting(HD_CYCLES_ENABLE_LOGGING);
    enable_motion_blur      = TfGetEnvSetting(HD_CYCLES_ENABLE_MOTION_BLUR);
    motion_steps            = TfGetEnvSetting(HD_CYCLES_MOTION_STEPS);
    enable_subdivision      = TfGetEnvSetting(HD_CYCLES_ENABLE_SUBDIVISION);
    subdivision_dicing_rate = atof(
        TfGetEnvSetting(HD_CYCLES_SUBDIVISION_DICING_RATE).c_str());
    max_subdivision               = TfGetEnvSetting(HD_CYCLES_MAX_SUBDIVISION);
    enable_dof                    = TfGetEnvSetting(HD_CYCLES_ENABLE_DOF);
    render_width                  = TfGetEnvSetting(HD_CYCLES_RENDER_WIDTH);
    render_height                 = TfGetEnvSetting(HD_CYCLES_RENDER_HEIGHT);
    use_old_curves                = TfGetEnvSetting(HD_CYCLES_USE_OLD_CURVES);
    enable_transparent_background = TfGetEnvSetting(
        HD_CYCLES_USE_TRANSPARENT_BACKGROUND);

    // -- Cycles Settings
    enable_experimental   = TfGetEnvSetting(HD_CYCLES_ENABLE_EXPERIMENTAL);
    bvh_type              = TfGetEnvSetting(HD_CYCLES_BVH_TYPE);
    device_name           = TfGetEnvSetting(HD_CYCLES_DEVICE_NAME);
    shading_system        = TfGetEnvSetting(HD_CYCLES_SHADING_SYSTEM);
    display_buffer_linear = TfGetEnvSetting(HD_CYCLES_DISPLAY_BUFFER_LINEAR);
    max_samples           = TfGetEnvSetting(HD_CYCLES_MAX_SAMPLES);
    num_threads           = TfGetEnvSetting(HD_CYCLES_NUM_THREADS);
    pixel_size            = TfGetEnvSetting(HD_CYCLES_PIXEL_SIZE);
    tile_size             = pxr::GfVec2i(TfGetEnvSetting(HD_CYCLES_TILE_SIZE_X),
                             TfGetEnvSetting(HD_CYCLES_TILE_SIZE_Y));
    start_resolution      = TfGetEnvSetting(HD_CYCLES_START_RESOLUTION);
    exposure              = atof(TfGetEnvSetting(HD_CYCLES_EXPOSURE).c_str());
    shutter_motion_position = TfGetEnvSetting(
        HD_CYCLES_SHUTTER_MOTION_POSITION);

    default_point_style      = TfGetEnvSetting(HD_CYCLES_DEFAULT_POINT_STYLE);
    default_point_resolution = TfGetEnvSetting(
        HD_CYCLES_DEFAULT_POINT_RESOLUTION);


    // -- Curve Settings

    curve_resolution   = TfGetEnvSetting(HD_CYCLES_CURVE_RESOLUTION);
    curve_subdivisions = TfGetEnvSetting(HD_CYCLES_CURVE_SUBDIVISIONS);

    curve_use_backfaces = TfGetEnvSetting(HD_CYCLES_CURVE_USE_BACKFACES);
    curve_use_encasing  = TfGetEnvSetting(HD_CYCLES_CURVE_USE_ENCASING);
    curve_use_tangent_normal_geometry = TfGetEnvSetting(
        HD_CYCLES_CURVE_USE_TANGENT_NORMAL_GEO);

    curve_shape           = TfGetEnvSetting(HD_CYCLES_CURVE_SHAPE);
    curve_primitive       = TfGetEnvSetting(HD_CYCLES_CURVE_PRIMITIVE);
    curve_triangle_method = TfGetEnvSetting(HD_CYCLES_CURVE_TRIANGLE_METHOD);
    curve_line_method     = TfGetEnvSetting(HD_CYCLES_CURVE_LINE_METHOD);


    // -- Integrator Settings
    integrator_method = TfGetEnvSetting(HD_CYCLES_INTEGRATOR_METHOD);

    diffuse_samples      = TfGetEnvSetting(HD_CYCLES_DIFFUSE_SAMPLES);
    glossy_samples       = TfGetEnvSetting(HD_CYCLES_GLOSSY_SAMPLES);
    transmission_samples = TfGetEnvSetting(HD_CYCLES_TRANSMISSION_SAMPLES);
    ao_samples           = TfGetEnvSetting(HD_CYCLES_AO_SAMPLES);
    mesh_light_samples   = TfGetEnvSetting(HD_CYCLES_MESH_LIGHT_SAMPLES);
    subsurface_samples   = TfGetEnvSetting(HD_CYCLES_SUBSURFACE_SAMPLES);
    volume_samples       = TfGetEnvSetting(HD_CYCLES_VOLUME_SAMPLES);
}

const HdCyclesConfig&
HdCyclesConfig::GetInstance()
{
    return TfSingleton<HdCyclesConfig>::GetInstance();
}

PXR_NAMESPACE_CLOSE_SCOPE