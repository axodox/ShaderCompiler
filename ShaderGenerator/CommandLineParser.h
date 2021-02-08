#pragma once
#include "pch.h"

namespace ShaderGenerator
{
  struct ShaderCompilationArguments
  {
    std::filesystem::path Input, Output;
    bool IsDebug;

    static ShaderCompilationArguments Parse(int argc, char* argv[]);
  };
}