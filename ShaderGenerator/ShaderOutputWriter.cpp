#include "pch.h"
#include "ShaderOutputWriter.h"

using namespace std;

namespace ShaderGenerator
{
  void WriteShaderOutput(const std::filesystem::path& path, const std::vector<CompiledShader>& compiledShaders)
  {
    ofstream stream(path, ios::out | ios::binary);
    stream << "CompiledShaderGroup";
    stream << uint32_t(compiledShaders.size());
    for (auto& shader : compiledShaders)
    {
      stream << shader.Key;
      stream.write((const char*)shader.Data.data(), shader.Data.size());
    }
    printf("Output saved to %s.\n", path.string().c_str());
  }
}