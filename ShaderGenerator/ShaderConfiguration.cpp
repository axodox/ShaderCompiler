#include "pch.h"
#include "ShaderConfiguration.h"

using namespace std;

namespace ShaderGenerator
{
  std::unique_ptr<ShaderOption> ParseOption(const std::string& text)
  {
    static regex boolRegex("bool\\s+(\\w*)\\s*");
    static regex enumRegex("enum\\s+(\\w*)\\s+\\{\\s*((\\w+\\s*,\\s*)*\\w+)\\s*\\}\\s*");
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
    if (!file.good())
    {
      throw std::exception(("Failed to open file " + path.string()).c_str());
    }

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
    if (options.empty())
    {
      return { {} };
    }

    std::vector<size_t> indices(options.size());
    size_t currentIndex = 0;
    auto done = false;

    vector<OptionPermutation> results;

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
          value.Defines.push_back({ option->Name + definedValue });
        }

        value.Key |= indices[i] << offset;
        offset += option->KeyLength();
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

  void WriteHeader(const ShaderCompilationArguments& arguments, const ShaderInfo& shader)
  {
    string namespaceName;
    if (!shader.Namespace.empty()) namespaceName = shader.Namespace;
    else if (!arguments.NamespaceName.empty()) namespaceName = arguments.NamespaceName;
    else namespaceName = "ShaderGenerator";

    static regex namespaceRegex{"\\."};
    namespaceName = regex_replace(namespaceName, namespaceRegex, "::");

    printf("Generating header for shader group %s at namespace %s...\n", shader.Path.string().c_str(), namespaceName.c_str());
    auto header = shader.GenerateHeader(namespaceName);

    error_code ec;
    filesystem::create_directory(arguments.Header.parent_path(), ec);
    if (ec)
    {
      printf("Failed to create output directory at %s.\n", arguments.Header.parent_path().string().c_str());
    }
    else
    {
      ofstream stream(arguments.Header, ios::out);
      if (stream.good())
      {
        stream << header;
        printf("Output saved to %s.\n", arguments.Header.string().c_str());
      }
      else
      {
        printf("Failed to save output to %s.\n", arguments.Header.string().c_str());
      }
    }
  }
}