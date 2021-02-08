#include "pch.h"
#include "CommandLineParser.h"

using namespace std;

namespace ShaderGenerator
{
  ShaderCompilationArguments ShaderCompilationArguments::Parse(int argc, char* argv[])
  {
    regex argRegex("-(\\w+)(?:=(.*))?");

    ShaderCompilationArguments result{};
    for (int index = 0; index < argc; index++)
    {
      auto arg = string(argv[index]);
      smatch match;
      if (regex_match(arg, match, argRegex))
      {
        if (match[1] == "i")
        {
          result.Input = string(match[2]);
        }
        else if(match[1] == "o")
        {
          result.Output = string(match[2]);
          result.Output = result.Output / result.Input.filename().replace_extension(".csg");
        }
        else if (match[1] == "d")
        {
          result.IsDebug = true;
        }
      }

      
    }
    return result;
  }
}