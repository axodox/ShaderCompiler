using Microsoft.CodeAnalysis.CSharp.Scripting;
using Microsoft.CodeAnalysis.Scripting;
using System;
using System.Collections.Generic;
using System.Globalization;
using System.Linq;
using System.Text;
using System.Text.RegularExpressions;
using System.Threading.Tasks;

namespace ShaderCompilerTask
{
  class PreprocessorDefinition
  {
    public string Name { get; set; }

    public string Value { get; set; }

    public PreprocessorDefinition(string name, string value = default)
    {
      Name = name;
      Value = value;
    }

    public static PreprocessorDefinition Empty = new PreprocessorDefinition(string.Empty);

    public override string ToString()
    {
      var result = Name;
      if (!string.IsNullOrEmpty(Value))
      {
        result += $" = {Value}";
      }
      return result;
    }
  }

  abstract class Option
  {
    public string Name { get; set; }

    public abstract PreprocessorDefinition[] Defines { get; }

    public string Condition { get; set; }

    public abstract string Type { get; }

    public ScriptRunner<bool> ConditionScript { get; set; }
  }

  class BooleanOption : Option
  {
    public override PreprocessorDefinition[] Defines => new[] {
      PreprocessorDefinition.Empty,
      new PreprocessorDefinition(Name)
    };

    public override string Type { get; } = "bool";
  }

  class EnumOption : Option
  {
    public string[] Values { get; set; }

    public override PreprocessorDefinition[] Defines => Values
      .Select(p => new PreprocessorDefinition(Name, p))
      .Concat(Condition == null ? Array.Empty<PreprocessorDefinition>() : new[] { PreprocessorDefinition.Empty })
      .ToArray();

    public override string Type { get; } = "string";
  }

  class IntegerOption : Option
  {
    public int From { get; set; }

    public int To { get; set; }

    public override PreprocessorDefinition[] Defines => Enumerable
      .Range(From, To - From)
      .Select(p => new PreprocessorDefinition(Name, p.ToString()))
      .Concat(Condition == null ? Array.Empty<PreprocessorDefinition>() : new[] { PreprocessorDefinition.Empty } )
      .ToArray();

    public override string Type { get; } = "int";
  }

  class OptionParser
  {
    private const RegexOptions _regexOptions = RegexOptions.Compiled;

    private readonly Regex _optionRegex = new Regex(@"^(?<name>\w+)(?<arguments>[^(]+)?(\s*\((?<condition>[^)]*)\))?$", _regexOptions);
    private readonly Regex _enumArgumentsRegex = new Regex(@"^(?<value>\w+)\s*(?>\|\s*(?<value>\w+)\s*)*$", _regexOptions);
    private readonly Regex _integerArgumentsRegex = new Regex(@"^(?<from>\d+)..(?<to>\d+)$", _regexOptions);

