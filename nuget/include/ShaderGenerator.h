#pragma once
#include <vector>
#include <unordered_map>
#include <filesystem>
#include <fstream>
#include <string>

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

    template<typename T>
    static void read(std::ifstream& stream, T& value)
    {
      static_assert(std::is_trivially_copyable<T>::value);
      stream.read((char*)&value, sizeof(T));
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
      std::vector<CompiledShader> shaders;
      std::ifstream file(path, std::ios::binary);

      if (file.good())
      {
        std::string magic{ "    " };
        file.read(magic.data(), magic.length());

        if (magic != "CSG1")
        {
          throw std::exception("Invalid compiled shader group file header.");
        }

        uint32_t shaderCount;
        read(file, shaderCount);

        shaders.resize(shaderCount);
        for (auto& shader : shaders)
        {
          read(file, shader.Key);

          uint32_t size;
          read(file, size);

          shader.ByteCode.resize(size);
          file.read((char*)shader.ByteCode.data(), shader.ByteCode.size());
        }
      }
      else
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