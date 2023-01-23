#include "pch.h"
#include "ShaderConfiguration.h"
#include "FileAttributes.h"

using namespace std;

namespace ShaderGenerator
{
  std::unique_ptr<ShaderOption> ParseOption(const std::string& text)
  {
    static regex boolRegex("bool\\s+(\\w*)\\s*");
    static regex enumRegex("enum\\s+(\\w*)\\s+\\{\\s*((\\w+\\s*,\\s*)*\\w+)\\s*\\}\\s*");
    static regex uintRegex("u?int\\s+(\\w*)\\s+\\{\\s*(\\d+)\\s*\\.\\.\\s*(\\d+)\\s*\\}\\s*");

    smatch match;
    if (regex_match(text, match, boolRegex))
    {
      auto result = make_unique<BooleanOption>();
      result->Name = match[1];
      return result;
    }
    else if (regex_match(text, match, enumRegex))
    {
      auto result = make_unique<EnumerationOption>();
      result->Name = match[1];

      static regex enumValueRegex("\\w+");
      smatch valueMatch;

      auto valuesText = match[2].str();
      while (regex_search(valuesText, valueMatch, enumValueRegex))
      {
        result->Values.push_back(valueMatch[0]);
        valuesText = valueMatch.suffix().str();
      }

      if (result->Values.size() == 0)
      {
        throw exception("Enum options must have at least one value!");
      }

      return result;
    }
    else if (regex_match(text, match, uintRegex))
    {
      auto result = make_unique<IntegerOption>();
      result->Name = match[1];
      result->Minimum = stoi(match[2]);
      result->Maximum = stoi(match[3]);

      if (result->Minimum > result->Maximum)
      {
        throw exception("Integer option maximum must be greater than minimum!");
      }

      return result;
    }

    return nullptr;
  }

  unordered_set<filesystem::path> GetDependencies(const std::filesystem::path& path)
  {
    static regex includeRegex("#include\\s+\"([^\"]*)\"");

    queue<filesystem::path> dependenciesToCheck;
    dependenciesToCheck.push(path);

    unordered_set<filesystem::path> dependencies;
    dependencies.emplace(path.lexically_normal());

    auto parentPath = path.parent_path();
    while (!dependenciesToCheck.empty())
    {
      ifstream file(dependenciesToCheck.front());
      if (!file.good())
      {
        throw std::exception(("Failed to open file " + path.string()).c_str());
      }

      string line;
      while (getline(file, line))
      {
        smatch match;
        if (regex_match(line, match, includeRegex))
        {
          auto includePath = (parentPath / filesystem::path(match[1].str())).lexically_normal();
          if (dependencies.emplace(includePath).second)
          {
            dependenciesToCheck.push(includePath);
          }
        }
      }

      dependenciesToCheck.pop();
    }

    return dependencies;
  }

  ShaderInfo ShaderInfo::FromFile(const std::filesystem::path& path)
  {
    ShaderInfo result{};
    result.Path = path;

    auto dependencies = GetDependencies(path);
    result.Dependencies = { dependencies.begin(), dependencies.end() };

    result.InputTimestamp = {};
    for (auto& dependency : result.Dependencies)
    {
      result.InputTimestamp = max(result.InputTimestamp, get_file_time(dependency, file_time_kind::modification));
    }

    static regex optionRegex("#pragma\\s+(target|namespace|entry|option)\\s+(.*)");
    ifstream file(path);
    if (!file.good())
    {
      throw std::exception(("Failed to open file " + path.string()).c_str());
    }

    string line;
    while (getline(file, line))
    {
      smatch match;
      if (regex_match(line, match, optionRegex))
      {
        if (match[1] == "target")
        {
          result.Target = match[2];
        }
        else if (match[1] == "namespace")
        {
          result.Namespace = match[2];
        }
        else if (match[1] == "entry")
        {
          result.EntryPoint = match[2];
        }
        else if (match[1] == "option")
        {
          auto option = ParseOption(match[2]);
          if (option) result.Options.push_back(move(option));
        }
      }
    }

    return result;
  }

