cmake_minimum_required(VERSION 3.20)
project(neutral_atom_vm_bindings LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

execute_process(
    COMMAND ${Python3_EXECUTABLE} -c
        "import pybind11, os; print(os.path.join(os.path.dirname(pybind11.__file__), 'share', 'cmake', 'pybind11'))"
    OUTPUT_VARIABLE PYBIND11_CMAKE_DIR
    OUTPUT_STRIP_TRAILING_WHITESPACE
)
find_package(pybind11 CONFIG REQUIRED HINTS ${PYBIND11_CMAKE_DIR})

add_library(vm STATIC
    ../src/engine_statevector.cpp
    ../src/noise.cpp
    ../src/hardware_vm.cpp
    ../src/service/job.cpp
)
target_include_directories(vm PUBLIC
    ${CMAKE_SOURCE_DIR}/../src
)
set_target_properties(vm PROPERTIES POSITION_INDEPENDENT_CODE ON)

pybind11_add_module(_neutral_atom_vm
    ../src/bindings/python/module.cpp
)
target_link_libraries(_neutral_atom_vm PRIVATE vm)
set_target_properties(_neutral_atom_vm PROPERTIES
    LIBRARY_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/src/neutral_atom_vm
)
install(TARGETS _neutral_atom_vm DESTINATION neutral_atom_vm)
