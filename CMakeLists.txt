cmake_minimum_required(VERSION 3.13 FATAL_ERROR)
project(ChakravyuhaPasses CXX)

find_package(LLVM REQUIRED CONFIG)
message(STATUS "Found LLVM ${LLVM_PACKAGE_VERSION}")
message(STATUS "Using LLVM_DIR: ${LLVM_DIR}")

include_directories(${LLVM_INCLUDE_DIRS})
add_definitions(${LLVM_DEFINITIONS})
add_compile_options(${LLVM_CXX_FLAGS}) # Essential for proper C++ dialect/standards

add_library(HelloWorldPass SHARED HelloWorldPass.cpp)

target_link_libraries(HelloWorldPass PRIVATE ${LLVM_LIBS})

set_target_properties(HelloWorldPass PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib"
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib"
)
