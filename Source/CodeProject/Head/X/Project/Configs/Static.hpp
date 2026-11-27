#pragma once

#include <vulkan/vulkan_core.h>

#include <fastgltf/core.hpp>

#include <string>

namespace Project::Configs::Static
{

inline constexpr int TARGET_FPS = 144;

namespace ViewportStartup 
{

inline constexpr int WINDOW_WIDTH  = 1650;
inline constexpr int WINDOW_HEIGHT = 1000;

inline constexpr bool WINDOW_FULLSCREEN = false;
inline constexpr bool WINDOW_RESIZABLE  = true;

inline constexpr bool WINDOW_BORDERED = true;
inline constexpr bool WINDOW_VISIBLE  = true;

inline constexpr bool WINDOW_CURSOR_CAPTURED = false;

}

namespace Contents
{

inline const std::string BASE_CONTENT_DIR = "raw_contents/";

inline const std::string SHADERS_DIR     = BASE_CONTENT_DIR + "Shader/";
inline const std::string GLTF_MODELS_DIR = BASE_CONTENT_DIR + "3D/";

}


namespace Graphics
{

inline constexpr unsigned char BACKGROUND_RGB[3] = {0x0d, 0x11, 0x17};

inline constexpr int MAX_FRAMES_IN_FLIGHT = 4;
inline constexpr bool VSYNC               = true;

inline constexpr uint32_t MAX_SAMPLERS  = 64;
inline constexpr uint32_t MAX_TEXTURES  = 4096;
inline constexpr uint32_t MAX_MATERIALS = 4096;

inline constexpr VkFormat DEPTH_FORMAT = VK_FORMAT_D32_SFLOAT;
inline constexpr VkFormat HDR_FORMAT   = VK_FORMAT_R16G16B16A16_SFLOAT;

inline constexpr VkFormat SCENE_COLOR_FORMAT      = VK_FORMAT_R8G8B8A8_SRGB;
inline constexpr VkFormat GBUFFER_ALBEDO_FORMAT   = VK_FORMAT_R8G8B8A8_SRGB;
inline constexpr VkFormat GBUFFER_NORMAL_FORMAT   = VK_FORMAT_R16G16B16A16_SFLOAT;
inline constexpr VkFormat GBUFFER_VELOCITY_FORMAT = VK_FORMAT_R16G16_SFLOAT;

inline constexpr VkFormat GTAO_FORMAT = VK_FORMAT_R32_SFLOAT;

inline constexpr fastgltf::Extensions PARSER_EXTENSIONS =
      fastgltf::Extensions::KHR_texture_transform
    | fastgltf::Extensions::KHR_mesh_quantization
    | fastgltf::Extensions::KHR_lights_punctual
    | fastgltf::Extensions::KHR_materials_specular
    | fastgltf::Extensions::KHR_materials_ior
    | fastgltf::Extensions::KHR_materials_iridescence
    | fastgltf::Extensions::KHR_materials_volume
    | fastgltf::Extensions::KHR_materials_transmission
    | fastgltf::Extensions::KHR_materials_clearcoat
    | fastgltf::Extensions::KHR_materials_emissive_strength
    | fastgltf::Extensions::KHR_materials_sheen
    | fastgltf::Extensions::KHR_materials_unlit
    | fastgltf::Extensions::KHR_materials_anisotropy
    | fastgltf::Extensions::KHR_materials_diffuse_transmission
    | fastgltf::Extensions::EXT_mesh_gpu_instancing
    | fastgltf::Extensions::MSFT_packing_normalRoughnessMetallic;

}

namespace Scene
{

inline constexpr float MetersPerUnit = 1.0f;
inline constexpr float UnitsPerMeter = 1.0f / MetersPerUnit;

}

namespace ResourceLoader
{

inline constexpr int BATCH_COUNT = Graphics::MAX_FRAMES_IN_FLIGHT;

inline constexpr VkDeviceSize BUFFER_STAGING_BLOCK_SIZE = 64  * 1024 * 1024;
inline constexpr VkDeviceSize IMAGE_STAGING_BLOCK_SIZE  = 128 * 1024 * 1024;

inline constexpr VkDeviceSize GPU_UPLOAD_BYTES_PER_FRAME = 32 * 1024 * 1024;

inline constexpr uint32_t GPU_COPY_COMMANDS_PER_FRAME      = 128;
inline constexpr uint32_t GPU_RESOURCE_CREATES_PER_FRAME   = 16;
inline constexpr uint32_t GPU_RESOURCE_DESTROYS_PER_FRAME  = 32;
inline constexpr uint32_t GPU_DESCRIPTOR_UPDATES_PER_FRAME = 256;

inline constexpr int GPU_OPS_MAX_AGING_BOOST = 900;

}

}
