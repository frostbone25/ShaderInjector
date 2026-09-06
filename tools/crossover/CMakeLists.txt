cmake_minimum_required(VERSION 3.20)
project(ShaderInjectorCrossOver LANGUAGES C CXX RC)

get_filename_component(default_source_dir "${CMAKE_CURRENT_SOURCE_DIR}/../.." ABSOLUTE)
set(SHADER_INJECTOR_SOURCE_DIR "${default_source_dir}" CACHE PATH "Root of the ShaderInjector repository")
set(MINHOOK_SOURCE_DIR "" CACHE PATH "Root of the MinHook v1.3.4 source repository")
if(NOT WIN32 OR NOT MINGW OR NOT CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    message(FATAL_ERROR "Use the supplied Windows x86-64 MinGW GCC toolchain.")
endif()
if(NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
    message(FATAL_ERROR "Shader Injector requires the x86-64 target.")
endif()
foreach(dependency SHADER_INJECTOR_SOURCE_DIR MINHOOK_SOURCE_DIR)
    if(NOT IS_DIRECTORY "${${dependency}}")
        message(FATAL_ERROR "Set -D${dependency} to its existing source checkout.")
    endif()
    get_filename_component(${dependency} "${${dependency}}" ABSOLUTE)
endforeach()
set(injector_dir "${SHADER_INJECTOR_SOURCE_DIR}/ShaderInjector")
set(injector_project "${injector_dir}/ShaderInjector.vcxproj")
if(NOT EXISTS "${injector_project}" OR NOT EXISTS "${MINHOOK_SOURCE_DIR}/src/hook.c")
    message(FATAL_ERROR "Expected ShaderInjector/ShaderInjector.vcxproj and MinHook src/hook.c.")
endif()

# Read the upstream project source list to avoid compiling examples or losing a
# source file when the compatibility patch changes the project.
file(STRINGS "${injector_project}" compile_items REGEX "<ClCompile Include=")
set(injector_sources)
foreach(item IN LISTS compile_items)
    string(REGEX MATCH "Include=\"([^\"]+)\"" include_attribute "${item}")
    string(REPLACE "\\" "/" source_relative "${CMAKE_MATCH_1}")
    list(APPEND injector_sources "${injector_dir}/${source_relative}")
endforeach()
list(LENGTH injector_sources injector_source_count)
if(injector_source_count LESS 40)
    message(FATAL_ERROR "Unexpected project source list: ${injector_source_count} entries.")
endif()
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${injector_project}")

# The supplied MSVC LTCG .lib is incompatible with GNU ld. Build exactly the
# x64 MinHook C sources and include the matching header from the same checkout.
add_library(minhook STATIC
    "${MINHOOK_SOURCE_DIR}/src/buffer.c"
    "${MINHOOK_SOURCE_DIR}/src/hook.c"
    "${MINHOOK_SOURCE_DIR}/src/trampoline.c"
    "${MINHOOK_SOURCE_DIR}/src/hde/hde64.c")
target_include_directories(minhook PUBLIC "${MINHOOK_SOURCE_DIR}/include")

add_library(dsound SHARED ${injector_sources}
    "${injector_dir}/exports.def"
    "${injector_dir}/ShaderInjector.rc")
set_target_properties(dsound PROPERTIES
    PREFIX ""
    CXX_STANDARD 17
    CXX_STANDARD_REQUIRED YES)
target_include_directories(dsound PRIVATE
    "${injector_dir}"
    "${injector_dir}/Resources"
    "${injector_dir}/ExternalDependencies/json/nlohmann"
    "${injector_dir}/ExternalDependencies/imgui-1.92.8"
    "${injector_dir}/ExternalDependencies/imgui-1.92.8/backends"
    "${injector_dir}/ExternalDependencies/inifile-cpp/include"
    "${injector_dir}/ExternalDependencies/renderdoc/include")
target_compile_definitions(dsound PRIVATE
    NDEBUG SHADERINJECTOR_EXPORTS _WINDOWS _USRDLL UNICODE _UNICODE NOMINMAX)
# Upstream Hash.h relies on incidental cstdint inclusion. Its MinHook callsites
# also use MSVC's implicit function-pointer-to-void-pointer conversion.
target_compile_options(dsound PRIVATE
    "$<$<COMPILE_LANGUAGE:CXX>:-include;cstdint;-fpermissive>")

include(CheckCXXSourceCompiles)
check_cxx_source_compiles("#include <windows.h>
#include <d3d12shader.h>
int main() { return static_cast<int>(D3D12_SHVER_LIBRARY); }"
    HAVE_D3D12_SHVER_LIBRARY)
if(NOT HAVE_D3D12_SHVER_LIBRARY)
    # Microsoft DirectX-Headers d3d12shader.h defines this shader type as 6.
    target_compile_definitions(dsound PRIVATE D3D12_SHVER_LIBRARY=6)
endif()

target_link_libraries(dsound PRIVATE
    minhook d3d12 dxgi d3dcompiler version dwmapi psapi ole32)
# Keep all GNU runtime dependencies in the DLL; the resulting imports are
# Windows/UCRT APIs, with no companion libstdc++/libgcc/libwinpthread DLL.
target_link_options(dsound PRIVATE
    -static -static-libgcc -static-libstdc++ -Wl,--subsystem,windows)
message(STATUS "Shader Injector sources: ${injector_source_count}; output: dsound.dll")
