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

  template<typename T>
  struct Chunk
  {
    const T* Begin;
    const T* End;

    Chunk(const T* begin, size_t count) : Begin(begin), End(begin + count)
    { }

    size_t Size() const
    {
      return End - Begin;
    }
  };

  struct ShaderChunkingInfo
  {
    inline static const size_t MaxChunkSize = 128;

    size_t ChunkCount = 1ull;
    size_t ChunkSize = 0ull;
    size_t ChunkIndexOffset = 0ull;
    size_t ChunkIndexMask = 0ull;

    ShaderChunkingInfo(const ShaderInfo& info, size_t shaderVariationCount)
    {
      if (shaderVariationCount < MaxChunkSize)
      {
        ChunkSize = shaderVariationCount;
        return;
      }

      //Find the first N options that divide the variations into chunks that are smaller than MaxChunkSize
      for (auto& option : info.Options)
      {
        ChunkCount *= option->ValueCount();
        ChunkSize = shaderVariationCount / ChunkCount;
        ChunkIndexOffset += option->KeyLength();
        if (ChunkSize <= MaxChunkSize)
        {
          //Construct the index mask: first ChunkIndexOffset number of bits are 1s
          ChunkIndexMask = (uint32_t)(((uint64_t)1 << ChunkIndexOffset) - 1);
          break;
        }
      }
    }
  };

  struct CompressionBlock
  {
    uint64_t Key;
    Buffer Data{ nullptr };
    vector<uint64_t> Components;
  };

  CompressionBlock CompressWorker(const Chunk<CompiledShader>& shaderChunk, const ShaderChunkingInfo& chunking)
  {
    CompressionBlock block;
    block.Key = shaderChunk.Begin->Key & chunking.ChunkIndexMask;
    block.Components.reserve(shaderChunk.Size());

    InMemoryRandomAccessStream compressedStream;
    Compressor compressor{ compressedStream, CompressAlgorithm::Lzms, 64 * 1024 * 1024 };

    //Add shaders
    for_each(shaderChunk.Begin, shaderChunk.End, [&](const CompiledShader& shader)
    {
      //Serialize shader
      InMemoryRandomAccessStream uncompressedStream;
      {
        DataWriter dataWriter{ uncompressedStream };
        dataWriter.WriteString(L"SH01");
        dataWriter.WriteUInt64(shader.Key);
        dataWriter.WriteUInt32(uint32_t(shader.Data.size()));
        dataWriter.WriteBytes(shader.Data);
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
      block.Components.push_back(shader.Key);
    });

    //Finish compression
    compressor.FlushAsync().get();
    compressor.FinishAsync().get();
    compressor.DetachStream();

    //Save results
    auto blockSize = uint32_t(compressedStream.Size());
    block.Data = Buffer{ blockSize };

    compressedStream.Seek(0);
    compressedStream.ReadAsync(block.Data, blockSize, InputStreamOptions::None).get();

    return block;
  }

  bool WriteShaderBinary(const std::filesystem::path& path, const std::vector<CompiledShader>& compiledShaders, const ShaderInfo& shader)
  {
    bool hasDebugInfo = false;

    try
    {
      ShaderChunkingInfo chunkingInfo{ shader, compiledShaders.size() };

      //Define compression input
      vector<Chunk<CompiledShader>> input;
      input.reserve(chunkingInfo.ChunkCount);
      for (size_t i = 0; i < compiledShaders.size(); )
      {
        Chunk<CompiledShader> chunk{ &compiledShaders[i], min(chunkingInfo.ChunkSize, compiledShaders.size() - i) };
        input.push_back(chunk);
        i += chunkingInfo.ChunkSize;
      }

      vector<CompressionBlock> output(input.size());

      //Run compression threads
      transform(execution::par_unseq, input.begin(), input.end(), output.begin(), 
        [&](const Chunk<CompiledShader>& shaderChunk) 
        { 
          return CompressWorker(shaderChunk, chunkingInfo); 
        }
      );

      //Write output data
      auto properPath = path;
      properPath.make_preferred();
      auto storageFolder = StorageFolder::GetFolderFromPathAsync(properPath.parent_path().c_str()).get();
      auto storageFile = storageFolder.CreateFileAsync(properPath.filename().c_str(), CreationCollisionOption::ReplaceExisting).get();
      auto fileStream = storageFile.OpenAsync(FileAccessMode::ReadWrite).get();
      
      DataWriter dataWriter{ fileStream };
      dataWriter.WriteString(L"CSG3");
      dataWriter.WriteUInt64(chunkingInfo.ChunkIndexMask);
      dataWriter.WriteUInt32(uint32_t(output.size()));

      size_t compressedOffset = 0;
      for (auto& block : output)
      {
        dataWriter.WriteUInt64(block.Key);
        dataWriter.WriteUInt32(uint32_t(block.Components.size()));
        dataWriter.WriteUInt64(compressedOffset);
        compressedOffset += block.Data.Length();
      }

      for (auto& block : output)
      {
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

  void WriteShaderOutput(const std::filesystem::path& path, const std::vector<CompiledShader>& compiledShaders, const ShaderInfo& shader)
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

      hasDebugInfo = WriteShaderBinary(path, compiledShaders, shader);
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