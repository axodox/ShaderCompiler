#pragma once
#include "pch.h"

namespace ShaderGenerator
{
  struct ShaderCompilationArguments
  {
    std::filesystem::path Input, Output, Header;
    bool IsDebug = false;
    int OptimizationLevel = 2;
    std::string NamespaceName;
    bool WaitForDebugger = false;

    static ShaderCompilationArguments Parse(int argc, char* argv[]);
  };
}