cmake_minimum_required(VERSION 3.16)
project(neutral_atom_vm LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

option(NA_VM_BUILD_TESTS "Build tests" ON)
option(NA_VM_WITH_ONEAPI "Enable oneAPI/SYCL backend" OFF)

if(NA_VM_WITH_ONEAPI)
    find_program(DPCPP_COMPILER dpcpp)
    if(NOT DPCPP_COMPILER)
        message(FATAL_ERROR "NA_VM_WITH_ONEAPI requires the dpcpp compiler (e.g. conda-forge dpcpp_linux-64) on PATH")
    endif()
    set(CMAKE_CXX_COMPILER "${DPCPP_COMPILER}" CACHE FILEPATH "SYCL compiler" FORCE)
    set(CMAKE_CXX_STANDARD 17)
    set(CMAKE_CXX_STANDARD_REQUIRED ON)
    set(ONEAPI_COMPILE_OPTIONS -fsycl)
    set(ONEAPI_LINK_OPTIONS -fsycl)
endif()

include(FetchContent)
find_package(Python3 COMPONENTS Interpreter REQUIRED)

add_library(vm STATIC
    src/engine_statevector.cpp
    src/cpu_state_backend.cpp
    src/oneapi_state_backend.cpp
    src/noise.cpp
    src/noise/pauli_utils.cpp
    src/noise/measurement_noise_source.cpp
    src/noise/single_qubit_pauli_source.cpp
    src/noise/two_qubit_pauli_source.cpp
    src/noise/correlated_pauli_source.cpp
    src/noise/idle_dephasing_source.cpp
    src/noise/phase_kick_noise_source.cpp
    src/noise/idle_phase_drift_source.cpp
    src/noise/loss_tracking_source.cpp
    src/hardware_vm.cpp
    src/service/job.cpp
)
target_include_directories(vm PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)
set_target_properties(vm PROPERTIES POSITION_INDEPENDENT_CODE ON)

if(NA_VM_WITH_ONEAPI)
    target_compile_definitions(vm PRIVATE NA_VM_WITH_ONEAPI=1)
    target_compile_options(vm PRIVATE ${ONEAPI_COMPILE_OPTIONS})
    target_link_options(vm PRIVATE ${ONEAPI_LINK_OPTIONS})
endif()

add_executable(vm_demo
    src/main.cpp
)
target_link_libraries(vm_demo PRIVATE vm)
if(NA_VM_WITH_ONEAPI)
    target_compile_options(vm_demo PRIVATE ${ONEAPI_COMPILE_OPTIONS})
    target_link_options(vm_demo PRIVATE ${ONEAPI_LINK_OPTIONS})
endif()

if(NA_VM_BUILD_TESTS)
    enable_testing()
    FetchContent_Declare(
        googletest
        URL https://github.com/google/googletest/archive/refs/tags/v1.15.0.zip
        DOWNLOAD_EXTRACT_TIMESTAMP TRUE
    )
    FetchContent_MakeAvailable(googletest)

    add_executable(vm_tests
        test/vm_tests.cpp
        test/service_api_tests.cpp
        test/noise_tests.cpp
    )
    target_link_libraries(vm_tests PRIVATE vm gtest_main)
    if(NA_VM_WITH_ONEAPI)
        target_compile_options(vm_tests PRIVATE ${ONEAPI_COMPILE_OPTIONS})
        target_link_options(vm_tests PRIVATE ${ONEAPI_LINK_OPTIONS})
    endif()
    include(GoogleTest)
    gtest_discover_tests(vm_tests)
    add_executable(hardware_vm_tests
        test/hardware_vm_tests.cpp
    )
    target_link_libraries(hardware_vm_tests PRIVATE vm gtest_main)
    if(NA_VM_WITH_ONEAPI)
        target_compile_options(hardware_vm_tests PRIVATE ${ONEAPI_COMPILE_OPTIONS})
        target_link_options(hardware_vm_tests PRIVATE ${ONEAPI_LINK_OPTIONS})
    endif()
    gtest_discover_tests(hardware_vm_tests)
endif()
