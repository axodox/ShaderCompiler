#pragma once
#include <vector>
#include <unordered_map>
#include <filesystem>
#include <fstream>
#include <string>
#include <winrt/base.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.ApplicationModel.h>
#include <winrt/Windows.Storage.h>
#include <winrt/Windows.Storage.Streams.h>
#include <winrt/Windows.Storage.Compression.h>
#include <appmodel.h>

namespace ShaderGenerator
{
  struct CompiledShader
  {
    uint64_t Key;
    uint32_t Size;
    std::vector<uint8_t> ByteCode;
  };

  struct BlockInfo
  {
    uint32_t ShaderCount = 0u;
    size_t CompressedOffset = 0ull;
  };

  struct UncompressedBlock
  {
    uint64_t Index;
    std::unordered_map<uint64_t, uint64_t> ShaderOffsets;
    winrt::Windows::Storage::Streams::InMemoryRandomAccessStream Block;
  };

  class CompiledShaderGroup
  {
  private:
    //Bitmask used to obtain block key from a shader key
    uint64_t _blockIndexMask = 0;

    //All compressed blocks
    winrt::Windows::Storage::Streams::IRandomAccessStream _compressedBlocks { nullptr };

    //Info about the compressed blocks 
    std::unordered_map<uint64_t, BlockInfo> _blockInfos;

    //Last uncompressed block
    std::optional<UncompressedBlock> _uncompressedBlock;

    //Cache for the uncompressed shaders
    std::unordered_map<uint64_t, CompiledShader> _shadersByKey;

    CompiledShaderGroup() = default;

    template <typename TAsync>
    static void AwaitOperation(TAsync const& async)
    {
      auto event = winrt::handle(CreateEvent(nullptr, true, false, nullptr));
      async.Completed([&](auto&&...)
        {
          SetEvent(event.get());
        });

      WaitForSingleObject(event.get(), INFINITE);
    }

    template <typename TAsync>
    static auto AwaitResults(TAsync const& async)
    {
      AwaitOperation(async);
      return async.GetResults();
    }

    template<typename T>
    static T read_internal(winrt::Windows::Storage::Streams::DataReader& reader);

    template<>
    static uint32_t read_internal<uint32_t>(winrt::Windows::Storage::Streams::DataReader& reader)
    {
      return reader.ReadUInt32();
    }

    template<>
    static uint64_t read_internal<uint64_t>(winrt::Windows::Storage::Streams::DataReader& reader)
    {
      return reader.ReadUInt64();
    }

    template<typename T>
    static void read(winrt::Windows::Storage::Streams::DataReader& reader, T& value)
    {
      static_assert(std::is_trivially_copyable_v<T>);
      AwaitOperation(reader.LoadAsync(sizeof(T)));
      value = read_internal<T>(reader);
    }

    static void read(winrt::Windows::Storage::Streams::DataReader& reader, std::vector<uint8_t>& value)
    {
      AwaitOperation(reader.LoadAsync(uint32_t(value.size())));
      reader.ReadBytes(value);
    }

    template<typename T>
    static auto read(winrt::Windows::Storage::Streams::DataReader& reader)
    {
      T value{};
      read(reader, value);
      return value;
    }

    static auto read_string(winrt::Windows::Storage::Streams::DataReader& reader, uint32_t length)
    {
      AwaitOperation(reader.LoadAsync(length));
      return reader.ReadString(length);
    }

    static CompiledShader read_shader(winrt::Windows::Storage::Streams::DataReader& reader, bool partial = false)
    {
      auto magic = read_string(reader, 4);
      if (magic != L"SH01")
      {
        throw std::exception("Invalid compiled shader instance header.");
      }

      CompiledShader shader;
      read(reader, shader.Key);
      read(reader, shader.Size);

      if (!partial)
      {
        shader.ByteCode.resize(shader.Size);
        read(reader, shader.ByteCode);
      }

      return shader;
    }
    
    static bool IsUwp()
    {
      uint32_t length = 0;
      auto result = GetCurrentPackageFullName(&length, nullptr);
      return result == APPMODEL_ERROR_NO_PACKAGE ? false : true;
    }

    const CompiledShader* LoadFromUncompressedBlock(uint64_t key)
    {
      using namespace winrt::Windows::Storage::Streams;

      auto it = _uncompressedBlock->ShaderOffsets.find(key);
      if (it != _uncompressedBlock->ShaderOffsets.end())
      {
        auto shaderPosition = it->second;
        _uncompressedBlock->Block.Seek(shaderPosition);

        DataReader dataReader{ _uncompressedBlock->Block };

        auto shader = read_shader(dataReader);

        dataReader.DetachStream();

        auto [shaderIt, _] = _shadersByKey.emplace(key, std::move(shader));
        return &shaderIt->second;
      }
      else
      {
        //Internal error, should not happen
        throw std::exception("Shader not present.");
      }
    }

