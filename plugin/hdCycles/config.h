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

#ifndef HD_CYCLES_CONFIG_H
#define HD_CYCLES_CONFIG_H

#include "api.h"
#include <graph/node.h>
#include <iostream>

#include <pxr/base/gf/vec2i.h>
#include <pxr/base/tf/singleton.h>
#include <pxr/pxr.h>


// TODO: Create a proper HdCycles Logger
#include <iostream>

PXR_NAMESPACE_OPEN_SCOPE

/**
 * @brief This env wrapper allows us to use environment variables
 * as overrides to usd schema. The main addition is the eval function
 * which only applies an override if an environment variable is both
 * AUTHORED and DIFFERENT to the default.
 * 
 * This could do with some more work, and likely is a little slow.
 * The current design decision is that the renderParam is setup with
 * the defaults set in this config, usdCycles schema prims are applied 
 * on top, and then if environment variable is authored, that value is 
 * used. To the effect of: usdCycles < Environment variable.
 * 
 * @tparam T 
 */
template<typename T> struct HdCyclesEnvValue {
    HdCyclesEnvValue() = default;

    HdCyclesEnvValue(const char* a_envName, T a_default) {}

    T value;
    bool hasOverride;
    std::string envName;

    bool eval(T& a_previous, bool a_forceInit = false) const
    {
        if (a_forceInit) {
            a_previous = value;
            return true;
        }

        if (hasOverride) {
            a_previous = value;
            std::cout << "[" << envName << "] has been set: " << a_previous
                      << '\n';
        }

        return hasOverride;
    }

    bool eval(ccl::Node* node, const ccl::SocketType* a_previous, bool a_forceInit = false) const
    {
        assert(node);
        assert(a_previous);
        if (a_forceInit) {
            node->set(*a_previous, value);
            return true;
        }

        if (hasOverride) {
            node->set(*a_previous, value);
            std::cout << "[" << envName << "] has been set: " << value
                      << '\n';
        }

        return hasOverride;
    }
};

/**
 * @brief Main singleton that loads and stores mutable, global variables for the lifetime of the
 * cycles render delegate.
 *
 */
class HdCyclesConfig {
public:
    /// Return an instance of HdCyclesConfig.
        static const HdCyclesConfig& GetInstance();

    /* ====== Cycles Settings ====== */

    /**
     * @brief If enabled, Cycles will log 
     *
     */
    bool cycles_enable_logging;

    /**
     * @brief Severity of Cycles logging
     *
     */
    int cycles_logging_severity;

    /**
     * @brief Valid, existing directory to dump shader graphs
     * 
     */
    std::string cycles_shader_graph_dump_dir;

    /* ====== HdCycles Settings ====== */

    /**
     * @brief Use tiles for renders, allows AOV's and better performance
     * EXPERIMENTAL, currently many known issues.
     * 
     */
    bool use_tiled_rendering;

    /**
     * @brief If enabled, HdCycles will log every step
     *
     */
    bool enable_logging;

    /**
     * @brief If enabled, HdCycles stdout progress in a format of 'Progres: 0%'
     *
     */
    bool enable_progress;

    /**
     * @brief Set custom up axis (Z or Y currently supported)
     *
     */
    std::string up_axis;

    /**
     * @brief If enabled, HdCycles will populate object's motion and enable motion blur
     *
     */
    HdCyclesEnvValue<bool> enable_motion_blur;

    /**
     * @brief Number of frames to populate motion for
     *
     */
    HdCyclesEnvValue<int> motion_steps;

    /**
     * @brief If enabled, subdiv meshes will be subdivided
     * 
     */
    HdCyclesEnvValue<bool> enable_subdivision;

    /**
     * @brief Dicing rate of mesh subdivision
     * 
     */
    HdCyclesEnvValue<float> subdivision_dicing_rate;

    /**
     * @brief Maximum amount of subdivisions
     * 
     */
    HdCyclesEnvValue<int> max_subdivision;

    /**
     * @brief Enable dpeth of field for cycles
     * 
     */
    HdCyclesEnvValue<bool> enable_dof;

    /**
     * @brief Width of non interactive render output
     *
     */
    HdCyclesEnvValue<int> render_width;

    /**
     * @brief Height of non interactive render output
     *
     */
    HdCyclesEnvValue<int> render_height;

    /**
     * @brief Disabled by default, if enabled, curves will be generated through mesh geometry.
     * 
     */
    HdCyclesEnvValue<bool> use_old_curves;

