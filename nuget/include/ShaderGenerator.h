#pragma once
#include <vector>
#include <unordered_map>
#include <filesystem>
#include <string>
#include <mutex>
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
    uint64_t Key = 0ull;
    uint32_t Size = 0u;
    std::vector<uint8_t> ByteCode;
  };

  class CompiledShaderGroup
  {
#pragma region Helper types
    struct ShaderBlockInfo
    {
      uint64_t CompressedOffset = 0ull;
      uint32_t ShaderCount = 0u;
    };

    struct ShaderBlock
    {
      uint64_t Key;
      std::unordered_map<uint64_t, uint64_t> ShaderOffsets;
      winrt::Windows::Storage::Streams::InMemoryRandomAccessStream Block;
    };
#pragma endregion

#pragma region Helper methods
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
    static void ReadValue(winrt::Windows::Storage::Streams::DataReader& reader, T& value)
    {
      static_assert(std::is_trivially_copyable_v<T>);
      AwaitOperation(reader.LoadAsync(sizeof(T)));
      reader.ReadBytes({ reinterpret_cast<uint8_t*>(&value), sizeof(T) });
    }

    template<typename T>
    static auto ReadValue(winrt::Windows::Storage::Streams::DataReader& reader)
    {
      T value{};
      ReadValue(reader, value);
      return value;
    }

    static void ReadVector(winrt::Windows::Storage::Streams::DataReader& reader, std::vector<uint8_t>& value)
    {
      AwaitOperation(reader.LoadAsync(uint32_t(value.size())));
      reader.ReadBytes(value);
    }

    static auto ReadString(winrt::Windows::Storage::Streams::DataReader& reader, uint32_t length)
    {
      AwaitOperation(reader.LoadAsync(length));
      return reader.ReadString(length);
    }

    static bool IsUwp()
    {
      uint32_t length = 0;
      auto result = GetCurrentPackageFullName(&length, nullptr);
      return result == APPMODEL_ERROR_NO_PACKAGE ? false : true;
    }
#pragma endregion

  private:
    //Bitmask used to obtain block key from a shader key
    uint64_t _blockKeyMask = 0ull;

    //The start position of the first block
    uint64_t _blockOffset = 0ull;

    //The backing shader stream
    winrt::Windows::Storage::Streams::IRandomAccessStream _shaderStream{ nullptr };

    //Info about the shader blocks 
    std::unordered_map<uint64_t, ShaderBlockInfo> _shaderBlocks;

    //The active shader block
    std::optional<ShaderBlock> _activeBlock;

    //Shader cache
    std::unordered_map<uint64_t, CompiledShader> _shaderCache;

    //Mutex for shader management - mutex cannot be moved
    std::unique_ptr<std::mutex> _mutex = std::make_unique<std::mutex>();

    CompiledShaderGroup() = default;
    CompiledShaderGroup(CompiledShaderGroup&&) = default;
    CompiledShaderGroup& operator=(CompiledShaderGroup&&) = default;

    static CompiledShader ReadShader(winrt::Windows::Storage::Streams::DataReader& reader, bool headerOnly = false)
    {
      auto magic = ReadString(reader, 4);
      if (magic != L"SH01")
      {
        throw std::exception("Invalid compiled shader instance header.");
      }

      CompiledShader shader;
      ReadValue(reader, shader.Key);
      ReadValue(reader, shader.Size);

      if (!headerOnly)
      {
        shader.ByteCode.resize(shader.Size);
        ReadVector(reader, shader.ByteCode);
      }

      return shader;
    }

    void ActivateBlock(uint64_t blockKey)
    {
      using namespace winrt::Windows::Storage::Streams;
      using namespace winrt::Windows::Storage::Compression;

      //Maybe the block is already loaded
      if (_activeBlock && _activeBlock->Key == blockKey) return;

      //Locate block info
      auto& blockInfo = _shaderBlocks.at(blockKey);

      //Seek to the start of the compressed block
      _shaderStream.Seek(_blockOffset + blockInfo.CompressedOffset);

      ShaderBlock uncompressedBlock;
      uncompressedBlock.Key = blockKey;

      //Decompress block
      {
        Buffer buffer{ 1024 * 1024 };
        Decompressor decompressor{ _shaderStream };
        do
        {
          AwaitOperation(decompressor.ReadAsync(buffer, buffer.Capacity(), InputStreamOptions::None));
          AwaitOperation(uncompressedBlock.Block.WriteAsync(buffer));
        } while (buffer.Length() > 0);
        decompressor.DetachStream();
        uncompressedBlock.Block.Seek(0);
      }

      //Load shader offsets
      {
        DataReader dataReader{ uncompressedBlock.Block };
        dataReader.ByteOrder(ByteOrder::LittleEndian);

        for (uint32_t i = 0; i < blockInfo.ShaderCount; ++i)
        {
          auto shaderStart = uncompressedBlock.Block.Position();
          CompiledShader shader = ReadShader(dataReader, true);

          uncompressedBlock.ShaderOffsets[shader.Key] = shaderStart;
          uncompressedBlock.Block.Seek(uncompressedBlock.Block.Position() + shader.Size);
        }

        dataReader.DetachStream();
      }

      //Replace the cached uncompressed block
      _activeBlock = std::move(uncompressedBlock);
    }

    CompiledShader LoadShader(uint64_t key)
    {
      using namespace winrt::Windows::Storage::Streams;

      //Active the appropriate block
      auto blockKey = key & _blockKeyMask;
      ActivateBlock(blockKey);

      //Load the shader
      auto shaderOffset = _activeBlock->ShaderOffsets.at(key);
      _activeBlock->Block.Seek(shaderOffset);

      DataReader dataReader{ _activeBlock->Block };
      dataReader.ByteOrder(ByteOrder::LittleEndian);
      auto result = ReadShader(dataReader);
      dataReader.DetachStream();

      //Return the result
      return result;
    }

  public:
    CompiledShaderGroup(std::vector<CompiledShader>&& shaders)
    {
      for (auto& shader : shaders)
      {
        _shaderCache[shader.Key] = std::move(shader);
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
        //Open file
        {
          auto preferredPath = path;
          preferredPath.make_preferred();

          StorageFile storageFile{ nullptr };
          if (IsUwp())
          {
            Uri uri{ L"ms-appx:///" + preferredPath };
            storageFile = AwaitResults(StorageFile::GetFileFromApplicationUriAsync(uri));
          }
          else
          {
            storageFile = AwaitResults(StorageFile::GetFileFromPathAsync(preferredPath.c_str()));
          }

          result._shaderStream = AwaitResults(storageFile.OpenAsync(FileAccessMode::Read));
        }

        //Parse file
        {
          DataReader dataReader{ result._shaderStream };
          dataReader.ByteOrder(ByteOrder::LittleEndian);

          //Check header
          auto magic = ReadString(dataReader, 4);
          if (magic != L"CSG3")
          {
            throw std::exception("Invalid compiled shader group file header.");
          }

          //Read block index mask and block count
          ReadValue(dataReader, result._blockKeyMask);
          auto blockCount = ReadValue<uint32_t>(dataReader);

          //Read block infos
          result._shaderBlocks.reserve(blockCount);
          for (uint32_t i = 0; i < blockCount; ++i)
          {
            auto key = ReadValue<uint64_t>(dataReader);

            ShaderBlockInfo info;
            ReadValue(dataReader, info.CompressedOffset);
            ReadValue(dataReader, info.ShaderCount);

            result._shaderBlocks[key] = info;
          }

          dataReader.DetachStream();
        }

        result._blockOffset = result._shaderStream.Position();
      }
      catch (...)
      {
        //Return empty result
        result._shaderStream = nullptr;
        result._shaderBlocks.clear();

        throw std::exception("Failed to open compiled shader group file.");
      }

      return result;
    }

    const std::unordered_map<uint64_t, CompiledShader>& Shaders() const
    {
      return _shaderCache;
    }

    const CompiledShader* Shader(uint64_t key)
    {
      try
      {
        std::lock_guard lock(*_mutex);

        auto& shader = _shaderCache[key];
        if (!shader.Size)
        {
          shader = LoadShader(key);
        }

        return &shader;
      }
      catch (...)
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
      _shaderCache.clear();
      _activeBlock.reset();
    }
  };
}