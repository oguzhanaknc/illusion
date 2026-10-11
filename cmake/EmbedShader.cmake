# Script mode helper for illusion_add_shaders(): turns a SPIR-V file into a C++ source file.
# Inputs: INPUT (.spv), OUTPUT (.cpp), FUNCTION, NAMESPACE, HEADER.

file(READ "${INPUT}" hex HEX)
string(LENGTH "${hex}" hex_length)
if(hex_length EQUAL 0)
    message(FATAL_ERROR "EmbedShader: ${INPUT} is empty")
endif()

string(REGEX REPLACE "([0-9a-f][0-9a-f])" "0x\\1," bytes "${hex}")
# 16 bytes per line keeps the generated file readable and diff-friendly.
string(REGEX REPLACE "((0x[0-9a-f][0-9a-f],){16})" "\\1\n    " bytes "${bytes}")

file(WRITE "${OUTPUT}" "// Generated from ${INPUT} by illusion_add_shaders(); do not edit.

#include <${HEADER}>

namespace
{
// SPIR-V is a stream of 32-bit words.
alignas(4) constexpr unsigned char kBytecode[] = {
    ${bytes}
};
} // namespace

namespace ${NAMESPACE}
{

std::span<const std::byte> ${FUNCTION}() noexcept
{
    return std::as_bytes(std::span<const unsigned char>(kBytecode));
}

} // namespace ${NAMESPACE}
")
