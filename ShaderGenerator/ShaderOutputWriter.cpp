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

  struct CompressionBlock
  {
    Buffer Data{ nullptr };
    vector<uint64_t> Components;
  };

  struct CompressionContext
  {
    std::mutex Mutex;
    queue<const CompiledShader*> Shaders;
    list<CompressionBlock> Results;
  };

  DWORD WINAPI CompressWorker(LPVOID contextPointer)
  {
    //Prepare workset
    auto context = static_cast<CompressionContext*>(contextPointer);    
    vector<uint64_t> components;
    InMemoryRandomAccessStream compressedStream;
    Compressor compressor{ compressedStream, CompressAlgorithm::Lzms, 64 * 1024 * 1024 };

    //Add shaders
    const CompiledShader* shader;
    do
    {
      //Get next shader
      shader = nullptr;
      {
        lock_guard lock(context->Mutex);
        if (!context->Shaders.empty())
        {
          shader = context->Shaders.front();
          context->Shaders.pop();
        }
      }
      if (!shader) continue;

      //Serialize shader
      InMemoryRandomAccessStream uncompressedStream;
      {
        DataWriter dataWriter{ uncompressedStream };
        dataWriter.WriteString(L"SH01");
        dataWriter.WriteUInt64(shader->Key);
        dataWriter.WriteUInt32(uint32_t(shader->Data.size()));
        dataWriter.WriteBytes(shader->Data);
        dataWriter.StoreAsync().get();
        dataWriter.FlushAsync().get();
        dataWriter.DetachStream();
      }

      //Copy data into a buffer
      auto contentSize = uint32_t(uncompressedStream.Size());

      Buffer buffer{ contentSize };
      uncompressedStream.Seek(0);
      uncompressedStream.ReadAsync(buffer, contentSize, InputStreamOptions::None).get();

      //Write compressed data
      compressor.WriteAsync(buffer).get();
      components.push_back(shader->Key);
    } while (shader);

    //Finish compression
    compressor.FlushAsync().get();
    compressor.FinishAsync().get();
    compressor.DetachStream();

    //Save results
    auto blockSize = uint32_t(compressedStream.Size());

    CompressionBlock block;
    block.Data = Buffer{ blockSize };
    block.Components = move(components);

    compressedStream.Seek(0);
    compressedStream.ReadAsync(block.Data, blockSize, InputStreamOptions::None).get();

    {
      lock_guard lock(context->Mutex);
      context->Results.push_back(move(block));
    }

    return 0;
  }

  bool WriteShaderBinary(const std::filesystem::path& path, const std::vector<CompiledShader>& compiledShaders)
  {
    bool hasDebugInfo = false;

    try
    {
      //Define compression context
      CompressionContext context;
      for (auto& shader : compiledShaders)
      {
        context.Shaders.push(&shader);
      }

      //Run compression threads
      auto threadCount = thread::hardware_concurrency();
      vector<HANDLE> threads(threadCount);
      for (auto& thread : threads)
      {
        thread = CreateThread(nullptr, 0, &CompressWorker, &context, 0, nullptr);
      }

      WaitForMultipleObjects(DWORD(threads.size()), threads.data(), true, INFINITE);

      //Write output data
      auto properPath = path;
      properPath.make_preferred();
      auto storageFolder = StorageFolder::GetFolderFromPathAsync(properPath.parent_path().c_str()).get();
      auto storageFile = storageFolder.CreateFileAsync(properPath.filename().c_str(), CreationCollisionOption::ReplaceExisting).get();
      auto fileStream = storageFile.OpenAsync(FileAccessMode::ReadWrite).get();
      
      DataWriter dataWriter{ fileStream };
      dataWriter.WriteString(L"CSG2");      
      dataWriter.WriteUInt32(uint32_t(context.Results.size()));

      for (auto& block : context.Results)
      {
        dataWriter.WriteUInt32(uint32_t(block.Components.size()));
        /*for (auto key : block.Components)
        {
          dataWriter.WriteUInt64(key);
        }*/
        dataWriter.WriteBuffer(block.Data);
      }

      dataWriter.StoreAsync().get();
      dataWriter.FlushAsync().get();
      dataWriter.DetachStream();

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