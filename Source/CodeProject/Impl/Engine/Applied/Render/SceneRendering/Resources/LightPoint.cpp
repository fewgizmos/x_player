#include "X/Engine/Applied/Render/SceneRendering/Resources/LightPoint.hpp"

#include <utility>

namespace Engine::Applied::Render::SceneRendering::Resources
{

void LightPoint::LoadHdri(std::string p_Path)
{
    using TextureInfo = ::Engine::Applied::Render::Resources::Textures::TextureCreateInfo;

    Hdri = ::Engine::Util::Applied::ResourceHandle<Texture>(
        TextureInfo{std::move(p_Path), VK_FORMAT_R16G16B16A16_SFLOAT}
    );
}

}
