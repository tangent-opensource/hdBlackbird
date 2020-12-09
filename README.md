# HdCycles

[![License](https://img.shields.io/badge/License-Apache%202.0-blue.svg)](LICENSE.txt)

A USD/Hydra RenderDelegate plugin that adds support for the 
Cycles renderer (Blender's physically based path tracer) to any client.

Its goal is to render a one-to-one representation of a USD scene with Cycles. 

This requires three components:
* hdCycles (Cycles Hydra Delegate)
* ndrCycles (Cycles Node Definition Registry)
* usdCycles (Cycles USD Schema)

The first two of which are implemented in this repository.

## Building

### Requirements

* Cycles standalone libraries and headers [Source](https://git.blender.org/gitweb/gitweb.cgi/cycles.git)
* USD 19.xx+
  * Most of the USD requirements need to be available (OpenSubdiv, PNG, OpenImageIO, OpenVDB ...)

### Linux

Make sure to build cycles with `-DCMAKE_POSITION_INDEPENDENT_CODE=ON`

```shell
mkdir build
cd build

cmake -DUSD_ROOT=/path/to/usd/root               \
  -DCYCLES_ROOT=/path/to/cycles                  \
  -DCYCLES_LIBRARY_DIR=/path/to/cycles/build/lib \
  -DCYCLES_INCLUDE_DIRS=/path/to/cycles/src      \
  ..
```

### Windows

```batch
mkdir build
cd build

cmake -DUSD_ROOT=C:/path/to/usd/root               \
  -DCYCLES_ROOT=C:/path/to/cycles                  \
  -DCYCLES_LIBRARY_DIR=C:/path/to/cycles/build/lib \
  -DCYCLES_INCLUDE_DIRS=C:/path/to/cycles/src      \
  ..
```

## Installation

Both the hdCycles plugin and the ndrCycles plugin must be added to the 
`PXR_PLUGINPATH_NAME` environment variable. 

For example:

`PXR_PLUGINPATH_NAME = %HDCYCLES_INSTALL_DIR%/plugin/usd/ndrCycles/resources;%HDCYCLES_INSTALL_DIR%/plugin/usd/hdCycles/resources`

## Notes

### Stability & Performance                           

The codebase is in __active__ development and should be deemed as __unstable__. 

The primary priority is feature-completness. 
Stability and performance will be addressed in the future.

Please file issues for any question or problem.

### Materials

Currently Cycles materials are exported through custom additions to the Blender 
USD Exporter.

Of note, hdCycles expects a flattened Cycles Material graph, with no groups or 
reroute nodes. It also does not use the `Material Output` node. Instead it
favours the USD/Hydra material binding inputs. 

For now only BSDF nodes are registered in the ndr plugin.

### Lights

Currently light node networks are unsupported via USD Lux. 
[A proposal from Pixar](https://graphics.pixar.com/usd/docs/Adapting-UsdLux-to-the-Needs-of-Renderers.html) 
plans to fix these limitations. 
It is planned to support proper world and light materials once the proposal
is accepted.

## Feature Set

Currently supported features:

| hdCycles        | Feature               | Status | Notes                                             |
|:---------------:|-----------------------|--------|---------------------------------------------------|
| **Meshes**      | Basic Mesh            | ✅    |                                                   |
|                 | Geom Subsets          | ✅    |                                                   |
|                 | Subdivision Surface   | ✅    | Ability to set at render time                     |
|                 | Subdivision Surface (Adaptive)   | ❌    |                      |
|                 | Generic Primvars      | ✅    |                                                   |
|                 | UVs                   | ✅    |                                                   |
|                 | Display Colors        | ✅    |                                                   |
|                 | Generic Primitives    | ✅    | (Cube, sphere, cylinder)                          |
|                 | Tangents              | ✅    |                                                   |
|                 | Point Instances       | ✅    |                                                   |
|                 | usdCycles Schema Support| ❌  |                                                   |
|                 | Motion Blur (Transform) | ✅  |                                                   |
|                 | Motion Blur (Deforming) | ✅  | Known slow down.                                  |
|                 | Motion Blur (Velocity)  | ❌  |                                                   |
|                 | Motion Blur (Instances) | ❌  |                                                   |
| **Materials**   | Cycles Material Graph | ✅    | Ongoing support                                   |
|                 | Displacement          | ✅    |                                                   |
|                 | Volumetric            | ❌    |                                                   |
|                 | OSL                   | ❌    |                                                   |
|                 | USD Preview Surface   | ✅    |                                                   |
| **Volumes**     | VDB Support           | ❌    | (Likely will go with foundations implementation)  |
| **Cameras**     | Basic Support         | ✅    |                                                   |
|                 | Depth of Field        | ✅    |                                                   |
|                 | Motion Blur           | ✅    |                                                   |
|                 | usdCycles Schema Support| ❌  |                                                   |
| **Curves**      | BasisCurves           | ✅    |                                                   |
|                 | NURBs                 | ❌    |                                                   |
|                 | Point Instancing      | ❌    |                                                   |
|                 | Motion Blur (Transform) | ✅  |                                                   |
|                 | Motion Blur (Deforming) | ✅  | Known slow down.                                  |
|                 | Motion Blur (Velocity)  | ❌  |                                                   |
|                 | Motion Blur (Instances) | ❌  |                                                   |
|                 | usdCycles Schema Support| ❌  |                                                   |
| **Points**      | Points                | ✅    |                                                   |
|                 | usdCycles Schema Support| ✅  |                                                   |
|                 | Motion Blur (Transform) | ❌  |                                                   |
|                 | Motion Blur (Velocity)  | ❌  |                                                   |
| **Lights**      | Point                 | ✅    |                                                   |
|                 | Directional           | ✅    |                                                   |
|                 | Spot                  | ✅    |                                                   |
|                 | Area                  | ✅    |                                                   |
|                 | Dome                  | ✅    |                                                   |
|                 | Temperature           | ✅    | We manually create a blackbody shader for now...  |
|                 | Light Materials       |   ❌  | Pending support for new USD Light network shaders |
|                 | usdCycles Schema Support| ❌  |                                                   |
| **Render Settings** | Basic Render Settings |✅ |                                                   |
|                 | usdCycles Schema Support  | ❌| Render Settings, Render Products, etc.            |
| **Rendering**   | Combined AOV          | ✅    |                                                   |
|                 | Tiled Rendering       | ❌    |                                                   |
|                 | Full AOV Support      | ❌    |                                                   |
|                 | Cryptomatte           | ❌    |                                                   |
|                 | OCIO Support          | ✅    |                                                   |
|                 | CUDA/GPU Support      | ❌    | Should just require adjustments to build scripts  |

## License

This project is licensed under the [Apache 2 license](LICENSE.txt).

For a full list of third-party licenses see: [LICENSE-THIRDPARTY](LICENSE-THIRDPARTY.txt)

## Attribution

This could not have been made without the help and reference of the following open source projects:
* [USD - Copyright 2016 Pixar Animation Studios - Apache 2.0 License](https://github.com/PixarAnimationStudios/USD)
* [arnold-usd - Copyright 2020 Autodesk - Apache 2.0 License](https://github.com/Autodesk/arnold-usd)
* [arnold-usd - Copyright 2019 Luma Pictures - Apache 2.0 License](https://github.com/LumaPictures/usd-arnold)
* [RadeonProRenderUSD - Copyright 2020 Advanced Micro Devices, Inc - Apache 2.0 License](https://github.com/GPUOpen-LibrariesAndSDKs/RadeonProRenderUSD)
* [Mikktspace - Copyright 2011 Morten S. Mikkelsen - Zlib](http://www.mikktspace.com/)
* [GafferCycles - Copyright 2018 Alex Fuller - BSD 3-Clause License](https://github.com/boberfly/GafferCycles)
