cmake_minimum_required(VERSION 3.20)
project(P3DG-ENVIRONMENT LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(WIN32 AND NOT MSVC)
    message(FATAL_ERROR
        "This project requires the MSVC compiler on Windows.\n"
        "Install Visual Studio 2022 (or Build Tools 2022) with the\n"
        "\"Desktop development with C++\" workload, set it as\n"
        "your Launch/Debug target: \n"
        "ctrl+shft+p CMake: Select a Kit\n"
        " --> Visual Studio Professional 2022 Release - amd64\n"
        "then reconfigure.")
endif()

if(CMAKE_SIZEOF_VOID_P EQUAL 4)
    message(FATAL_ERROR
        "32-bit build detected. The Vulkan SDK only ships 64-bit libraries.\n"
        "Delete the build directory and reconfigure with:\n"
        "  cmake -B build -G \"Visual Studio 17 2022\" -A x64")
endif()

find_package(Vulkan REQUIRED)

if(NOT Vulkan_GLSLC_EXECUTABLE)
    message(FATAL_ERROR
        "glslc not found. It ships with the LunarG Vulkan SDK.\n"
        "Reinstall the SDK, or make sure the VULKAN_SDK environment\n"
        "variable points at your SDK installation, then reconfigure.")
endif()

include(FetchContent)
FetchContent_Declare(glfw
    GIT_REPOSITORY https://github.com/glfw/glfw.git
    GIT_TAG 3.4)
set(GLFW_BUILD_DOCS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(glfw)

function(compile_shaders TARGET)
    if(NOT ARGN)
        return()
    endif()

    set(spv_outputs "")

    foreach(shader IN LISTS ARGN)
        get_filename_component(shader_absolute "${shader}" ABSOLUTE)
        get_filename_component(shader_filename "${shader}" NAME)

        set(spv_output "${CMAKE_CURRENT_BINARY_DIR}/shaders/${shader_filename}.spv")

        add_custom_command(
            OUTPUT "${spv_output}"
            COMMAND "${CMAKE_COMMAND}" -E make_directory
            "${CMAKE_CURRENT_BINARY_DIR}/shaders"
            COMMAND "${Vulkan_GLSLC_EXECUTABLE}"
            --target-env=vulkan1.3
            $<IF:$<CONFIG:Debug>,-g,-O>
            "${shader_absolute}"
            -o "${spv_output}"
            DEPENDS "${shader_absolute}"
            COMMENT "Compiling shader ${shader_filename}"
            VERBATIM)

        list(APPEND spv_outputs "${spv_output}")
    endforeach()

    add_custom_target(${TARGET}_shaders DEPENDS ${spv_outputs})
    add_dependencies(${TARGET} ${TARGET}_shaders)

    add_custom_command(TARGET ${TARGET} POST_BUILD
        COMMAND "${CMAKE_COMMAND}" -E make_directory
        "$<TARGET_FILE_DIR:${TARGET}>/shaders"
        COMMAND "${CMAKE_COMMAND}" -E copy_if_different
        ${spv_outputs}
        "$<TARGET_FILE_DIR:${TARGET}>/shaders"
        COMMENT "Copying shaders to output directory"
        VERBATIM)

    target_sources(${TARGET} PRIVATE ${ARGN})
    set_source_files_properties(${ARGN} PROPERTIES HEADER_FILE_ONLY TRUE)
    source_group("Shaders" FILES ${ARGN})
endfunction()

add_executable(P3DG-ENVIRONMENT src/main.cpp)
target_link_libraries(P3DG-ENVIRONMENT PRIVATE Vulkan::Vulkan glfw)

target_compile_definitions(P3DG-ENVIRONMENT PRIVATE 
    SHADER_DIR="$<TARGET_FILE_DIR:P3DG-ENVIRONMENT>/shaders"
)

file(GLOB SHADER_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/shaders/*.comp"
    "${CMAKE_CURRENT_SOURCE_DIR}/shaders/*.vert"
    "${CMAKE_CURRENT_SOURCE_DIR}/shaders/*.frag"
)

compile_shaders(P3DG-ENVIRONMENT ${SHADER_SOURCES})