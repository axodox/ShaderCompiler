#pragma once
#include "pch.h"

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
    
    virtual size_t KeyLength() const
    {
      auto range = ValueCount();
      return range > 0 ? (size_t)ceil(log2(float(range))) : 0;
    }

    virtual OptionType Type() const = 0;

    virtual size_t ValueCount() const = 0;

    virtual bool TryGetDefinedValue(size_t index, std::string& value) const = 0;

    static std::vector<OptionPermutation> Permutate(const std::vector<std::unique_ptr<ShaderOption>>& options);
  };

  struct BooleanOption : public ShaderOption
  {
    virtual OptionType Type() const override
    {
      return OptionType::Boolean;
    }

    virtual size_t ValueCount() const override
    {
      return 2;
    }

    virtual bool TryGetDefinedValue(size_t index, std::string& value) const override
    {
      value = "";
      return index == 1;
    }
  };

  struct EnumerationOption : public ShaderOption
  {
    std::vector<std::string> Values;

    virtual OptionType Type() const override
    {
      return OptionType::Enumeration;
    }

    virtual size_t ValueCount() const override
    {
      return Values.size();
    }

    virtual bool TryGetDefinedValue(size_t index, std::string& value) const override
    {
      if (index >= 0 && index < Values.size())
      {
        value = Values.at(index);
        return true;
      }
      else
      {
        value = "";
        return false;
      }
    }
  };

  struct IntegerOption : public ShaderOption
  {
    int Minimum, Maximum;

    virtual OptionType Type() const override
    {
      return OptionType::Integer;
    }

    virtual size_t ValueCount() const override
    {
      return size_t(Maximum - Minimum) + 1;
    }

    virtual bool TryGetDefinedValue(size_t index, std::string& value) const override
    {
      if (index >= 0 && index < ValueCount())
      {
        value = std::to_string(index + Minimum);
        return true;
      }
      else
      {
        value = "";
        return false;
      }
    }
  };

  struct ShaderInfo
  {
    std::filesystem::path Path;
    std::vector<std::unique_ptr<ShaderOption>> Options;
    std::string Target;
    std::string EntryPoint = "main";

    static ShaderInfo FromFile(const std::filesystem::path& path);
  };
}