cmake_minimum_required(VERSION 3.20)
project(neutral_atom_vm_bindings LANGUAGES CXX)

cmake_policy(SET CMP0148 NEW)

option(NA_VM_WITH_ONEAPI "Enable oneAPI/SYCL backend for the Python bindings" OFF)
option(NA_VM_WITH_STIM "Enable Stim-backed stabilizer backend for the Python bindings" ON)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(ONEAPI_COMPILE_OPTIONS "")
set(ONEAPI_LINK_OPTIONS "")
set(ONEAPI_TOOLCHAIN_ROOT "")

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

execute_process(
    COMMAND ${Python3_EXECUTABLE} -c
        "import pybind11, os; print(os.path.join(os.path.dirname(pybind11.__file__), 'share', 'cmake', 'pybind11'))"
    OUTPUT_VARIABLE PYBIND11_CMAKE_DIR
    OUTPUT_STRIP_TRAILING_WHITESPACE
)
find_package(pybind11 CONFIG REQUIRED HINTS ${PYBIND11_CMAKE_DIR})

add_library(vm STATIC
    ../src/engine_statevector.cpp
    ../src/cpu_state_backend.cpp
    ../src/oneapi_state_backend.cpp
    ../src/noise.cpp
    ../src/noise/pauli_utils.cpp
    ../src/noise/amplitude_damping_source.cpp
    ../src/noise/measurement_noise_source.cpp
    ../src/noise/single_qubit_pauli_source.cpp
    ../src/noise/two_qubit_pauli_source.cpp
    ../src/noise/correlated_pauli_source.cpp
    ../src/noise/device_pauli_noise_source.cpp
    ../src/noise/device_noise_builder.cpp
    ../src/noise/idle_dephasing_source.cpp
    ../src/noise/phase_kick_noise_source.cpp
    ../src/noise/idle_phase_drift_source.cpp
    ../src/noise/loss_tracking_source.cpp
    ../src/hardware_vm.cpp
    ../src/stabilizer_backend.cpp
    ../src/service/job.cpp
    ../src/service/job_validation.cpp
    ../src/service/scheduler.cpp
    ../src/service/job_service.cpp
)
target_include_directories(vm PUBLIC
    ${CMAKE_SOURCE_DIR}/../src
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

pybind11_add_module(_neutral_atom_vm
    ../src/bindings/python/module.cpp
)
target_link_libraries(_neutral_atom_vm PRIVATE vm)
if(NA_VM_WITH_ONEAPI)
    target_compile_options(_neutral_atom_vm PRIVATE ${ONEAPI_COMPILE_OPTIONS})
    target_link_options(_neutral_atom_vm PRIVATE ${ONEAPI_LINK_OPTIONS})
    target_compile_definitions(_neutral_atom_vm PRIVATE NA_VM_WITH_ONEAPI=1)
endif()
if(NA_VM_WITH_STIM)
    target_compile_definitions(_neutral_atom_vm PRIVATE NA_VM_WITH_STIM=1)
    target_include_directories(_neutral_atom_vm PRIVATE ${STIM_INCLUDE_DIR})
endif()
set_target_properties(_neutral_atom_vm PROPERTIES
    LIBRARY_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/src/neutral_atom_vm
)
install(TARGETS _neutral_atom_vm DESTINATION neutral_atom_vm)
