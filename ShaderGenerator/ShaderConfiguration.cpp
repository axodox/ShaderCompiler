#include "pch.h"
#include "ShaderConfiguration.h"

using namespace std;

namespace ShaderGenerator
{
  std::unique_ptr<ShaderOption> ParseOption(const std::string& text)
  {
    static regex boolRegex("bool\\s+(\\w*)\\s*");
    static regex enumRegex("enum\\s+(\\w*)\\s+\\{((\\w+\\s*,\\s*)*\\w+\\s*)\\}\\s*");
    static regex intRegex("int\\s+(\\w*)\\s+\\{(\\d+)\\.\\.(\\d+)\\}\\s*");

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
    else if (regex_match(text, match, intRegex))
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
  
  ShaderInfo ShaderInfo::FromFile(const std::filesystem::path& path)
  {
    ShaderInfo result{};
    result.Path = path;

    static regex regex("#pragma\\s+(target|namespace|entry|option)\\s+(.*)");
    ifstream file(path);
    string line;
    while (getline(file, line))
    {
      smatch match;
      if (regex_match(line, match, regex))
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
        else if(match[1] == "option")
        {
          auto option = ParseOption(match[2]);
          if (option) result.Options.push_back(move(option));
        }
      }
    }

    return result;
  }

  std::string ShaderInfo::GenerateHeader() const
  {
    stringstream text;
    text << "#pragma once\n";
    text << "#include \"pch.h\"\n";
    text << "\n";
    text << "namespace " << Namespace.c_str() << "\n";
    text << "{\n";
    text << "  enum class " << Path.filename().replace_extension().string().c_str() << "Flags : uint64_t\n";
    text << "  {\n";
    text << "    Default = 0,\n";
    auto offset = 0;
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
        for (auto i = 0; i < enumerationOption->Values.size(); i++)
        {
          text << "    " << option->Name.c_str() << enumerationOption->Values[i].c_str() << " = " << (i << offset) << ",\n";
        }
        break;
      }
      case OptionType::Integer:
      {
        auto integerOption = static_cast<const IntegerOption*>(option.get());
        auto range = integerOption->Maximum - integerOption->Minimum;
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

  std::vector<OptionPermutation> ShaderOption::Permutate(const std::vector<std::unique_ptr<ShaderOption>>& options)
  {
    std::vector<size_t> indices(options.size());
    size_t currentIndex = 0;
    auto done = false;

    vector<OptionPermutation> results;

    while (!done)
    {
      //Emit result
      OptionPermutation value{};
      for (size_t i = 0; i < options.size(); i++)
      {
        auto& option = options[i];

        string definedValue;
        if (option->TryGetDefinedValue(indices[i], definedValue))
        {
          value.Defines.push_back({ option->Name, definedValue });
        }

        value.Key = (value.Key << option->KeyLength()) + indices[i];
      }
      results.push_back(value);

      //Iterate
      indices[currentIndex]++;
      auto roll = false;
      while (!done && indices[currentIndex] == options[currentIndex]->ValueCount())
      {
        roll = true;
        currentIndex++;
        if (currentIndex == indices.size())
        {
          done = true;
          continue;
        }

        for (size_t i = 0; i < currentIndex; i++)
        {
          indices[i] = 0;
        }
        indices[currentIndex]++;
      }

      if (roll) currentIndex = 0;
    }

    return results;
  }
  
  void WriteAllText(const std::filesystem::path& path, const std::string& text)
  {
    ofstream stream(path);
    stream << text;
  }
}