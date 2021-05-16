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

#include <hdCycles/attributeSource.h>
#include <hdCycles/basisCurves.h>

#include <render/hair.h>

#include <pxr/base/gf/matrix2d.h>
#include <pxr/base/gf/matrix2f.h>
#include <pxr/base/gf/matrix3d.h>
#include <pxr/base/gf/matrix3f.h>
#include <pxr/base/gf/matrix4d.h>
#include <pxr/base/gf/matrix4f.h>
#include <pxr/base/gf/vec2f.h>
#include <pxr/base/gf/vec2i.h>
#include <pxr/base/gf/vec3f.h>
#include <pxr/base/gf/vec3i.h>
#include <pxr/base/gf/vec4f.h>
#include <pxr/base/gf/vec4i.h>
#include <pxr/base/tf/registryManager.h>
#include <pxr/base/vt/array.h>
#include <pxr/imaging/hd/tokens.h>

#include <pxr/base/tf/diagnosticMgr.h>
#include <pxr/base/tf/errorMark.h>

#include <memory>
#include <random>



PXR_NAMESPACE_USING_DIRECTIVE

namespace doctest {

template<> struct StringMaker<TfErrorMark> {
    static String convert(const TfErrorMark& value)
    {
        std::string s;
        for (auto it = value.GetBegin(); it != value.GetEnd(); ++it) {
            s += it->GetPrettyPrintString();
        }
        return String(s.data(), s.size());
    }
};

template<typename F, typename S> struct StringMaker<std::pair<F, S>> {
    static String convert(const std::pair<F, S>& value)
    {
        using namespace std::string_literals;
        std::string s = "first:"s + std::to_string(value.first) + " second:" + std::to_string(value.second);
        return String(s.data(), s.size());
    }
};

}  // namespace doctest

int
main(int argc, char** argv)
{
    // For testing some checks intend to fail internally and they produce errors printed to stdout,
    // we don't want to print all messages, we want to print only those errors for unintended assertions.
    TfDiagnosticMgr::GetInstance().SetQuiet(true);

    doctest::Context context(argc, argv);
    return context.run();
}

