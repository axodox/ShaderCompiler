#pragma once
#include "ShaderConfiguration.h"
#include "ShaderCompilationArguments.h"

namespace ShaderGenerator
{
  struct CompiledShader
  {
    uint64_t Key;
    std::vector<uint8_t> Data;
  };

  std::vector<CompiledShader> CompileShader(const ShaderInfo& shader, const ShaderCompilationArguments& options = {});
}