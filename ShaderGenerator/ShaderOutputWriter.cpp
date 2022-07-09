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

  std::string ReadAllText(const std::filesystem::path& path)
  {
    FILE* file = nullptr;
    _wfopen_s(&file, path.c_str(), L"rb");
    if (!file) return "";

    fseek(file, 0, SEEK_END);
    auto length = ftell(file);

    std::string buffer(length, '\0');

    fseek(file, 0, SEEK_SET);
    fread_s(buffer.data(), length, length, 1, file);
    fclose(file);

    return buffer;
  }

  bool WriteAllText(const std::filesystem::path& path, const std::string& text)
  {
    FILE* file = nullptr;
    _wfopen_s(&file, path.c_str(), L"wb");
    if (!file) return false;

    fwrite(text.data(), 1, text.size(), file);
    fclose(file);

    return true;
  }

  void WriteHeader(const ShaderCompilationArguments& arguments, const ShaderInfo& shader)
  {
    string namespaceName;
    if (!shader.Namespace.empty()) namespaceName = shader.Namespace;
    else if (!arguments.NamespaceName.empty()) namespaceName = arguments.NamespaceName;
    else namespaceName = "ShaderGenerator";

    static regex namespaceRegex{ "\\." };
    namespaceName = regex_replace(namespaceName, namespaceRegex, "::");

    printf("Generating header for shader group %s at namespace %s...\n", shader.Path.string().c_str(), namespaceName.c_str());
    auto header = shader.GenerateHeader(namespaceName);

    error_code ec;
    filesystem::create_directory(arguments.Header.parent_path(), ec);
    if (ec)
    {
      printf("Failed to create output directory at %s.\n", arguments.Header.parent_path().string().c_str());
    }
    else
    {
      auto existing = ReadAllText(arguments.Header);
      if (existing != header)
      {
        if (WriteAllText(arguments.Header, header))
        {
          printf("Output saved to %s.\n", arguments.Header.string().c_str());
        }
        else
        {
          printf("Failed to save output to %s.\n", arguments.Header.string().c_str());
        }
      }
      else
      {
        printf("Shader header %s is up to date.\n", arguments.Header.string().c_str());
      }
    }
  }
}