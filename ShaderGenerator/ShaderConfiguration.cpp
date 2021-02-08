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

    static regex regex("#pragma\\s+(target|entry|option)\\s+(.*)");
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
}