#pragma once

#include <vulkan/vulkan_core.h>

namespace Engine::Configs::Static
{

inline constexpr int MAX_FPS = 120;

namespace Viewport 
{

inline constexpr int WINDOW_WIDTH  = 1920;
inline constexpr int WINDOW_HEIGHT = 1080;

inline constexpr bool WINDOW_FULLSCREEN = true;
inline constexpr bool WINDOW_RESIZABLE  = false;

inline constexpr bool WINDOW_BORDERED = false;
inline constexpr bool WINDOW_VISIBLE  = true;

inline constexpr bool WINDOW_CURSOR_CAPTURED = false;

}

namespace Graphics
{

inline constexpr unsigned char BACKGROUND_RGB[3] = {13, 17, 23};
inline constexpr const char* SHADERS_DIR = "Resources/Shaders/";

inline constexpr int MAX_FRAMES_IN_FLIGHT = 4;

inline constexpr uint32_t MAX_SAMPLERS = 64;
inline constexpr uint32_t MAX_TEXTURES = 4096;
inline constexpr uint32_t MAX_MATERIALS = 4096;

inline constexpr VkFormat DEPTH_FORMAT = VK_FORMAT_D32_SFLOAT;
inline constexpr VkFormat HDR_FORMAT   = VK_FORMAT_R16G16B16A16_SFLOAT;

inline constexpr VkFormat GBUFFER_NORMAL_FORMAT   = VK_FORMAT_R16G16B16A16_SFLOAT;
inline constexpr VkFormat GBUFFER_VELOCITY_FORMAT = VK_FORMAT_R16G16_SFLOAT;

inline constexpr VkFormat GTAO_FORMAT = VK_FORMAT_R32_SFLOAT;

}

namespace Tests
{

inline constexpr bool TRIANGLE_RENDERER_ENABLED = true;
inline constexpr bool TEXTURE_RENDERER_ENABLED = true;

}

}
