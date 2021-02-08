#pragma once
#include "ShaderConfiguration.h"

namespace ShaderGenerator
{
  struct CompiledShader
  {
    uint64_t Key;
    std::vector<uint8_t> Data;
  };

  struct CompilationOptions
  {
    bool IsDebug = false;
  };

  std::vector<CompiledShader> CompileShader(const ShaderInfo& shader, const CompilationOptions& options = {});
}