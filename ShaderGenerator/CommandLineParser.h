#pragma once
#include "pch.h"

namespace ShaderGenerator
{
  struct ShaderCompilationArguments
  {
    std::filesystem::path Input, Output, Header;
    bool IsDebug;

    static ShaderCompilationArguments Parse(int argc, char* argv[]);
  };
}