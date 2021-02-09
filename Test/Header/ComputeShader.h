#pragma once
#include "pch.h"

namespace MyApp::Shaders
{
  enum class ComputeShaderFlags : uint64_t
  {
    Default = 0,
    IS_X = 1,
    ENval1 = 0,
    ENval2 = 2,
    ENval3 = 4,
    Y1 = 0,
    Y2 = 8,
    Y3 = 16,
    Y4 = 24,
    Y5 = 32,
    Y6 = 40,
    Y7 = 48,
    Y8 = 56,
    Y9 = 64,
    Y10 = 72,
    Y11 = 80,
  };
}
