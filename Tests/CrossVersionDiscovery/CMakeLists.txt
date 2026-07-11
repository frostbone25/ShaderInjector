cmake_minimum_required(VERSION 3.16)
project(CrossVersionDiscoveryTests LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(SHADER_INJECTOR_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../../ShaderInjector")
set(NLOHMANN_JSON_DIR "${SHADER_INJECTOR_ROOT}/ExternalDependencies/json/nlohmann")

add_executable(cross_version_discovery_tests
    src/CrossVersionDiscoveryTests.cpp
    src/DiscoveryLogicMirror.cpp
    src/ModifiedShaderMatching.cpp
    src/SimilarityDiagnostics.cpp
    "${SHADER_INJECTOR_ROOT}/Hash.cpp"
    "${SHADER_INJECTOR_ROOT}/ShaderAnalysis.cpp"
)

target_include_directories(cross_version_discovery_tests PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/src"
    "${CMAKE_CURRENT_SOURCE_DIR}/ThirdParty"
    "${SHADER_INJECTOR_ROOT}"
    "${NLOHMANN_JSON_DIR}"
)

target_compile_definitions(cross_version_discovery_tests PRIVATE
    CROSS_VERSION_DISCOVERY_DATA_DIR="${CMAKE_CURRENT_SOURCE_DIR}/data"
)

target_compile_options(cross_version_discovery_tests PRIVATE
    -Wall
    -Wextra
)

set_source_files_properties("${SHADER_INJECTOR_ROOT}/Hash.cpp" PROPERTIES
    COMPILE_OPTIONS "-include;${CMAKE_CURRENT_SOURCE_DIR}/compat/msvc_compat.h"
)

enable_testing()
add_test(NAME cross_version_discovery COMMAND cross_version_discovery_tests)