TEST_SUITE("Testing HdCyclesAttributeSource")
{
    using V = VtValue;

    void is_holding_float(bool expected) {}

    template<typename T, typename... Ts> void is_holding_float(bool expected, const T& d, const Ts&... ds)
    {
        auto IsHoldingFloat = HdBbHairAttributeSource::IsHoldingFloat;
        CHECK(IsHoldingFloat(V { d }) == expected);

        is_holding_float(expected, ds...);
    }

    TEST_CASE("Is simple type holding float")
    {
        is_holding_float(true, float {}, GfVec2f {}, GfVec3f {}, GfVec4f {}, GfMatrix3f {}, GfMatrix4f {});

        // GfMatrix2f is an exception
        is_holding_float(false, GfMatrix2f {}, GfMatrix2d {}, GfMatrix4d {});
        is_holding_float(false, int {}, GfVec2i {}, GfVec3i {}, GfVec4i {});
        is_holding_float(false, GfHalf {}, GfVec2h {}, GfVec3h {}, GfVec4h {});
        is_holding_float(false, double {}, GfVec2d {}, GfVec3d {}, GfVec4d {});
    }

    TEST_CASE("Is VtArray holding float")
    {
        is_holding_float(true, VtFloatArray {}, VtVec2fArray {}, VtVec3fArray {}, VtVec4fArray {}, VtMatrix3fArray {},
                         VtMatrix4fArray {});

        // GfMatrix2f is an exception
        is_holding_float(false, VtMatrix2fArray {}, VtMatrix2dArray {}, VtMatrix3dArray {}, VtMatrix4dArray {});
        is_holding_float(false, VtUIntArray {}, VtIntArray {}, VtVec2iArray {}, VtVec3iArray {}, VtVec4iArray {});
        is_holding_float(false, VtHalfArray {}, VtVec2hArray {}, VtVec3hArray {}, VtVec4hArray {});
        is_holding_float(false, VtDoubleArray {}, VtVec2dArray {}, VtVec3dArray {}, VtVec4dArray {});
    }

    TEST_CASE("HdType to TypeDesc conversion")
    {
        using Fn = ccl::TypeDesc (*)(const HdType& type);
        auto GetTypeDesc = static_cast<Fn>(HdBbHairAttributeSource::GetTypeDesc);

        CHECK(GetTypeDesc(HdTypeInt32) == ccl::TypeFloat);
        CHECK(GetTypeDesc(HdTypeInt32Vec2) == ccl::TypeFloat2);
        CHECK(GetTypeDesc(HdTypeInt32Vec3) == ccl::TypeVector);
        CHECK(GetTypeDesc(HdTypeInt32Vec4) == ccl::TypeRGBA);

        CHECK(GetTypeDesc(HdTypeUInt32) == ccl::TypeFloat);
        CHECK(GetTypeDesc(HdTypeUInt32Vec2) == ccl::TypeFloat2);
        CHECK(GetTypeDesc(HdTypeUInt32Vec3) == ccl::TypeVector);
        CHECK(GetTypeDesc(HdTypeUInt32Vec4) == ccl::TypeRGBA);

        CHECK(GetTypeDesc(HdTypeFloat) == ccl::TypeFloat);
        CHECK(GetTypeDesc(HdTypeFloatVec2) == ccl::TypeFloat2);
        CHECK(GetTypeDesc(HdTypeFloatVec3) == ccl::TypeVector);
        CHECK(GetTypeDesc(HdTypeFloatVec4) == ccl::TypeRGBA);
        CHECK(GetTypeDesc(HdTypeFloatMat3) == ccl::TypeUnknown);  // unsupported
        CHECK(GetTypeDesc(HdTypeFloatMat4) == ccl::TypeUnknown);  // unsupported

        CHECK(GetTypeDesc(HdTypeDouble) == ccl::TypeFloat);
        CHECK(GetTypeDesc(HdTypeDoubleVec2) == ccl::TypeFloat2);
        CHECK(GetTypeDesc(HdTypeDoubleVec3) == ccl::TypeVector);
        CHECK(GetTypeDesc(HdTypeDoubleVec4) == ccl::TypeRGBA);
        CHECK(GetTypeDesc(HdTypeDoubleMat3) == ccl::TypeUnknown);  // unsupported
        CHECK(GetTypeDesc(HdTypeDoubleMat4) == ccl::TypeUnknown);  // unsupported

        CHECK(GetTypeDesc(HdTypeHalfFloat) == ccl::TypeFloat);
        CHECK(GetTypeDesc(HdTypeHalfFloatVec2) == ccl::TypeFloat2);
        CHECK(GetTypeDesc(HdTypeHalfFloatVec3) == ccl::TypeVector);
        CHECK(GetTypeDesc(HdTypeHalfFloatVec4) == ccl::TypeRGBA);
    }

    TEST_CASE("Checking TfToken role to TypeDesc conversion")
    {
        using Fn = ccl::TypeDesc (*)(const TfToken& type);
        auto GetTypeDesc = static_cast<Fn>(HdBbHairAttributeSource::GetTypeDesc);

        CHECK(GetTypeDesc(HdPrimvarRoleTokens->normal) == ccl::TypeNormal);
        CHECK(GetTypeDesc(HdPrimvarRoleTokens->point) == ccl::TypePoint);
        CHECK(GetTypeDesc(HdPrimvarRoleTokens->vector) == ccl::TypeVector);
        CHECK(GetTypeDesc(HdPrimvarRoleTokens->color) == ccl::TypeColor);
        CHECK(GetTypeDesc(HdPrimvarRoleTokens->textureCoordinate) == ccl::TypeFloat2);

        // few unsupported tokens
        CHECK(GetTypeDesc(HdTokens->geometry) == ccl::TypeUnknown);
        CHECK(GetTypeDesc(HdTokens->velocities) == ccl::TypeUnknown);
    }

    TEST_CASE("Checking destination stride size")
    {
        using Fn = HdTupleType (*)(const ccl::TypeDesc& type_desc);
        auto GetTupleType = static_cast<Fn>(HdBbHairAttributeSource::GetTupleType);
        auto GetTupleTypeCount = [&GetTupleType](const ccl::TypeDesc& type_desc) -> size_t {
            return GetTupleType(type_desc).count;
        };

        // supported types by Cycles
        CHECK(GetTupleTypeCount(ccl::TypeFloat) == 1);
        CHECK(GetTupleTypeCount(ccl::TypeFloat2) == 2);
        CHECK(GetTupleTypeCount(ccl::TypeRGBA) == 4);
        CHECK(GetTupleTypeCount(ccl::TypeColor) == 4);
        CHECK(GetTupleTypeCount(ccl::TypePoint) == 4);
        CHECK(GetTupleTypeCount(ccl::TypeVector) == 4);
        CHECK(GetTupleTypeCount(ccl::TypeNormal) == 4);

        // unsupported
        CHECK(GetTupleTypeCount(ccl::TypeFloat4) == 1);
        CHECK(GetTupleTypeCount(ccl::TypeMatrix33) == 1);
        CHECK(GetTupleTypeCount(ccl::TypeMatrix44) == 1);
        CHECK(GetTupleTypeCount(ccl::TypeMatrix) == 1);
    }

    TEST_CASE("UncheckedCastToFloat")
    {
        auto UncheckedCastToFloat = HdBbHairAttributeSource::UncheckedCastToFloat;

        SUBCASE("Single")
        {
            CHECK(UncheckedCastToFloat(V { static_cast<int>(42) }).Get<float>() == doctest::Approx { 42 });
            CHECK(UncheckedCastToFloat(V { static_cast<double>(42.14) }).Get<float>() == doctest::Approx { 42.14 });
        }

        SUBCASE("Component")
        {
            CHECK(UncheckedCastToFloat(V { GfVec3i { 3, 14, 15 } }).Get<GfVec3f>()[0] == doctest::Approx { 3 });
            CHECK(UncheckedCastToFloat(V { GfVec3i { 3, 14, 15 } }).Get<GfVec3f>()[1] == doctest::Approx { 14 });
            CHECK(UncheckedCastToFloat(V { GfVec3i { 3, 14, 15 } }).Get<GfVec3f>()[2] == doctest::Approx { 15 });
        }

        SUBCASE("Array")
        {
            VtVec3iArray src_array { { 3, 14, 15 }, { 2, 71, 82 } };
            VtVec3fArray dst_array = UncheckedCastToFloat(V { src_array }).Get<VtVec3fArray>();

            REQUIRE(dst_array.size() == 2);

            CHECK(dst_array[0][0] == doctest::Approx { 3 });
            CHECK(dst_array[0][1] == doctest::Approx { 14 });
            CHECK(dst_array[0][2] == doctest::Approx { 15 });

            CHECK(dst_array[1][0] == doctest::Approx { 2 });
            CHECK(dst_array[1][1] == doctest::Approx { 71 });
            CHECK(dst_array[1][2] == doctest::Approx { 82 });
        }
    }
}