  std::string ShaderInfo::GenerateHeader(const std::string& namespaceName) const
  {
    stringstream text;
    text << "#pragma once\n";
    text << "\n";
    text << "namespace " << namespaceName.c_str() << "\n";
    text << "{\n";
    text << "  enum class " << Path.filename().replace_extension().string().c_str() << "Flags : unsigned long long\n";
    text << "  {\n";
    text << "    Default = 0,\n";
    size_t offset = 0;
    for (auto& option : Options)
    {
      switch (option->Type())
      {
      case OptionType::Boolean:
        text << "    " << option->Name.c_str() << " = " << (1 << offset) << ",\n";
        break;
      case OptionType::Enumeration:
      {
        auto enumerationOption = static_cast<const EnumerationOption*>(option.get());
        for (auto i = 0u; i < enumerationOption->Values.size(); i++)
        {
          text << "    " << option->Name.c_str() << enumerationOption->Values[i].c_str() << " = " << (i << offset) << ",\n";
        }
        break;
      }
      case OptionType::Integer:
      {
        auto integerOption = static_cast<const IntegerOption*>(option.get());
        auto range = integerOption->Maximum - integerOption->Minimum + 1;
        for (auto i = 0; i < range; i++)
        {
          text << "    " << option->Name.c_str() << (i + integerOption->Minimum) << " = " << (i << offset) << ",\n";
        }
        break;
      }
      }

      offset += option->KeyLength();
    }
    text << "  };\n";
    text << "}\n";
    return text.str();
  }

  size_t ShaderOption::KeyLength() const
  {
    auto range = ValueCount();
    return range > 0 ? (size_t)ceil(log2(float(range))) : 0;
  }

  std::vector<OptionPermutation> ShaderOption::Permutate(const std::vector<std::unique_ptr<ShaderOption>>& options)
  {
    //If there are no options, there is a single empty permutation
    if (options.empty())
    {
      return { {} };
    }

    //Count permutations
    size_t permutationCount = 1;
    for (auto& option : options)
    {
      permutationCount *= option->ValueCount();
    }

    //Create result buffer
    vector<OptionPermutation> results;
    results.reserve(permutationCount);

    //Create index buffer
    vector<size_t> indices(options.size());
    size_t currentIndex = indices.size() - 1;

    //Iterate permutations
    auto done = false;
    while (!done)
    {
      //Emit result
      OptionPermutation value{};
      size_t offset = 0;
      for (size_t i = 0; i < options.size(); i++)
      {
        auto& option = options[i];

        string definedValue;
        if (option->TryGetDefinedValue(indices[i], definedValue))
        {
          value.Defines.push_back({ option->Name + definedValue, "1" });

          if (option->IsValueDefinedExplicitly())
          {
            value.Defines.push_back({ option->Name, definedValue });
          }
        }

        value.Key |= indices[i] << offset;
        offset += option->KeyLength();
      }
      results.push_back(move(value));

      //Increment indices
      indices[currentIndex]++;

      auto roll = false;
      while (!done && indices[currentIndex] == options[currentIndex]->ValueCount())
      {
        roll = true;

        if (currentIndex == 0)
        {
          done = true;
          break;
        }
        else
        {
          currentIndex--;
        }

        for (size_t i = currentIndex + 1; i < indices.size(); i++)
        {
          indices[i] = 0;
        }

        indices[currentIndex]++;
      }

      if (roll) currentIndex = indices.size() - 1;
    }

    return results;
  }

  OptionType BooleanOption::Type() const
  {
    return OptionType::Boolean;
  }

  size_t BooleanOption::ValueCount() const
  {
    return 2;
  }

  bool BooleanOption::IsValueDefinedExplicitly() const
  {
    return false;
  }

  bool BooleanOption::TryGetDefinedValue(size_t index, std::string& value) const
  {
    value = "";
    return index == 1;
  }

  OptionType EnumerationOption::Type() const
  {
    return OptionType::Enumeration;
  }

  size_t EnumerationOption::ValueCount() const
  {
    return Values.size();
  }

  bool EnumerationOption::IsValueDefinedExplicitly() const
  {
    return true;
  }

  bool EnumerationOption::TryGetDefinedValue(size_t index, std::string& value) const
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

  OptionType IntegerOption::Type() const
  {
    return OptionType::Integer;
  }

  size_t IntegerOption::ValueCount() const
  {
    return size_t(Maximum - Minimum) + 1;
  }

  bool IntegerOption::IsValueDefinedExplicitly() const
  {
    return true;
  }

  bool IntegerOption::TryGetDefinedValue(size_t index, std::string& value) const
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
}