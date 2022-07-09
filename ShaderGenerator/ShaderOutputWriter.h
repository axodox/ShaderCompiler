#pragma once
#include "ShaderCompiler.h"

namespace ShaderGenerator
{
  void WriteShaderOutput(const std::filesystem::path& path, const std::vector<CompiledShader>& data);

  void WriteHeader(const ShaderCompilationArguments& path, const ShaderInfo& shader);
}