TEST_SUITE("Testing HdCyclesCurveAttributeSource")
{
    using V = VtValue;

    auto hair_ptr = std::make_unique<ccl::Hair>();
    ccl::Hair* hair = hair_ptr.get();

    TEST_CASE("Testing interpolation type")
    {
        hair->resize_curves(300, 300 * 6);

        SUBCASE("Constant interpolation")
        {
            HdBbHairAttributeSource source { HdTokens->points, HdPrimvarRoleTokens->none, VtValue {}, hair,
                                             HdInterpolationConstant };
            CHECK(source.GetAttributeElement() == ccl::ATTR_ELEMENT_OBJECT);
        }

        SUBCASE("Uniform interpolation")
        {
            HdBbHairAttributeSource source { HdTokens->points, HdPrimvarRoleTokens->none, VtValue {}, hair,
                                             HdInterpolationUniform };
            CHECK(source.GetAttributeElement() == ccl::ATTR_ELEMENT_CURVE);
        }

        SUBCASE("Varying interpolation")
        {
            HdBbHairAttributeSource source { HdTokens->points, HdPrimvarRoleTokens->none, VtValue {}, hair,
                                             HdInterpolationVarying };
            CHECK(source.GetAttributeElement() == ccl::ATTR_ELEMENT_CURVE_KEY);
        }

        SUBCASE("Vertex interpolation")
        {
            HdBbHairAttributeSource source { HdTokens->points, HdPrimvarRoleTokens->none, VtValue {}, hair,
                                             HdInterpolationVertex };
            CHECK(source.GetAttributeElement() == ccl::ATTR_ELEMENT_CURVE_KEY);
        }

        SUBCASE("Face varying interpolation")
        {
            HdBbHairAttributeSource source { HdTokens->points, HdPrimvarRoleTokens->none, VtValue {}, hair,
                                             HdInterpolationFaceVarying };
            CHECK(source.GetAttributeElement() == ccl::ATTR_ELEMENT_NONE);
        }

        SUBCASE("Instance interpolation")
        {
            HdBbHairAttributeSource source { HdTokens->points, HdPrimvarRoleTokens->none, VtValue {}, hair,
                                             HdInterpolationInstance };
            CHECK(source.GetAttributeElement() == ccl::ATTR_ELEMENT_NONE);
        }
    }

    /*
 * object - value or array with size 1
 */

    void is_valid_value(const HdInterpolation& interpolation, bool expected) {}

    template<typename T, typename... Ts>
    void is_valid_value(const HdInterpolation& interpolation, bool expected, const T& d, const Ts&... ds)
    {
        TfErrorMark error_mark;
        V value_wrapper { d };
        HdBbHairAttributeSource source { HdTokens->points, HdPrimvarRoleTokens->none, value_wrapper, hair,
                                         interpolation };

        auto is_valid = source.IsValid();
        CAPTURE(error_mark);
        CHECK(is_valid == expected);

        is_valid_value(interpolation, expected, ds...);
    }

    TEST_CASE("Testing value validation for object element")
    {
        hair->resize_curves(300, 300 * 6);

        is_valid_value(HdInterpolationConstant, true,                           //
                       int { 42 }, GfHalf { 42 }, float { 42 }, double { 42 },  //
                       GfVec2i {}, GfVec2h {}, GfVec2f {}, GfVec2d {},          //
                       GfVec3i {}, GfVec3h {}, GfVec3f {}, GfVec3d {},          //
                       GfVec4i {}, GfVec4h {}, GfVec4f {}, GfVec4d {}           //
        );

        // unsupported
        is_valid_value(HdInterpolationConstant, false,  //
                       GfMatrix2f {}, GfMatrix2d {},    //
                       GfMatrix3f {}, GfMatrix3d {},    //
                       GfMatrix4f {}, GfMatrix4d {}     //
        );
    }

    TEST_CASE("Testing array validation for object element")
    {
        hair->resize_curves(300, 300 * 6);

        // exactly one element is supported
        is_valid_value(HdInterpolationConstant, true,                                                       //
                       VtIntArray { {} }, VtHalfArray { {} }, VtFloatArray { {} }, VtDoubleArray { {} },    //
                       VtVec2iArray { {} }, VtVec2hArray { {} }, VtVec2fArray { {} }, VtVec2dArray { {} },  //
                       VtVec3iArray { {} }, VtVec3hArray { {} }, VtVec3fArray { {} }, VtVec3dArray { {} },  //
                       VtVec4iArray { {} }, VtVec4hArray { {} }, VtVec4fArray { {} }, VtVec4dArray { {} }   //
        );

        // zero size is not supported
        is_valid_value(HdInterpolationConstant, false,                                      //
                       VtIntArray {}, VtHalfArray {}, VtFloatArray {}, VtDoubleArray {},    //
                       VtVec2iArray {}, VtVec2hArray {}, VtVec2fArray {}, VtVec2dArray {},  //
                       VtVec3iArray {}, VtVec3hArray {}, VtVec3fArray {}, VtVec3dArray {},  //
                       VtVec4iArray {}, VtVec4hArray {}, VtVec4fArray {}, VtVec4dArray {}   //
        );

        // two or more size is not supported
        is_valid_value(
            HdInterpolationConstant, false,                                                                      //
            VtIntArray { {}, {} }, VtHalfArray { {}, {} }, VtFloatArray { {}, {} }, VtDoubleArray { {}, {} },    //
            VtVec2iArray { {}, {} }, VtVec2hArray { {}, {} }, VtVec2fArray { {}, {} }, VtVec2dArray { {}, {} },  //
            VtVec3iArray { {}, {} }, VtVec3hArray { {}, {} }, VtVec3fArray { {}, {} }, VtVec3dArray { {}, {} },  //
            VtVec4iArray { {}, {} }, VtVec4hArray { {}, {} }, VtVec4fArray { {}, {} }, VtVec4dArray { {}, {} },  //
            VtIntArray { {}, {}, {}, {}, {} }                                                                    //
        );
    }

    /*
 * arrays only
 */

    void is_valid_for_interpolation(const HdInterpolation& interpolation, size_t expected)
    {
        SUBCASE("unsupported single values")
        {
            is_valid_value(interpolation, false,                                    //
                           int { 42 }, GfHalf { 42 }, float { 42 }, double { 42 },  //
                           GfVec2i {}, GfVec2h {}, GfVec2f {}, GfVec2d {},          //
                           GfVec3i {}, GfVec3h {}, GfVec3f {}, GfVec3d {},          //
                           GfVec4i {}, GfVec4h {}, GfVec4f {}, GfVec4d {}           //
            );

            is_valid_value(interpolation, false,          //
                           GfMatrix2f {}, GfMatrix2d {},  //
                           GfMatrix3f {}, GfMatrix3d {},  //
                           GfMatrix4f {}, GfMatrix4d {}   //
            );
        }

        SUBCASE("array size same as expected")
        {
            is_valid_value(interpolation, true,  //
                           VtIntArray(expected), VtHalfArray(expected), VtFloatArray(expected),
                           VtDoubleArray(expected),  //
                           VtVec2iArray(expected), VtVec2hArray(expected), VtVec2fArray(expected),
                           VtVec2dArray(expected),  //
                           VtVec3iArray(expected), VtVec3hArray(expected), VtVec3fArray(expected),
                           VtVec3dArray(expected),  //
                           VtVec4iArray(expected), VtVec4hArray(expected), VtVec4fArray(expected),
                           VtVec4dArray(expected)  //
            );
        }

        SUBCASE("array size equal to 1")
        {
            const size_t mock_size = 1;
            is_valid_value(interpolation, false,  //
                           VtIntArray(mock_size), VtHalfArray(mock_size), VtFloatArray(mock_size),
                           VtDoubleArray(mock_size),  //
                           VtVec2iArray(mock_size), VtVec2hArray(mock_size), VtVec2fArray(mock_size),
                           VtVec2dArray(mock_size),  //
                           VtVec3iArray(mock_size), VtVec3hArray(mock_size), VtVec3fArray(mock_size),
                           VtVec3dArray(mock_size),  //
                           VtVec4iArray(mock_size), VtVec4hArray(mock_size), VtVec4fArray(mock_size),
                           VtVec4dArray(mock_size)  //
            );
        }
    }

    TEST_CASE("Testing value validation for varying element")
    {
        const size_t num_curves = 30;
        const size_t num_keys = num_curves * 6;
        hair->resize_curves(num_curves, num_keys);

        is_valid_for_interpolation(HdInterpolationVarying, num_keys);
    }

    TEST_CASE("Testing value validation for vertex element")
    {
        const size_t num_curves = 30;
        const size_t num_keys = num_curves * 6;
        hair->resize_curves(num_curves, num_keys);

        is_valid_for_interpolation(HdInterpolationVertex, num_keys);
    }

    TEST_CASE("Testing value validation for uniform element")
    {
        const size_t num_curves = 30;
        const size_t num_keys = num_curves * 6;
        hair->resize_curves(num_curves, num_keys);

        is_valid_for_interpolation(HdInterpolationUniform, num_curves);
    }

    /*
 * Size has been checked, check the data
 */

    template<typename T> VtArray<T> generate_random_vector(std::default_random_engine & generator, size_t size)
    {
        static_assert(GfIsGfVec<T>::value, "not a vector!");
        constexpr size_t num_dim = T::dimension;

        VtArray<T> random_array;
        random_array.reserve(size);

        std::uniform_real_distribution<float> distribution(-42.0, 42.0);

        for (size_t i = 0; i < size; ++i) {
            T v {};
            for (size_t d = 0; d < num_dim; ++d) {
                float number = distribution(generator);
                v[d] = number;
            }
            random_array.push_back(std::move(v));
        }

        return random_array;
    }

    void generate_random_curves(std::default_random_engine & generator)
    {
        hair->clear();

        std::uniform_int_distribution<int> curve_distribution(2, 1000);
        std::uniform_int_distribution<int> keys_distribution(2, 42);

        const int num_curves = curve_distribution(generator);
        const int num_keys = num_curves * keys_distribution(generator);
        hair->resize_curves(num_curves, num_keys);

        INFO("Testing curve vertex data %s:%s", num_curves, num_keys);

        // generate data
        const VtArray<GfVec3d> random_vector = generate_random_vector<GfVec3d>(generator, num_keys);

        TfErrorMark error_mark;
        V value_wrapper { random_vector };
        HdBbHairAttributeSource source { HdTokens->points, HdPrimvarRoleTokens->none, value_wrapper, hair,
                                         HdInterpolationVertex };

        // double check
        CAPTURE(error_mark);
        REQUIRE(source.IsValid());

        // resolve
        REQUIRE(source.Resolve());

        // check the data
        auto attribute = source.GetAttribute();
        REQUIRE(attribute != nullptr);

        auto data = attribute->data_float3();
        for (size_t i {}; i < num_keys; ++i) {
            const ccl::float3& a = data[i];
            const GfVec3d& b = random_vector[i];

            CHECK(a[0] == doctest::Approx { b[0] });
            CHECK(a[1] == doctest::Approx { b[1] });
            CHECK(a[2] == doctest::Approx { b[2] });
        }
    }

    TEST_CASE("Testing vertex attribute data")
    {
        std::default_random_engine generator {};

        // alter number of iterations for more/less tests
        for (size_t i {}; i < 10; ++i) {
            generate_random_curves(generator);
        }
    }
}
