add_subdirectory(CodeThirdParty)

configure_file(
    ${CMAKE_CURRENT_SOURCE_DIR}/CodeProject/Head/X/Project/Configs/Dynamic.hpp.in
    ${CMAKE_CURRENT_SOURCE_DIR}/CodeProject/Head/X/Project/Configs/PreCompiled/Dynamic.hpp
    @ONLY
)

file(GLOB_RECURSE XP_SOURCES CONFIGURE_DEPENDS
    ${CMAKE_CURRENT_SOURCE_DIR}/CodeProject/Impl/*.cpp
)

file(GLOB_RECURSE XP_HEADERS CONFIGURE_DEPENDS
    ${CMAKE_CURRENT_SOURCE_DIR}/CodeProject/Head/*.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/CodeProject/Head/*.h
)

find_package(Threads REQUIRED)

if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    find_library(X_PNG_STATIC_LIBRARY NAMES libpng.a libpng16.a REQUIRED)
    find_library(X_ZLIB_STATIC_LIBRARY NAMES libz.a REQUIRED)

    set(PNG_LIBRARY_RELEASE "${X_PNG_STATIC_LIBRARY}")
    set(PNG_LIBRARY_DEBUG   "${X_PNG_STATIC_LIBRARY}")

    set(ZLIB_LIBRARY_RELEASE "${X_ZLIB_STATIC_LIBRARY}")
    set(ZLIB_LIBRARY_DEBUG   "${X_ZLIB_STATIC_LIBRARY}")

    find_package(ZLIB REQUIRED MODULE)
    find_package(PNG REQUIRED MODULE)
else()
    find_package(PNG REQUIRED)
endif()

add_executable(XProjectExecutable
    ${XP_SOURCES}
    ${XP_HEADERS}
)

target_include_directories(XProjectExecutable PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/CodeProject/Head
)

target_link_libraries(XProjectExecutable PRIVATE
    SDL3::SDL3-static
    Vulkan::Vulkan
    glm::glm
    fastgltf::fastgltf
    imgui
    shaderc
    stb
    GPUOpen::VulkanMemoryAllocator
    vkbootstrap
    Threads::Threads
    PNG::PNG
    ${CMAKE_DL_LIBS}
)

if(CMAKE_SYSTEM_NAME STREQUAL "Linux" AND CMAKE_CXX_COMPILER_ID MATCHES "^(GNU|Clang)$")
    target_link_options(XProjectExecutable PRIVATE -static-libstdc++ -static-libgcc)
endif()
