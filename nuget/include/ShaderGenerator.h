#pragma once
#include <vector>
#include <unordered_map>
#include <filesystem>
#include <sstream>
#include <fstream>
#include <string>
#include <mutex>
#include <winrt/base.h>
#include <compressapi.h>

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
      uint64_t CompressedLength = 0ull;
      uint32_t ShaderCount = 0u;
    };

    struct ShaderBlock
    {
      uint64_t Key;
      std::unordered_map<uint64_t, uint64_t> ShaderOffsets;
      std::stringstream Block;
    };

    struct decompressor_handle_traits
    {
      using type = DECOMPRESSOR_HANDLE;

      static void close(type value) noexcept
      {
        CloseDecompressor(value);
      }

      static type invalid() noexcept
      {
        return reinterpret_cast<type>(-1);
      }
    };
#pragma endregion

#pragma region Helper methods
    template<typename T>
    static void ReadValue(std::istream& stream, T& value)
    {
      static_assert(std::is_trivially_copyable_v<T>);
      stream.read(reinterpret_cast<char*>(&value), sizeof(T));
    }

    template<typename T>
    static auto ReadValue(std::istream& stream)
    {
      T value{};
      ReadValue(stream, value);
      return value;
    }

    static void ReadVector(std::istream& stream, std::vector<uint8_t>& value)
    {
      stream.read(reinterpret_cast<char*>(value.data()), value.size());
    }

    static auto ReadString(std::istream& stream, uint32_t length)
    {
      std::string buffer(length, '\0');
      stream.read(buffer.data(), buffer.size());
      return winrt::to_hstring(buffer);
    }
#pragma endregion

  private:
    //Bitmask used to obtain block key from a shader key
    uint64_t _blockKeyMask = 0ull;

    //The start position of the first block
    uint64_t _blockOffset = 0ull;

    //The backing shader stream
    std::ifstream _shaderStream;

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

    static CompiledShader ReadShader(std::istream& reader, bool headerOnly = false)
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
      //Maybe the block is already loaded
      if (_activeBlock && _activeBlock->Key == blockKey) return;

      //Locate block info
      auto& blockInfo = _shaderBlocks.at(blockKey);

      //Seek to the start of the compressed block
      _shaderStream.seekg(_blockOffset + blockInfo.CompressedOffset);

      ShaderBlock uncompressedBlock;
      uncompressedBlock.Key = blockKey;

      //Decompress block
      {
        //Read the compressed data from the file
        std::string compressedBuffer(size_t(blockInfo.CompressedLength), '\0');
        _shaderStream.read(compressedBuffer.data(), compressedBuffer.size());

        //Create decompressor
        winrt::handle_type<decompressor_handle_traits> decompressor;
        winrt::check_bool(CreateDecompressor(COMPRESS_ALGORITHM_LZMS, nullptr, decompressor.put()));

        //Get the decompressed length
        SIZE_T decompressedLength = 0;
        if (!Decompress(decompressor.get(), compressedBuffer.data(), compressedBuffer.size(), nullptr, 0, &decompressedLength) 
          && GetLastError() != ERROR_INSUFFICIENT_BUFFER)
        {
          throw std::exception("Invalid compressed buffer.");
        }

        //Decompress the data
        std::string decompressedBuffer(decompressedLength, '\0');
        winrt::check_bool(Decompress(decompressor.get(), compressedBuffer.data(), compressedBuffer.size(), decompressedBuffer.data(), decompressedBuffer.size(), &decompressedLength));

        //Store it in the decompressed stream
        uncompressedBlock.Block.write(decompressedBuffer.data(), decompressedLength);
        uncompressedBlock.Block.seekg(0);
      }

      //Load shader offsets
      {
        for (uint32_t i = 0; i < blockInfo.ShaderCount; ++i)
        {
          auto shaderStart = uncompressedBlock.Block.tellg();
          CompiledShader shader = ReadShader(uncompressedBlock.Block, true);

          uncompressedBlock.ShaderOffsets[shader.Key] = shaderStart;
          uncompressedBlock.Block.seekg(std::streamoff(shader.Size), std::ios_base::cur);
        }
      }

      //Replace the cached uncompressed block
      _activeBlock = std::move(uncompressedBlock);
    }

    CompiledShader LoadShader(uint64_t key)
    {
      //Active the appropriate block
      auto blockKey = key & _blockKeyMask;
      ActivateBlock(blockKey);

      //Load the shader
      auto shaderOffset = _activeBlock->ShaderOffsets.at(key);
      _activeBlock->Block.seekg(shaderOffset);

      auto result = ReadShader(_activeBlock->Block);

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
      CompiledShaderGroup result;

      try
      {
        //Open file
        {
          auto preferredPath = path;
          preferredPath.make_preferred();

          std::ifstream stream(preferredPath.string().c_str(), std::ios::binary);
          if (!stream.is_open()) throw std::runtime_error("Failed to open shader group file!");

          //Enough to check eofbit and badbit, the latter signals read/write errors.
          //Since we only read raw bytes and we don't use the >> operator, logical errors can't occur, so no need to check failbit.
          stream.exceptions(std::ios_base::eofbit | std::ios_base::badbit);

          result._shaderStream = move(stream);
        }

        //Parse file
        {
          auto& stream = result._shaderStream;

          //Check header
          auto magic = ReadString(stream, 4);
          if (magic != L"CSG3")
          {
            throw std::runtime_error("Invalid compiled shader group file header.");
          }

          //Read block index mask and block count
          ReadValue(stream, result._blockKeyMask);
          auto blockCount = ReadValue<uint32_t>(stream);

          //Read block infos
          result._shaderBlocks.reserve(blockCount);

          ShaderBlockInfo* previousBlock = nullptr;
          for (uint32_t i = 0; i < blockCount; ++i)
          {
            auto key = ReadValue<uint64_t>(stream);
            auto& currentBlock = result._shaderBlocks[key];
            ReadValue(stream, currentBlock.CompressedOffset);
            ReadValue(stream, currentBlock.ShaderCount);

            if (previousBlock) previousBlock->CompressedLength = currentBlock.CompressedOffset - previousBlock->CompressedOffset;
            previousBlock = &currentBlock;
          }

          result._blockOffset = stream.tellg();

          stream.seekg(0, std::ios_base::end);
          if (previousBlock) previousBlock->CompressedLength = stream.tellg() - std::streamoff(result._blockOffset + previousBlock->CompressedOffset);
        }

      }
      catch (...)
      {
        throw std::runtime_error("Failed to open compiled shader group file.");
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