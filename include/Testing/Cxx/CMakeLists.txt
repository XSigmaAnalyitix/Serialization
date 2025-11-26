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
    if(APPLE)

    # Ensure proper C++ standard library linking for Homebrew LLVM
    # Detect if using Homebrew LLVM and link to its libc++ library
    get_filename_component(COMPILER_DIR "${CMAKE_CXX_COMPILER}" DIRECTORY)
    get_filename_component(COMPILER_PARENT "${COMPILER_DIR}" DIRECTORY)
    if(EXISTS "${COMPILER_PARENT}/lib/c++/libc++.dylib")
        # Using Homebrew LLVM - explicitly link to its libc++
        target_link_libraries(${TEST_NAME}
            PRIVATE
                "${COMPILER_PARENT}/lib/c++/libc++.dylib"
                "${COMPILER_PARENT}/lib/c++/libc++abi.dylib"
        )
    elseif(CMAKE_CXX_COMPILER_ID MATCHES "Clang" OR CMAKE_CXX_COMPILER_ID MATCHES "AppleClang")
        # Force linking with the C++ standard library
        target_link_options(${TEST_NAME} PRIVATE -stdlib=libc++)
    endif()
    endif()

    # Enable RTTI (required for dynamic_cast and typeid)
    if(MSVC)
        target_compile_options(${TEST_NAME} PRIVATE /GR)
    else()
        target_compile_options(${TEST_NAME} PRIVATE -frtti)
        # Ensure we use libc++ for both compilation and linking
        if(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
            target_compile_options(${TEST_NAME} PRIVATE -stdlib=libc++)
        endif()
    endif()

    # Set C++ standard and output directory for test executable
    # Place test executables in the same directory as the Serialization DLL
    set_target_properties(${TEST_NAME} PROPERTIES
        CXX_STANDARD 20
        CXX_STANDARD_REQUIRED ON
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/$<CONFIG>"
    )

    # Register the test with CTest
    add_test(
        NAME ${TEST_NAME}
        COMMAND ${TEST_NAME}
        WORKING_DIRECTORY "${CMAKE_BINARY_DIR}/$<CONFIG>"
    )
endforeach()
