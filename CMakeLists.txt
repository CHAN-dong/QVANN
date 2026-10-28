cmake_minimum_required(VERSION 3.18)
project(QVANN LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

find_package(OpenSSL 1.1.1 REQUIRED)
find_package(OpenMP REQUIRED)
if(DEFINED ENV{QVANN_DEPS_DIR})
    set(QVANN_DEFAULT_DEPS "$ENV{QVANN_DEPS_DIR}")
else()
    set(QVANN_DEFAULT_DEPS "${CMAKE_CURRENT_SOURCE_DIR}/.deps")
endif()
set(QVANN_DEPS_DIR "${QVANN_DEFAULT_DEPS}" CACHE PATH "External QVANN dependency directory")
set(FAISS_DIR "${QVANN_DEPS_DIR}/LearnedEarlyTer-20SIGMOD")
if(NOT EXISTS "${FAISS_DIR}/IndexHNSW.h")
    message(FATAL_ERROR "Set QVANN_DEPS_DIR to the compatible external Faiss/LightGBM dependency directory. See README.md.")
endif()
find_library(FAISS_LIBRARY NAMES faiss PATHS "${FAISS_DIR}" NO_DEFAULT_PATH REQUIRED)
find_library(LIGHTGBM_LIBRARY NAMES _lightgbm PATHS "${FAISS_DIR}/lib" NO_DEFAULT_PATH REQUIRED)
set(CMAKE_BUILD_RPATH "${FAISS_DIR};${FAISS_DIR}/lib;${QVANN_DEPS_DIR}/runtime/lib")

# Client verification has no dependency on the server's graph implementation.
add_library(qvann_client
    src/common/codec.cpp
    src/index/merkle.cpp
    src/index/quantizer.cpp
    src/optimize/expansion/record.cpp
    src/optimize/refinement/bounds.cpp
    src/optimize/traversal/lookup.cpp
    src/utils/hash.cpp
    src/utils/io.cpp
    src/verify/auth.cpp
    src/verify/replay.cpp
    src/verify/result.cpp
    src/verify/verifier.cpp
)
# Quantizer construction from a graph is compiled into qvann_core below.
target_include_directories(qvann_client PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/src")
target_link_libraries(qvann_client PUBLIC OpenSSL::Crypto)
target_compile_options(qvann_client PRIVATE -Wall -Wextra -Wpedantic)

add_library(qvann_core
    src/index/graph.cpp
    src/index/build.cpp
    src/index/index.cpp
    src/optimize/expansion/ordering.cpp
    src/optimize/refinement/refine.cpp
    src/query/search.cpp
    src/query/topk.cpp
    src/query/proof.cpp
    src/utils/config.cpp
    src/utils/metrics.cpp
)
target_include_directories(qvann_core SYSTEM PUBLIC "${QVANN_DEPS_DIR}" "${FAISS_DIR}" "${FAISS_DIR}/include")
target_link_libraries(qvann_core PUBLIC qvann_client OpenMP::OpenMP_CXX "${FAISS_LIBRARY}" "${LIGHTGBM_LIBRARY}" pthread dl)
target_compile_options(qvann_core PRIVATE -Wall -Wextra -Wpedantic)

add_executable(qvann src/main.cpp)
target_link_libraries(qvann PRIVATE qvann_core)

option(QVANN_BUILD_BASELINES "Build ANNProof, VLAH and MR-tree" ON)
if(QVANN_BUILD_BASELINES)
    add_library(baseline_support STATIC src/baselines/keccak.cpp)
    target_include_directories(baseline_support SYSTEM PUBLIC
        "${QVANN_DEPS_DIR}" "${FAISS_DIR}" "${FAISS_DIR}/include")
    target_link_libraries(baseline_support PUBLIC OpenSSL::Crypto OpenMP::OpenMP_CXX
        "${FAISS_LIBRARY}" "${LIGHTGBM_LIBRARY}" pthread dl)
    foreach(method annproof vlah)
        add_executable(${method} src/baselines/${method}.cpp)
        target_link_libraries(${method} PRIVATE baseline_support)
    endforeach()
    add_executable(mr src/baselines/mr.cpp)
    target_link_libraries(mr PRIVATE OpenSSL::Crypto)
endif()

option(QVANN_BUILD_EXPERIMENTS "Build paper-specific measurement executables" ON)
if(QVANN_BUILD_EXPERIMENTS)
    add_executable(granularity src/experiments/granularity.cpp)
    target_link_libraries(granularity PRIVATE qvann_core)
endif()

include(CTest)
if(BUILD_TESTING)
    add_executable(test_quantizer tests/quantizer.cpp)
    target_link_libraries(test_quantizer PRIVATE qvann_core)
    add_test(NAME quantizer COMMAND test_quantizer)

    add_executable(test_proof tests/proof.cpp)
    target_link_libraries(test_proof PRIVATE qvann_client)
    add_test(NAME proof COMMAND test_proof)

    add_executable(test_tamper tests/check.cpp tests/tamper.cpp)
    target_link_libraries(test_tamper PRIVATE qvann_core)
    foreach(test_target test_quantizer test_proof test_tamper)
        target_include_directories(${test_target} PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
    endforeach()
    add_test(NAME tamper COMMAND test_tamper "${CMAKE_CURRENT_BINARY_DIR}/test-output")
endif()