    /**
     * @brief Manual override of transparent background for renders
     * 
     */
    HdCyclesEnvValue<bool> enable_transparent_background;

    /**
     * @brief Square sampling values for easier artist control
     * 
     */
    HdCyclesEnvValue<bool> use_square_samples;

    /**
     * @brief Default style of HdPoints. Overridable by primvars
     * 0: Discs, 1: Spheres
     * 
     */
    HdCyclesEnvValue<int> default_point_style;

    /**
     * @brief Default resolution of point mesh. Overridable by primvars
     * 
     */
    HdCyclesEnvValue<int> default_point_resolution;

    /* ======= Cycles Settings ======= */

    /**
     * @brief Should cycles run in experimental mode
     *
     */
    //bool enable_experimental;
    HdCyclesEnvValue<bool> enable_experimental;

    /**
     * @brief Cycles BVH Type. Use Dynamic for any interactive viewport. (DYNAMIC, STATIC)
     * 
     */
    HdCyclesEnvValue<std::string> bvh_type;

    /**
     * @brief Name of cycles render device. (CPU, GPU, etc.)
     *
     */
    HdCyclesEnvValue<std::string> device_name;

    /**
     * @brief Shading system (SVM, OSL)
     * 
     */
    HdCyclesEnvValue<std::string> shading_system;

    /**
     * @brief If false, bytes will be used, if true, half's
     * 
     */
    HdCyclesEnvValue<bool> display_buffer_linear;

    /**
     * @brief Number of samples to render
     *
     */
    HdCyclesEnvValue<int> max_samples;

    /**
     * @brief Number of threads to use for cycles render
     *
     */
    HdCyclesEnvValue<int> num_threads;

    /**
     * @brief Size of pixel
     *
     */
    HdCyclesEnvValue<int> pixel_size;

    /**
     * @brief Size of individual render tile x axis
     *
     */
    HdCyclesEnvValue<int> tile_size_x;

    /**
     * @brief Size of individual render tile y axis
     *
     */
    HdCyclesEnvValue<int> tile_size_y;

    /**
     * @brief Start Resolution of render
     *
     */
    HdCyclesEnvValue<int> start_resolution;

    /**
     * @brief Exposure of cycles film
     *
     */
    HdCyclesEnvValue<float> exposure;

    /**
     * @brief Position of shutter motion position.
     *        0: Start
     *        1: Center
     *        2: End
     *
     */
    HdCyclesEnvValue<int> shutter_motion_position;

    /* ===== Curve Settings ===== */

    /**
     * @brief Curve subdvisions
     * 
     */
    HdCyclesEnvValue<int> curve_subdivisions;

    /* ===== Integrator Settings ===== */

    /**
     * @brief Method of path tracing. (PATH, BRANCHED_PATH)
     *
     */
    HdCyclesEnvValue<std::string> integrator_method;

    /**
     * @brief Number of diffuse samples for cycles integrator
     *
     */
    HdCyclesEnvValue<int> diffuse_samples;

    /**
     * @brief Number of glossy samples for cycles integrator
     *
     */
    HdCyclesEnvValue<int> glossy_samples;

    /**
     * @brief Number of transmission samples for cycles integrator
     *
     */
    HdCyclesEnvValue<int> transmission_samples;

    /**
     * @brief Number of ao samples for cycles integrator
     *
     */
    HdCyclesEnvValue<int> ao_samples;

    /**
     * @brief Number of mesh light samples for cycles integrator
     *
     */
    HdCyclesEnvValue<int> mesh_light_samples;

    /**
     * @brief Number of subsurface samples for cycles integrator
     *
     */
    HdCyclesEnvValue<int> subsurface_samples;

    /**
     * @brief Number of volume samples for cycles integrator
     *
     */
    HdCyclesEnvValue<int> volume_samples;

    /**
     * @brief Number of adaptive min samples
     *
     */
    HdCyclesEnvValue<int> adaptive_min_samples;

private:
    /**
     * @brief Constructor for reading the values from the environment variables.
     * 
     * @return 
     */
    HdCyclesConfig();
    ~HdCyclesConfig()                     = default;
    HdCyclesConfig(const HdCyclesConfig&) = delete;
    HdCyclesConfig(HdCyclesConfig&&)      = delete;
    HdCyclesConfig& operator=(const HdCyclesConfig&) = delete;

    friend class TfSingleton<HdCyclesConfig>;
};

PXR_NAMESPACE_CLOSE_SCOPE

#endif  // HD_CYCLES_CONFIG_H
