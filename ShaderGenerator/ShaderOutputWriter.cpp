#include "pch.h"
#include "ShaderOutputWriter.h"
#include "IO.h"
#include "Parallel.h"

using namespace winrt;
using namespace winrt::Windows::Storage;
using namespace winrt::Windows::Storage::Compression;
using namespace winrt::Windows::Storage::Streams;
using namespace std;
using namespace std::filesystem;

namespace ShaderGenerator
{
  struct ShaderBlockLayout
  {
    inline static const size_t MaxBlockSize = 64;

    size_t BlockCount = 1ull;
    size_t BlockSize = 0ull;
    size_t BlockIndexOffset = 0ull;
    uint64_t BlockIndexMask = 0ull;

    ShaderBlockLayout(const ShaderInfo& info, size_t shaderVariationCount)
    {
      if (shaderVariationCount <= MaxBlockSize)
      {
        BlockSize = shaderVariationCount;
      }
      else
      {
        //Find the first N options that divide the variations into blocks that are smaller than MaxBlockSize
        for (auto& option : info.Options)
        {
          BlockCount *= option->ValueCount();
          BlockSize = shaderVariationCount / BlockCount;
          BlockIndexOffset += option->KeyLength();
          if (BlockSize <= MaxBlockSize)
          {
            //Construct the index mask: first BlockIndexOffset number of bits are 1s
            BlockIndexMask = (1ull << BlockIndexOffset) - 1;
            break;
          }
        }
      }
    }
  };

  struct CompressionBlock
  {
    uint64_t Key;
    vector<uint64_t> Components;
    Buffer Data{ nullptr };
  };

  CompressionBlock CreateShaderBlock(const array_view<const CompiledShader>& shaders, const ShaderBlockLayout& layout)
  {
    CompressionBlock block;
    block.Key = shaders.begin()->Key & layout.BlockIndexMask;
    block.Components.reserve(shaders.size());

    InMemoryRandomAccessStream compressedStream;
    Compressor compressor{ compressedStream, CompressAlgorithm::Lzms, 64 * 1024 * 1024 };

    //Add shaders
    for (auto& shader : shaders)
    {
      //Serialize shader
      InMemoryRandomAccessStream uncompressedStream;
      {
        DataWriter dataWriter{ uncompressedStream };
        dataWriter.ByteOrder(ByteOrder::LittleEndian);
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
    }

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

  void WriteShaderBinary(std::filesystem::path path, const std::vector<CompiledShader>& compiledShaders, const ShaderInfo& shaderInfo)
  {
    try
    {
      wprintf(L"Writing output shaders to %s...\n", path.c_str());

      //Ensure output directory
      auto root = path.parent_path();

      error_code ec;
      filesystem::create_directory(root, ec);
      if (ec) throw runtime_error("Failed to create output directory.");

      //Define block layout
      ShaderBlockLayout blockLayout{ shaderInfo, compiledShaders.size() };
      wprintf(L"Layout: %zu block(s), %zu shader variants in each block.\n", blockLayout.BlockCount, blockLayout.BlockSize);

      //Organize compiled shaders into blocks
      vector<array_view<const CompiledShader>> input;
      input.reserve(blockLayout.BlockCount);
      for (size_t i = 0; i < compiledShaders.size(); )
      {
        array_view<const CompiledShader> block(&compiledShaders[i], uint32_t(blockLayout.BlockSize));
        input.push_back(block);
        i += block.size();
      }

      //Run compression threads
      vector<CompressionBlock> output = parallel_map<array_view<const CompiledShader>, CompressionBlock, vector<array_view<const CompiledShader>>>(input,
        [&](const auto& shaderBlock)
        {
          return CreateShaderBlock(shaderBlock, blockLayout);
        }
      );

      //Write output data
      path.make_preferred();
      auto storageFolder = StorageFolder::GetFolderFromPathAsync(path.parent_path().c_str()).get();
      auto storageFile = storageFolder.CreateFileAsync(path.filename().c_str(), CreationCollisionOption::ReplaceExisting).get();
      auto fileStream = storageFile.OpenAsync(FileAccessMode::ReadWrite).get();

      DataWriter dataWriter{ fileStream };
      dataWriter.ByteOrder(ByteOrder::LittleEndian);
      dataWriter.WriteString(L"CSG3");
      dataWriter.WriteUInt64(blockLayout.BlockIndexMask);
      dataWriter.WriteUInt32(uint32_t(output.size()));

      size_t compressedOffset = 0;
      for (auto& block : output)
      {
        dataWriter.WriteUInt64(block.Key);
        dataWriter.WriteUInt64(compressedOffset);
        dataWriter.WriteUInt32(uint32_t(block.Components.size()));
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
    catch (const hresult_error& error)
    {
      wprintf(L"Failed to save output to %s. Reason: %s\n", path.c_str(), error.message().c_str());
    }
    catch (const exception& error)
    {
      printf("Failed to save output to %s. Reason: %s\n", path.string().c_str(), error.what());
    }
    catch (...)
    {
      wprintf(L"Failed to save output to %s. An unknown error has been encountered.\n", path.c_str());
    }
  }

  void WriteDebugDatabase(const std::filesystem::path& path, const std::vector<CompiledShader>& compiledShaders)
  {
    //Check if PDB data is available
    auto hasPdb = any_of(compiledShaders.begin(), compiledShaders.end(), [](const CompiledShader& shader) { return !shader.PdbName.empty() && !shader.PdbData.empty(); });
    if (!hasPdb) return;

    //Ensure output directory
    auto root = path.parent_path() / "ShaderPdb";

    error_code ec;
    filesystem::create_directory(root, ec);

    if (ec)
    {
      wprintf(L"Failed to create PDB directory at %s.\n", root.c_str());
      return;
    }

    //Write PDB files
    wprintf(L"Writing PDBs to %s...\n", root.c_str());

    for (auto& shader : compiledShaders)
    {
      if (shader.PdbName.empty() || shader.PdbData.empty()) continue;

      if (WriteAllBytes(root / shader.PdbName, shader.PdbData))
      {
        wprintf(L"PDB saved to %s.\n", path.c_str());
      }
      else
      {
        wprintf(L"Failed to save PDB to %s.\n", path.c_str());
      }
    }
  }

  void WriteShaderOutput(const std::filesystem::path& path, const std::vector<CompiledShader>& compiledShaders, const ShaderInfo& shader)
  {
    WriteShaderBinary(path, compiledShaders, shader);
    WriteDebugDatabase(path, compiledShaders);
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