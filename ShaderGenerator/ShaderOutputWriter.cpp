#include "pch.h"
#include "ShaderOutputWriter.h"

using namespace std;

namespace ShaderGenerator
{
  template<typename T>
  void WriteValue(ofstream& stream, const T& value)
  {
    static_assert(is_trivially_copyable<T>::value);
    stream.write((char*)&value, sizeof(T));
  }

  bool WriteAllBytes(const std::filesystem::path& path, const std::vector<uint8_t>& bytes)
  {
    ofstream stream(path, ios::out | ios::binary);
    
    if (stream.good())
    {
      stream.write((const char*)bytes.data(), bytes.size());
    }

    return stream.good();
  }

  void WriteShaderOutput(const std::filesystem::path& path, const std::vector<CompiledShader>& compiledShaders)
  {
    auto root = path.parent_path();

    //Ensure output directory
    error_code ec;
    filesystem::create_directory(root, ec);

    //Write shaders
    auto hasDebugInfo = false;
    if (ec)
    {
      wprintf(L"Failed to create output directory at %s.\n", root.c_str());
    }
    else
    {
      wprintf(L"Writing output shaders to %s...", root.c_str());

      ofstream stream(path, ios::out | ios::binary);
      if (stream.good())
      {
        stream << "CSG1";
        WriteValue(stream, uint32_t(compiledShaders.size()));
        for (auto& shader : compiledShaders)
        {
          WriteValue(stream, shader.Key);
          WriteValue(stream, uint32_t(shader.Data.size()));
          stream.write((const char*)shader.Data.data(), shader.Data.size());

          hasDebugInfo |= !shader.PdbName.empty();
        }
        wprintf(L"Output saved to %s.\n", path.c_str());
      }
      else
      {
        printf("Failed to save output to %s.\n", path.string().c_str());
      }
    }

    //Write debug info
    if (hasDebugInfo)
    {
      auto pdbRoot = path.parent_path() / "ShaderPdb";

      //Ensure output directory
      filesystem::create_directory(pdbRoot, ec);

      for (auto& shader : compiledShaders)
      {
        WriteAllBytes(pdbRoot / shader.PdbName, shader.PdbData);
      }
    }
  }
}