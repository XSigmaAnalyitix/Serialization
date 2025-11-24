# Automatically discover all test files in this directory
file(GLOB TEST_FILES
    "${CMAKE_CURRENT_SOURCE_DIR}/*.cxx"
)

# Create a separate executable target for each test file
foreach(TEST_FILE ${TEST_FILES})
    # Get the filename without path and extension
    get_filename_component(TEST_NAME ${TEST_FILE} NAME_WE)

    # Create executable for this test
    add_executable(${TEST_NAME} ${TEST_FILE})

    # Link against the Serialization library
    # This automatically provides the include directories from the Serialization target
    target_link_libraries(${TEST_NAME}
        PRIVATE
            Serialization
    )

    # Enable RTTI (required for dynamic_cast and typeid)
    if(MSVC)
        target_compile_options(${TEST_NAME} PRIVATE /GR)
    else()
        target_compile_options(${TEST_NAME} PRIVATE -frtti)
    endif()

    # Set C++ standard and output directory for test executable
    # Place test executables in the same directory as the Serialization DLL
    set_target_properties(${TEST_NAME} PROPERTIES
        CXX_STANDARD 20
        CXX_STANDARD_REQUIRED ON
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/$<CONFIG>"
    )
endforeach()
