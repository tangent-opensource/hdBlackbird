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


#include <doctest/doctest.h>

#include <hdCycles/objectSource.h>
#include <hdCycles/transformSource.h>

PXR_NAMESPACE_USING_DIRECTIVE

TEST_SUITE("Testing HdCyclesTransformSource")
{
    // TODO: HdCyclesObjectSource should own object in the future
    //       Include those tests directly into library
    auto _object_ptr = std::make_unique<ccl::Object>();
    auto _object     = _object_ptr.get();

    HdCyclesMatrix4dTimeSampleArray samples;
    GfMatrix4d _fallback;

    TEST_CASE("Empty samples no motion blur - fallback matrix")
    {
        HdCyclesTransformSource src { _object, samples, _fallback };
        CHECK(src.IsValid() == true);
        CHECK(src.Resolve() == true);
        CHECK(src.GetObject()->motion.size() == 0);
    }

    TEST_CASE("Single sample - no motion blur")
    {
        samples.Resize(1);

        HdCyclesTransformSource src { _object, samples, _fallback };
        CHECK(src.IsValid() == true);
        CHECK(src.Resolve() == true);
        CHECK(src.GetObject()->motion.size() == 0);
    }

    TEST_CASE("Multi overlapping samples - no motion blur")
    {
        samples.Resize(10);

        HdCyclesTransformSource src { _object, samples, _fallback };
        CHECK(src.IsValid() == true);
        CHECK(src.Resolve() == true);
        CHECK(src.GetObject()->motion.size() == 0);
    };

    TEST_CASE("Three non overlapping samples")
    {
        samples.Resize(3);
        samples.times[0] = -1.0;
        samples.times[1] = -0.0;
        samples.times[2] = 1.0;

        HdCyclesTransformSource src { _object, samples, _fallback };
        CHECK(src.IsValid() == true);
        CHECK(src.Resolve() == true);
        CHECK(src.GetObject()->motion.size() == 3);
    }

    TEST_CASE("Resample0")
    {
        samples.Resize(2);
        samples.times[0] = -0.5;
        samples.times[1] = 0.5;
        auto result      = HdCyclesTransformSource::ResampleUniform(samples, 5);
        CHECK(result.count == 5);
        CHECK(result.times[0] == doctest::Approx { -0.50 });
        CHECK(result.times[1] == doctest::Approx { -0.25 });
        CHECK(result.times[2] == doctest::Approx { -0.00 });
        CHECK(result.times[3] == doctest::Approx { +0.25 });
        CHECK(result.times[4] == doctest::Approx { +0.50 });
    }

    TEST_CASE("Resample1")
    {
        samples.Resize(2);
        samples.times[0] = -0.5;
        samples.times[1] = 0.5;
        auto result      = HdCyclesTransformSource::ResampleUniform(samples, 3);
        CHECK(result.count == 3);
    }


    TEST_CASE("Resample2")
    {
        samples.Resize(5);
        samples.times[0] = -0.250;
        samples.times[1] = -0.125;
        samples.times[2] = -0.000;
        samples.times[3] = +0.125;
        samples.times[4] = +0.250;
        auto result      = HdCyclesTransformSource::ResampleUniform(samples, 10);
        CHECK(result.count == 11);
    }


    TEST_CASE("Resample3")
    {
        samples.Resize(5);
        samples.times[0] = -0.250;
        samples.times[1] = -0.125;
        samples.times[2] = -0.000;
        samples.times[3] = +0.125;
        samples.times[4] = +0.250;
        auto result      = HdCyclesTransformSource::ResampleUniform(samples, 3);
        CHECK(result.count == 3);
        CHECK(result.times[0] == doctest::Approx { -0.25 });
        CHECK(result.times[1] == doctest::Approx { -0.00 });
        CHECK(result.times[2] == doctest::Approx { +0.25 });
    }
}