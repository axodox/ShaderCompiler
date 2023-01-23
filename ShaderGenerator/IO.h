#pragma once
#include "pch.h"

namespace ShaderGenerator
{
  std::string ReadAllText(const std::filesystem::path& path);
  bool WriteAllText(const std::filesystem::path& path, const std::string& text);

  bool WriteAllBytes(const std::filesystem::path& path, const std::vector<uint8_t>& bytes);
}