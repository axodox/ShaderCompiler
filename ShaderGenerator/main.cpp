﻿#include "pch.h"
#include "ShaderCompilationArguments.h"
#include "ShaderConfiguration.h"
#include "ShaderCompiler.h"
#include "ShaderOutputWriter.h"
#include "FileAttributes.h"

using namespace std;
using namespace winrt;
using namespace ShaderGenerator;

int main(int argc, char* argv[])
{
  if (argc == 0)
  {
    printf("Shader Generator\n");
    printf("©Péter Major 2020\n");
    printf("\n");

    printf("Usage:\n");
    printf("  -i=<file_path>: Path of the source code\n");
    printf("  -o=<dir_path>: Path of the output directory\n");
    printf("  -h=<dir_path>: Path of the include header\n");
    printf("  -n=<namespace>: Header namespace name\n");
    printf("  -p=0..4: Optimization level\n");
    printf("  -d: Emit debug symbols\n");
    printf("  -x: Strip debug symbols to separate files\n");
    printf("  -t: Test mode - waits for debugger\n");
    printf("\n");

    printf("Source file usage:\n");
    printf("  #pragma target cs_5_0 //Compilation target\n");
    printf("  #pragma entry main //Entry point - optional, default is 'main'\n");
    printf("  #pragma namespace MyApp::Shaders //Namespace for include header\n");
    printf("  #pragma option bool IsSomethingEnabled //A boolean option\n");
    printf("  #pragma option enum RenderMode {X, Y, Z} //An enum option\n");
    printf("  #pragma option uint SampleCount {1..4} //An integer option\n");
    return 0;
  }

  try
  {
    init_apartment();

    auto arguments = ShaderCompilationArguments::Parse(argc, argv);
    if (arguments.WaitForDebugger)
    {
      while (!IsDebuggerPresent())
      {
        Sleep(1000);
      }

      DebugBreak();
    }

    auto shader = ShaderInfo::FromFile(arguments.Input);

    if (!arguments.Header.empty())
    {
      auto skip = false;
      if (filesystem::exists(arguments.Header))
      {
        auto headerTime = get_file_time(arguments.Header, file_time_kind::modification);
        skip = headerTime > shader.InputTimestamp;
      }

      if (!skip) WriteHeader(arguments, shader);
    }

    if (!arguments.Output.empty())
    {
      auto skip = false;
      if (filesystem::exists(arguments.Output))
      {
        auto shaderTime = get_file_time(arguments.Output, file_time_kind::modification);
        skip = shaderTime > shader.InputTimestamp;
      }

      if (!skip)
      {
        auto output = CompileShader(shader, arguments);

        if (!output.empty())
        {
          WriteShaderOutput(arguments.Output, output, shader);
        }
      }
    }
    return 0;
  }
  catch (const std::exception& error)
  {
    printf("Shader group compilation failed: %s\n", error.what());
    return -1;
  }
  catch (...)
  {
    printf("Shader group compilation failed: unknown error.\n");
    return -1;
  }
}
