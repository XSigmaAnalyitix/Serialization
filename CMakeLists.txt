cmake_minimum_required(VERSION 3.15)

project(Serialization
    VERSION 1.0.0
    LANGUAGES CXX
)

# Set C++ standard to C++20
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Add nlohmann/json as a header-only INTERFACE library
add_library(nlohmann_json INTERFACE)
target_include_directories(nlohmann_json INTERFACE
    ${CMAKE_CURRENT_SOURCE_DIR}/ThirdParty
)

# Automatically discover all source files in include/ directory
file(GLOB_RECURSE SERIALIZATION_SOURCES
    "${CMAKE_CURRENT_SOURCE_DIR}/include/*.h"
    "${CMAKE_CURRENT_SOURCE_DIR}/include/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/include/*.cxx"
)

# Exclude Testing directory files from the library sources
list(FILTER SERIALIZATION_SOURCES EXCLUDE REGEX ".*/Testing/.*")

# Create shared library version
add_library(Serialization SHARED
    ${SERIALIZATION_SOURCES}
)

# Set output name for shared library and enable automatic symbol export on Windows
set_target_properties(Serialization PROPERTIES
    OUTPUT_NAME Serialization
    WINDOWS_EXPORT_ALL_SYMBOLS ON
)

# Include directories for Serialization library
target_include_directories(Serialization
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
        ${CMAKE_CURRENT_SOURCE_DIR}/ThirdParty
)



# Link nlohmann/json to Serialization library
target_link_libraries(Serialization
    PUBLIC
        nlohmann_json
)

# Add the Testing/Cxx subdirectory to build test executables
add_subdirectory(include/Testing/Cxx)
