cmake_minimum_required(VERSION 3.16)
project(neutral_atom_vm LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

option(NA_VM_BUILD_TESTS "Build tests" ON)
option(NA_VM_WITH_ONEAPI "Enable oneAPI/SYCL backend" OFF)
option(NA_VM_WITH_STIM "Enable Stim-backed stabilizer backend" ON)

if(NA_VM_WITH_ONEAPI)
    find_program(DPCPP_COMPILER dpcpp)
    if(NOT DPCPP_COMPILER)
        message(FATAL_ERROR "NA_VM_WITH_ONEAPI requires the dpcpp compiler (e.g. conda-forge dpcpp_linux-64) on PATH")
    endif()
    set(CMAKE_CXX_COMPILER "${DPCPP_COMPILER}" CACHE FILEPATH "SYCL compiler" FORCE)
    set(CMAKE_CXX_STANDARD 17)
    set(CMAKE_CXX_STANDARD_REQUIRED ON)
    set(ONEAPI_COMPILE_OPTIONS -fsycl -fno-sycl-id-queries-fit-in-int)
    set(ONEAPI_LINK_OPTIONS -fsycl)
endif()

if(NA_VM_WITH_STIM)
    if(DEFINED ENV{CONDA_PREFIX})
        list(APPEND CMAKE_PREFIX_PATH $ENV{CONDA_PREFIX})
    endif()
    find_path(STIM_INCLUDE_DIR stim.h
        HINTS $ENV{CONDA_PREFIX}/include
    )
    find_library(STIM_LIBRARY stim
        HINTS $ENV{CONDA_PREFIX}/lib
    )
    if(NOT STIM_INCLUDE_DIR OR NOT STIM_LIBRARY)
        message(FATAL_ERROR "NA_VM_WITH_STIM requires stim headers and library (set CONDA_PREFIX or STIM paths)")
    endif()
endif()

include(FetchContent)
find_package(Python3 COMPONENTS Interpreter REQUIRED)

    add_library(vm STATIC
        src/engine_statevector.cpp
        src/cpu_state_backend.cpp
        src/oneapi_state_backend.cpp
        src/noise.cpp
        src/noise/amplitude_damping_source.cpp
        src/noise/pauli_utils.cpp
        src/noise/measurement_noise_source.cpp
        src/noise/single_qubit_pauli_source.cpp
        src/noise/two_qubit_pauli_source.cpp
        src/noise/correlated_pauli_source.cpp
        src/noise/device_pauli_noise_source.cpp
        src/noise/device_noise_builder.cpp
        src/noise/idle_dephasing_source.cpp
        src/noise/phase_kick_noise_source.cpp
    src/noise/idle_phase_drift_source.cpp
    src/noise/loss_tracking_source.cpp
    src/hardware_vm.cpp
    src/stabilizer_backend.cpp
    src/service/job.cpp
    src/service/scheduler.cpp
    src/service/job_service.cpp
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
if(NA_VM_WITH_STIM)
    target_compile_definitions(vm PRIVATE NA_VM_WITH_STIM=1)
    target_include_directories(vm PRIVATE ${STIM_INCLUDE_DIR})
    target_link_libraries(vm PUBLIC ${STIM_LIBRARY})
endif()

add_executable(vm_demo
    src/main.cpp
)
target_link_libraries(vm_demo PRIVATE vm)
if(NA_VM_WITH_ONEAPI)
    target_compile_options(vm_demo PRIVATE ${ONEAPI_COMPILE_OPTIONS})
    target_link_options(vm_demo PRIVATE ${ONEAPI_LINK_OPTIONS})
endif()
if(NA_VM_WITH_STIM)
    target_include_directories(vm_demo PRIVATE ${STIM_INCLUDE_DIR})
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
        test/statevector_engine_sync_tests.cpp
        test/service_job_service_tests.cpp
        test/scheduler_tests.cpp
    )
    target_link_libraries(vm_tests PRIVATE vm gtest_main)
    if(NA_VM_WITH_STIM)
        target_include_directories(vm_tests PRIVATE ${STIM_INCLUDE_DIR})
    endif()
    if(NA_VM_WITH_ONEAPI)
        target_compile_options(vm_tests PRIVATE ${ONEAPI_COMPILE_OPTIONS})
        target_link_options(vm_tests PRIVATE ${ONEAPI_LINK_OPTIONS})
        target_sources(vm_tests PRIVATE test/oneapi_gpu_tests.cpp)
    endif()
    include(GoogleTest)
    gtest_discover_tests(vm_tests)
    add_executable(hardware_vm_tests
        test/hardware_vm_tests.cpp
    )
    target_link_libraries(hardware_vm_tests PRIVATE vm gtest_main)
    if(NA_VM_WITH_STIM)
        target_include_directories(hardware_vm_tests PRIVATE ${STIM_INCLUDE_DIR})
    endif()
    if(NA_VM_WITH_ONEAPI)
        target_compile_options(hardware_vm_tests PRIVATE ${ONEAPI_COMPILE_OPTIONS})
        target_link_options(hardware_vm_tests PRIVATE ${ONEAPI_LINK_OPTIONS})
    endif()
    gtest_discover_tests(hardware_vm_tests)
endif()
