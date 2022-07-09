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

  class CompiledShaderGroup
  {
  private:
    std::vector<CompiledShader> _shaders;
    std::unordered_map<uint64_t, CompiledShader*> _shadersByKey;

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
      _shaders = std::move(shaders);
      for (auto& shader : _shaders)
      {
        _shadersByKey[shader.Key] = &shader;
      }
    }

    static CompiledShaderGroup FromFile(const std::filesystem::path& path)
    {
      using namespace winrt;
      using namespace winrt::Windows::ApplicationModel;
      using namespace winrt::Windows::Foundation;
      using namespace winrt::Windows::Storage;
      using namespace winrt::Windows::Storage::Compression;
      using namespace winrt::Windows::Storage::Streams;

      std::vector<CompiledShader> shaders;

      try
      {
        //Read shader data
        InMemoryRandomAccessStream memoryStream;
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

          //Check header
          {
            DataReader dataReader{ fileStream };
            AwaitOperation(dataReader.LoadAsync(4));
            auto magic = dataReader.ReadString(4);
            if (magic != L"CSG2")
            {
              throw std::exception("Invalid compiled shader group file header.");
            }
            dataReader.DetachStream();
          }

          //Decompress data
          {
            Decompressor decompressor{ fileStream };
            Buffer buffer{ 1024 * 1024 };

            do
            {
              AwaitOperation(decompressor.ReadAsync(buffer, buffer.Capacity(), InputStreamOptions::None));
              AwaitOperation(memoryStream.WriteAsync(buffer));
            } while (buffer.Length() > 0);

            decompressor.DetachStream();
            memoryStream.Seek(0);
          }

          fileStream.Close();
        }

        //Parse data
        {
          DataReader dataReader{ memoryStream };
          AwaitOperation(dataReader.LoadAsync(uint32_t(memoryStream.Size())));

          auto shaderCount = dataReader.ReadUInt32();

          shaders.resize(shaderCount);
          for (auto& shader : shaders)
          {
            shader.Key = dataReader.ReadUInt64();

            auto size = dataReader.ReadUInt32();
            shader.ByteCode.resize(size);

            dataReader.ReadBytes(shader.ByteCode);
          }
        }
      }
      catch (...)
      {
        throw std::exception("Failed to open compiled shader group file.");
      }

      return CompiledShaderGroup(std::move(shaders));
    }

    const std::vector<CompiledShader>& Shaders() const
    {
      return _shaders;
    }

    const CompiledShader* Shader(uint64_t key) const
    {
      auto it = _shadersByKey.find(key);
      if (it != _shadersByKey.end())
      {
        return it->second;
      }
      else
      {
        return nullptr;
      }
    }

    template<typename T>
    const CompiledShader* Shader(T key) const
    {
      return Shader(uint64_t(key));
    }
  };
}