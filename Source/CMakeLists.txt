add_subdirectory(CodeThirdParty)

configure_file(
    ${CMAKE_CURRENT_SOURCE_DIR}/CodeProject/Head/X/Configs/Dynamic.hpp.in
    ${CMAKE_CURRENT_SOURCE_DIR}/CodeProject/Head/X/Configs/PreCompiled/Dynamic.hpp
    @ONLY
)

file(GLOB_RECURSE XP_SOURCES CONFIGURE_DEPENDS
    ${CMAKE_CURRENT_SOURCE_DIR}/CodeProject/Impl/*.cpp
)

file(GLOB_RECURSE XP_HEADERS CONFIGURE_DEPENDS
    ${CMAKE_CURRENT_SOURCE_DIR}/CodeProject/Head/*.hpp
)

find_package(Threads REQUIRED)

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
    ${CMAKE_DL_LIBS}
)
