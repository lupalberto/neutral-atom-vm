cmake_minimum_required(VERSION 3.20)
project(neutral_atom_vm_bindings LANGUAGES CXX)

cmake_policy(SET CMP0148 NEW)

option(NA_VM_WITH_STIM "Enable Stim-backed stabilizer backend for the Python bindings" ON)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

get_filename_component(NA_VM_ROOT "${CMAKE_SOURCE_DIR}/.." ABSOLUTE)
include("${NA_VM_ROOT}/cmake/vm_sources.cmake")

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

set(NA_VM_SOURCE_FILES_PY "")
foreach(src IN LISTS NA_VM_SOURCE_FILES)
    list(APPEND NA_VM_SOURCE_FILES_PY "../${src}")
endforeach()
add_library(vm STATIC ${NA_VM_SOURCE_FILES_PY})
target_include_directories(vm PUBLIC
    ${CMAKE_SOURCE_DIR}/../src
)
set_target_properties(vm PROPERTIES POSITION_INDEPENDENT_CODE ON)

if(NA_VM_WITH_STIM)
    target_compile_definitions(vm PRIVATE NA_VM_WITH_STIM=1)
    target_include_directories(vm PRIVATE ${STIM_INCLUDE_DIR})
    target_link_libraries(vm PUBLIC ${STIM_LIBRARY})
endif()

pybind11_add_module(_neutral_atom_vm
    ../src/bindings/python/module.cpp
)
target_link_libraries(_neutral_atom_vm PRIVATE vm)
if(NA_VM_WITH_STIM)
    target_compile_definitions(_neutral_atom_vm PRIVATE NA_VM_WITH_STIM=1)
    target_include_directories(_neutral_atom_vm PRIVATE ${STIM_INCLUDE_DIR})
endif()
set_target_properties(_neutral_atom_vm PROPERTIES
    LIBRARY_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/src/neutral_atom_vm
)
install(TARGETS _neutral_atom_vm DESTINATION neutral_atom_vm)
