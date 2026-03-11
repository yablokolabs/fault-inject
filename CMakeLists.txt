cmake_minimum_required(VERSION 3.25)
project(fault-inject
    VERSION 0.1.0
    LANGUAGES CXX
    DESCRIPTION "Fault injection and resilience testing framework for avionics research"
)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# --- Strict warnings ---
add_compile_options(-Wall -Wextra -Wpedantic -Werror)

# --- Header-only library ---
add_library(fault INTERFACE)
target_include_directories(fault INTERFACE
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)

# --- Tests ---
option(FAULT_BUILD_TESTS "Build tests" ON)
if(FAULT_BUILD_TESTS)
    enable_testing()
    add_executable(test_fault tests/test_fault.cpp)
    target_link_libraries(test_fault PRIVATE fault)
    add_test(NAME test_fault COMMAND test_fault)
endif()

# --- Examples ---
option(FAULT_BUILD_EXAMPLES "Build examples" ON)
if(FAULT_BUILD_EXAMPLES)
    add_executable(resilience_demo examples/resilience_demo.cpp)
    target_link_libraries(resilience_demo PRIVATE fault)
endif()
