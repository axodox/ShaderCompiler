#include "pch.h"
#include "ShaderOutputWriter.h"

using namespace winrt;
using namespace winrt::Windows::Storage;
using namespace winrt::Windows::Storage::Compression;
using namespace winrt::Windows::Storage::Streams;
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

  bool WriteShaderBinary(const std::filesystem::path& path, const std::vector<CompiledShader>& compiledShaders)
  {
    bool hasDebugInfo = false;

    try
    {
      //Serialize into memory
      InMemoryRandomAccessStream memoryStream;

      {
        DataWriter dataWriter{ memoryStream };

        dataWriter.WriteUInt32(uint32_t(compiledShaders.size()));
        for (auto& shader : compiledShaders)
        {
          dataWriter.WriteUInt64(shader.Key);
          dataWriter.WriteUInt32(uint32_t(shader.Data.size()));
          dataWriter.WriteBytes(shader.Data);
          hasDebugInfo |= !shader.PdbName.empty();
        }

        dataWriter.FlushAsync().get();
        dataWriter.StoreAsync().get();
        dataWriter.DetachStream();
      }

      //Copy data into a buffer
      auto contentSize = uint32_t(memoryStream.Size());

      Buffer buffer{ contentSize };
      memoryStream.Seek(0);
      memoryStream.ReadAsync(buffer, contentSize, InputStreamOptions::None).get();

      //Write output data
      auto properPath = path;
      properPath.make_preferred();
      auto storageFolder = StorageFolder::GetFolderFromPathAsync(properPath.parent_path().c_str()).get();
      auto storageFile = storageFolder.CreateFileAsync(properPath.filename().c_str(), CreationCollisionOption::ReplaceExisting).get();
      auto fileStream = storageFile.OpenAsync(FileAccessMode::ReadWrite).get();

      {
        //Write header
        DataWriter dataWriter{ fileStream };
        dataWriter.WriteString(L"CSG2");
        dataWriter.StoreAsync().get();
        dataWriter.DetachStream();
      }

      {
        //Write compressed data
        Compressor compressor{ fileStream, CompressAlgorithm::Mszip, 0 };
        compressor.WriteAsync(buffer).get();
        compressor.FinishAsync().get();
        compressor.DetachStream();
      }

      //Close file
      fileStream.FlushAsync().get();
      fileStream.Close();

      wprintf(L"Output saved to %s.\n", path.c_str());
    }
    catch (hresult_error& error)
    {
      wprintf(L"Failed to save output to %s. Reason: %s\n", path.c_str(), error.message().c_str());
    }

    return hasDebugInfo;
  }

  void WriteDebugDatabase(const std::filesystem::path& path, const std::vector<CompiledShader>& compiledShaders)
  {
    auto pdbRoot = path.parent_path() / "ShaderPdb";

    //Ensure output directory
    error_code ec;
    filesystem::create_directory(pdbRoot, ec);

    if (ec)
    {
      wprintf(L"Failed to create PDB directory at %s.\n", pdbRoot.c_str());
    }
    else
    {
      wprintf(L"Writing PDBs to %s...\n", pdbRoot.c_str());

      for (auto& shader : compiledShaders)
      {
        if (WriteAllBytes(pdbRoot / shader.PdbName, shader.PdbData))
        {
          wprintf(L"PDB saved to %s.\n", path.c_str());
        }
        else
        {
          wprintf(L"Failed to save PDB to %s.\n", path.c_str());
        }
      }
    }
  }

  void WriteShaderOutput(const std::filesystem::path& path, const std::vector<CompiledShader>& compiledShaders)
  {
    auto root = path.parent_path();

    //Ensure output directory
    error_code ec;
    filesystem::create_directory(root, ec);

    //Write shader binary
    auto hasDebugInfo = false;
    if (ec)
    {
      wprintf(L"Failed to create output directory at %s.\n", root.c_str());
    }
    else
    {
      wprintf(L"Writing output shaders to %s...\n", root.c_str());

      hasDebugInfo = WriteShaderBinary(path, compiledShaders);
    }

    //Write debug database
    if (hasDebugInfo)
    {
      WriteDebugDatabase(path, compiledShaders);
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