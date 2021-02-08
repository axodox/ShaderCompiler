#include "pch.h"
#include "CommandLineParser.h"
#include "ShaderConfiguration.h"
#include "ShaderCompiler.h"
#include "ShaderOutputWriter.h"

using namespace winrt;
using namespace ShaderGenerator;

int main(int argc, char* argv[])
{
  try
  {
    init_apartment();
    
    auto arguments = ShaderCompilationArguments::Parse(argc, argv);
    auto info = ShaderInfo::FromFile(arguments.Input);

    if (!arguments.Header.empty())
    {
      printf("Generating header for shader group: %s", arguments.Input.string().c_str());
      auto header = info.GenerateHeader();
      WriteAllText(arguments.Header, header);
    }

    if (!arguments.Output.empty())
    {
      CompilationOptions options{};
      options.IsDebug = arguments.IsDebug;
      auto output = CompileShader(info, options);

      if (!output.empty())
      {
        WriteShaderOutput(arguments.Output, output);
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
