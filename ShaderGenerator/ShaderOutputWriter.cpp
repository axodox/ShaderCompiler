#include "pch.h"
#include "ShaderOutputWriter.h"

using namespace std;

namespace ShaderGenerator
{
  template<typename T>
  void write(ofstream& stream, const T& value)
  {
    static_assert(is_trivially_copyable<T>::value);
    stream.write((char*)&value, sizeof(T));
  }

  void WriteShaderOutput(const std::filesystem::path& path, const std::vector<CompiledShader>& compiledShaders)
  {
    error_code ec;
    filesystem::create_directory(path.parent_path(), ec);
    if (ec)
    {
      printf("Failed to create output directory at %s.\n", path.parent_path().string().c_str());
    }
    else
    {
      ofstream stream(path, ios::out | ios::binary);
      if (stream.good())
      {
        stream << "CSG1";
        write(stream, uint32_t(compiledShaders.size()));
        for (auto& shader : compiledShaders)
        {
          write(stream, shader.Key);
          write(stream, uint32_t(shader.Data.size()));
          stream.write((const char*)shader.Data.data(), shader.Data.size());
        }
        printf("Output saved to %s.\n", path.string().c_str());
      }
      else
      {
        printf("Failed to save output to %s.\n", path.string().c_str());
      }
    }
  }
}