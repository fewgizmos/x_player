#pragma once

#include "X/Engine/Util/Base/Assert.hpp"

#include <fstream>
#include <string>

namespace Engine::Util::File
{

inline std::string ReadRaw(const std::string& p_Path)
{
    std::ifstream File(p_Path, std::ios::ate | std::ios::binary);

    X_ASSERT(!File.is_open(), "Failed to open file: " + p_Path);

    std::string Content(static_cast<size_t>(File.tellg()), '\0');

    File.seekg(0);
    File.read(Content.data(), static_cast<std::streamsize>(Content.size()));

    return Content;
}

}
