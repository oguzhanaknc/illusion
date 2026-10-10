#include <illusion/core/TypeName.hpp>

#include <doctest/doctest.h>

using namespace illusion;

struct TypeNameGlobalStruct
{
};

namespace typename_test
{
class Widget
{
};
enum class Mode
{
    A
};
namespace inner
{
struct Deep
{
};
} // namespace inner
} // namespace typename_test

TEST_CASE("typeName returns the fully qualified name without elaborated prefixes")
{
    CHECK(typeName<TypeNameGlobalStruct>() == "TypeNameGlobalStruct");
    CHECK(typeName<typename_test::Widget>() == "typename_test::Widget");
    CHECK(typeName<typename_test::Mode>() == "typename_test::Mode");
    CHECK(typeName<typename_test::inner::Deep>() == "typename_test::inner::Deep");
}

TEST_CASE("typeName handles fundamental types")
{
    CHECK(typeName<int>() == "int");
    CHECK(typeName<double>() == "double");
    CHECK(typeName<float>() == "float");
}

TEST_CASE("typeName and typeHash are constexpr and distinguish types")
{
    static_assert(typeName<typename_test::Widget>() == "typename_test::Widget");
    static_assert(typeHash<typename_test::Widget>() != typeHash<typename_test::inner::Deep>());
    CHECK(typeHash<int>() == fnv1a64("int"));
}
