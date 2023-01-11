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
    std::vector<uint8_t> ByteCode;
  };

  struct ChunkInfo
  {
    uint32_t ShaderCount = 0ull;
    size_t CompressedOffset = 0ull;
  };

  class CompiledShaderGroup
  {
  private:
    //Bitmask used to obtain chunk key from a shader key
    uint64_t _chunkIndexMask = 0;

    //All compressed chunks
    winrt::Windows::Storage::Streams::InMemoryRandomAccessStream _compressedChunks { nullptr };

    //Info about the compressed chunks 
    std::unordered_map<uint64_t, ChunkInfo> _chunkInfos;

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
    
    static bool IsUwp()
    {
      uint32_t length = 0;
      auto result = GetCurrentPackageFullName(&length, nullptr);
      return result == APPMODEL_ERROR_NO_PACKAGE ? false : true;
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
      result._compressedChunks = InMemoryRandomAccessStream{};

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

          auto fileStream = AwaitResults(storageFile.OpenAsync(FileAccessMode::Read));

          //Copy file to the memorystream
          AwaitOperation(RandomAccessStream::CopyAsync(fileStream, result._compressedChunks));
        }

        result._compressedChunks.Seek(0);

        DataReader dataReader{ result._compressedChunks };

        //Check header
        AwaitOperation(dataReader.LoadAsync(4 + sizeof(uint64_t) + sizeof(uint32_t)));
        auto magic = dataReader.ReadString(4);
        if (magic != L"CSG3")
        {
          throw std::exception("Invalid compiled shader group file header.");
        }

        //Read chunk index mask and block count
        result._chunkIndexMask = dataReader.ReadUInt64();
        auto blockCount = dataReader.ReadUInt32();

        AwaitOperation(dataReader.LoadAsync((2 * sizeof(uint64_t) + sizeof(uint32_t)) * blockCount));

        //Read chunk infos
        for (uint32_t i = 0; i < blockCount; ++i)
        {
          auto key = dataReader.ReadUInt64();

          ChunkInfo info;
          info.ShaderCount = dataReader.ReadUInt32();
          info.CompressedOffset = dataReader.ReadUInt64();

          result._chunkInfos[key] = info;
        }

        dataReader.DetachStream();
      }
      catch (...)
      {
        //Return empty result
        result._compressedChunks = nullptr;
        result._chunkInfos.clear();

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
      using namespace winrt::Windows::Storage::Streams;
      using namespace winrt::Windows::Storage::Compression;

      if (auto shaderIt = _shadersByKey.find(key); shaderIt != _shadersByKey.end())
      {
        //Shader found in cache, return it
        return &shaderIt->second;
      }
      else if (auto chunkIt = _chunkInfos.find(key & _chunkIndexMask); chunkIt != _chunkInfos.end() && _compressedChunks)
      {
        //Clear the cached shaders
        _shadersByKey.clear();

        auto& chunkInfo = chunkIt->second;

        auto startPosition = _compressedChunks.Position();

        //Seek to the start of the compressed chunk
        _compressedChunks.Seek(startPosition + chunkInfo.CompressedOffset);

        InMemoryRandomAccessStream uncompressedChunk;

        //Decompress chunk
        {
          Buffer buffer{ 1024 * 1024 };
          Decompressor decompressor{ _compressedChunks };
          do
          {
            AwaitOperation(decompressor.ReadAsync(buffer, buffer.Capacity(), InputStreamOptions::None));
            AwaitOperation(uncompressedChunk.WriteAsync(buffer));
          } while (buffer.Length() > 0);
          decompressor.DetachStream();
          uncompressedChunk.Seek(0);
        }

        //Parse every shader in the uncompressed chunk
        {
          DataReader dataReader{ uncompressedChunk };
          AwaitOperation(dataReader.LoadAsync(uint32_t(uncompressedChunk.Size())));

          for (uint32_t i=0; i<chunkInfo.ShaderCount; ++i)
          {
            auto magic = dataReader.ReadString(4);
            if (magic != L"SH01")
            {
              throw std::exception("Invalid compiled shader instance header.");
            }

            CompiledShader shader;
            auto shaderKey = dataReader.ReadUInt64();
            shader.Key = shaderKey;

            auto size = dataReader.ReadUInt32();
            shader.ByteCode.resize(size);

            dataReader.ReadBytes(shader.ByteCode);

            _shadersByKey.emplace(shaderKey, std::move(shader));
          }
        }

        //Seek back to the first chunk
        _compressedChunks.Seek(startPosition);

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
  };
}