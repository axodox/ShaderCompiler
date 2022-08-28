#pragma once
#include "pch.h"

namespace ShaderGenerator
{
  enum class file_time_kind
  {
    creation,
    access,
    modification
  };

  std::chrono::time_point<std::chrono::system_clock> get_file_time(const std::filesystem::path& filePath, file_time_kind kind);
}