//  Copyright 2021 Tangent Animation
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

#include <pxr/base/tf/pyModule.h>

PXR_NAMESPACE_USING_DIRECTIVE

TF_WRAP_MODULE
{
    TF_WRAP(usdCyclesCameraSettingsAPI);
    TF_WRAP(usdCyclesCurveSettingsAPI);
    TF_WRAP(usdCyclesFilmSettingsAPI);
    //TF_WRAP(usdCyclesIntegratorSettingsAPI);
    //TF_WRAP(usdCyclesLightSettingsAPI);
    //TF_WRAP(usdCyclesMaterialSettingsAPI);
    TF_WRAP(usdCyclesObjectSettingsAPI);
    TF_WRAP(usdCyclesPointsSettingsAPI);
    TF_WRAP(usdCyclesRendererSettingsAPI);
}