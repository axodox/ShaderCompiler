#pragma once
#include "ShaderCompilationArguments.h"

namespace ShaderGenerator
{
  enum class OptionType
  {
    Boolean,
    Enumeration,
    Integer
  };

  struct OptionPermutation
  {
    std::vector<std::pair<std::string, std::string>> Defines;
    uint64_t Key;
  };

  struct ShaderOption
  {
    std::string Name;
    
    virtual size_t KeyLength() const;

    virtual OptionType Type() const = 0;

    virtual size_t ValueCount() const = 0;

    virtual bool IsValueDefinedExplicitly() const = 0;

    virtual bool TryGetDefinedValue(size_t index, std::string& value) const = 0;

    static std::vector<OptionPermutation> Permutate(const std::vector<std::unique_ptr<ShaderOption>>& options);

    virtual ~ShaderOption() = default;
  };

  struct BooleanOption : public ShaderOption
  {
    virtual OptionType Type() const override;

    virtual size_t ValueCount() const override;

    virtual bool IsValueDefinedExplicitly() const override;

    virtual bool TryGetDefinedValue(size_t index, std::string& value) const override;
  };

  struct EnumerationOption : public ShaderOption
  {
    std::vector<std::string> Values;

    virtual OptionType Type() const override;

    virtual size_t ValueCount() const override;

    virtual bool IsValueDefinedExplicitly() const override;

    virtual bool TryGetDefinedValue(size_t index, std::string& value) const override;
  };

  struct IntegerOption : public ShaderOption
  {
    int Minimum, Maximum;

    virtual OptionType Type() const override;

    virtual size_t ValueCount() const override;

    virtual bool IsValueDefinedExplicitly() const override;

    virtual bool TryGetDefinedValue(size_t index, std::string& value) const override;
  };

  struct ShaderInfo
  {
    std::filesystem::path Path;
    std::vector<std::unique_ptr<ShaderOption>> Options;
    std::string Namespace;
    std::string Target;
    std::string EntryPoint = "main";
    std::vector<std::filesystem::path> Dependencies;
    std::chrono::time_point<std::chrono::system_clock> InputTimestamp;

    static ShaderInfo FromFile(const std::filesystem::path& path);

    std::string GenerateHeader(const std::string& namespaceName = {}) const;
  };
}