    public Option[] Parse(string text)
    {
      var options = new List<Option>();
      foreach (var optionText in text.Split(','))
      {
        var optionMatch = _optionRegex.Match(optionText.Trim());
        if (optionMatch.Success)
        {
          var optionName = optionMatch.Groups["name"].Value;
          var optionArguments = optionMatch.Groups["arguments"].Value.Trim();
          var optionCondition = optionMatch.Groups["condition"].Value.Trim();

          Option option;
          if (optionArguments.Length == 0)
          {
            option = new BooleanOption() { Name = optionName };
          }
          else
          {
            var enumMatch = _enumArgumentsRegex.Match(optionArguments);
            if (enumMatch.Success)
            {
              var values = enumMatch
                .Groups["value"]
                .Captures
                .OfType<Capture>()
                .Select(p => p.Value)
                .ToArray();
              option = new EnumOption()
              {
                Name = optionName,
                Values = values
              };
            }
            else
            {
              var integerMatch = _integerArgumentsRegex.Match(optionArguments);
              if (integerMatch.Success)
              {
                var from = int.Parse(integerMatch.Groups["from"].Value, CultureInfo.InvariantCulture);
                var to = int.Parse(integerMatch.Groups["to"].Value, CultureInfo.InvariantCulture);
                option = new IntegerOption()
                {
                  Name = optionName,
                  From = from,
                  To = to
                };
              }
              else
              {
                throw new Exception($"Invalid option arguments: {optionArguments}.");
              }
            }
          }

          if (!string.IsNullOrEmpty(optionCondition))
          {
            option.Condition = optionCondition;
          }

          options.Add(option);
        }
        else
        {
          throw new Exception($"Invalid option: {optionText}.");
        }
      }

      var scriptHeader = "";
      foreach (var option in options)
      {
        scriptHeader += $"{option.Type} {option.Name} = default({option.Type}); object temp_{option.Name}; if(Variables.TryGetValue(\"{option.Name}\", out temp_{option.Name})) {{ {option.Name} = ({option.Type})temp_{option.Name}; }}\r\n";
      }

      foreach (var option in options)
      {
        if (option.Condition == null) continue;
        var script = scriptHeader;
        script += option.Condition;
        option.ConditionScript = CSharpScript.Create<bool>(script, globalsType: typeof(ConditionVariables)).CreateDelegate();
      }
      return options.ToArray();
    }
  }

  class PreprocessorDefinitionProvider
  {
    public PreprocessorDefinition[][] GeneratePermutations(Option[] options)
    {
      var definitions = options
        .Select(p => p.Defines)
        .ToArray();

      var permutations = new List<PreprocessorDefinition[]>();

      var indices = new int[definitions.Length];
      var currentIndex = 0;
      bool done = false;
      while (!done)
      {
        var result = Enumerable
          .Range(0, definitions.Length)
          .Select(p => definitions[p][indices[p]])
          .Where(p => p != PreprocessorDefinition.Empty)
          .ToArray();
        permutations.Add(result);

        indices[currentIndex]++;
        bool roll = false;        
        while (!done && indices[currentIndex] == definitions[currentIndex].Length)
        {
          roll = true;
          currentIndex++;
          if (currentIndex == indices.Length)
          {
            done = true;
            continue;
          }

          for (var i = 0; i < currentIndex; i++)
          {
            indices[i] = 0;
          }
          indices[currentIndex]++;
        }

        if (roll) currentIndex = 0;
      }

      var optionsByName = options.ToDictionary(p => p.Name);
      var validPermutations = new List<PreprocessorDefinition[]>();
      foreach (var permutation in permutations)
      {
        var variables = new ConditionVariables()
        {
          Variables = new Dictionary<string, object>()
        };

        foreach (var definition in permutation)
        {
          var option = optionsByName[definition.Name];
          object value;
          switch (option)
          {
            case BooleanOption booleanOption:
              value = true;
              break;
            case EnumOption enumOption:
              value = $"\"{definition.Value}\"";
              break;
            case IntegerOption integerOption:
              value = int.Parse(definition.Value, CultureInfo.InvariantCulture);
              break;
            default:
              throw new NotImplementedException();
          }
          variables.Variables[definition.Name] = value;
        }

        bool isValid = true;
        foreach (var definition in permutation)
        {
          var option = optionsByName[definition.Name];
          if (option.ConditionScript != null)
          {
            if (!option.ConditionScript(variables).Result)
            {
              isValid = false;
              break;
            }
          }
        }

        if (isValid) validPermutations.Add(permutation);
      }

      return validPermutations.ToArray();
    }
  }

  public class ConditionVariables
  {
    public Dictionary<string, object> Variables { get; set; }
  }

  class Program
  {
    static void Main(string[] args)
    {
      var options = new OptionParser().Parse("boolOptA, boolOptB (boolOptA && enumOpt == \"a\"), enumOpt a | b | c (intOpt == 2), intOpt 1..4");
      var permutations = new PreprocessorDefinitionProvider().GeneratePermutations(options);

      

      
    }
  }
}