    const CompiledShader* LoadFromCompressedBlock(uint64_t blockIndex, const BlockInfo& blockInfo, uint64_t key)
    {
      using namespace winrt::Windows::Storage::Streams;
      using namespace winrt::Windows::Storage::Compression;

      auto startPosition = _compressedBlocks.Position();

      //Seek to the start of the compressed block
      _compressedBlocks.Seek(startPosition + blockInfo.CompressedOffset);

      UncompressedBlock uncompressedBlock;
      uncompressedBlock.Index = blockIndex;

      //Decompress block
      {
        Buffer buffer{ 1024 * 1024 };
        Decompressor decompressor{ _compressedBlocks };
        do
        {
          AwaitOperation(decompressor.ReadAsync(buffer, buffer.Capacity(), InputStreamOptions::None));
          AwaitOperation(uncompressedBlock.Block.WriteAsync(buffer));
        } while (buffer.Length() > 0);
        decompressor.DetachStream();
        uncompressedBlock.Block.Seek(0);
      }

      //Set up shader offsets
      {
        DataReader dataReader{ uncompressedBlock.Block };

        for (uint32_t i = 0; i < blockInfo.ShaderCount; ++i)
        {
          auto shaderStart = uncompressedBlock.Block.Position();
          CompiledShader shader = read_shader(dataReader, true);

          if (shader.Key == key)
          {
            shader.ByteCode.resize(shader.Size);
            read(dataReader, shader.ByteCode);
            _shadersByKey.emplace(key, std::move(shader));
          }
          else
          {
            uncompressedBlock.ShaderOffsets[shader.Key] = shaderStart;
            uncompressedBlock.Block.Seek(uncompressedBlock.Block.Position() + shader.Size);
          }
        }

        dataReader.DetachStream();
      }

      //Seek back to the first block
      _compressedBlocks.Seek(startPosition);

      //Replace the cached uncompressed block
      _uncompressedBlock = std::move(uncompressedBlock);

      //Return the uncompressed shader
      auto it = _shadersByKey.find(key);
      if (it != _shadersByKey.end())
      {
        return &it->second;
      }
      else
      {
        //Internal error, should not happen
        throw std::exception("Shader not present.");
      }
    }

  public:
    CompiledShaderGroup(std::vector<CompiledShader>&& shaders)
    {
      for (auto& shader : shaders)
      {
        _shadersByKey[shader.Key] = std::move(shader);
      }
    }

    static CompiledShaderGroup FromFile(const std::filesystem::path& path)
    {
      using namespace winrt;
      using namespace winrt::Windows::ApplicationModel;
      using namespace winrt::Windows::Foundation;
      using namespace winrt::Windows::Storage;
      using namespace winrt::Windows::Storage::Streams;

      CompiledShaderGroup result;

      try
      {
        {
          //Open file
          auto preferredPath = path;
          preferredPath.make_preferred();

          StorageFile storageFile{nullptr};
          if (IsUwp())
          {
            Uri uri{ L"ms-appx:///" + preferredPath };
            storageFile = AwaitResults(StorageFile::GetFileFromApplicationUriAsync(uri));
          }
          else
          {
            storageFile = AwaitResults(StorageFile::GetFileFromPathAsync(preferredPath.c_str()));
          }

          result._compressedBlocks = AwaitResults(storageFile.OpenAsync(FileAccessMode::Read));
        }

        DataReader dataReader{ result._compressedBlocks };

        //Check header
        auto magic = read_string(dataReader, 4);
        if (magic != L"CSG3")
        {
          throw std::exception("Invalid compiled shader group file header.");
        }

        //Read block index mask and block count
        read(dataReader, result._blockIndexMask);
        auto blockCount = read<uint32_t>(dataReader);
       
        //Read block infos
        for (uint32_t i = 0; i < blockCount; ++i)
        {
          auto key = read<uint64_t>(dataReader);

          BlockInfo info;
          read(dataReader, info.ShaderCount);
          read(dataReader, info.CompressedOffset);

          result._blockInfos[key] = info;
        }

        dataReader.DetachStream();
      }
      catch (...)
      {
        //Return empty result
        result._compressedBlocks = nullptr;
        result._blockInfos.clear();

        throw std::exception("Failed to open compiled shader group file.");
      }

      return result;
    }

    const std::unordered_map<uint64_t, CompiledShader>& Shaders() const
    {
      return _shadersByKey;
    }

    const CompiledShader* Shader(uint64_t key)
    {
      auto blockIndex = key & _blockIndexMask;

      if (auto shaderIt = _shadersByKey.find(key); shaderIt != _shadersByKey.end())
      {
        //Shader found in cache, return it
        return &shaderIt->second;
      }
      else if (_uncompressedBlock && _uncompressedBlock->Index == blockIndex)
      {
        return LoadFromUncompressedBlock(key);
      }
      else if (auto blockIt = _blockInfos.find(blockIndex); blockIt != _blockInfos.end() && _compressedBlocks)
      {
        return LoadFromCompressedBlock(blockIndex, blockIt->second, key);
      }
      else
      {
        return nullptr;
      }
    }

    template<typename T>
    const CompiledShader* Shader(T key)
    {
      return Shader(uint64_t(key));
    }

    void ClearCache()
    {
      _shadersByKey.clear();
      _uncompressedBlock.reset();
    }
  };
}