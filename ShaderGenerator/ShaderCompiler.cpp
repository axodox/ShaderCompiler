#include "pch.h"
#include "ShaderCompiler.h"

using namespace std;
using namespace winrt;

namespace ShaderGenerator
{
  struct ShaderCompilationContext
  {
    const ShaderInfo* Shader;
    const ShaderCompilationArguments* Options;
    queue<const OptionPermutation*> Input;
    bool IsFailed = false;

    mutex InputMutex, OutputMutex, MessagesMutex;
    vector<CompiledShader> Output;
    unordered_set<string> Messages;

    ShaderCompilationContext(const ShaderInfo& info, const ShaderCompilationArguments& options, const vector<OptionPermutation>& permutations) :
      Shader(&info),
      Options(&options)
    {
      for (auto& permutation : permutations)
      {
        Input.push(&permutation);
      }
    }
  };

  DWORD WINAPI CompileWorker(LPVOID contextPtr)
  {
    ShaderCompilationContext& context = *((ShaderCompilationContext*)contextPtr);

    while (true)
    {
      //Get work item
      const OptionPermutation* permutation;
      {
        lock_guard<mutex> lock(context.InputMutex);
        if (context.Input.empty()) return 0;

        permutation = context.Input.front();
        context.Input.pop();
      }

      //Define macros
      vector<D3D_SHADER_MACRO> macros;
      for (auto& define : permutation->Defines)
      {
        macros.push_back({ define.c_str(), "1" });
      }
      macros.push_back({ nullptr, nullptr });

      auto flags = 0u;
      if (context.Options->IsDebug) flags |= D3DCOMPILE_DEBUG;
      switch (context.Options->OptimizationLevel)
      {
      case 0:
        flags |= D3DCOMPILE_OPTIMIZATION_LEVEL0;
        break;
      case 1:
        flags |= D3DCOMPILE_OPTIMIZATION_LEVEL1;
        break;
      case 2:
        flags |= D3DCOMPILE_OPTIMIZATION_LEVEL2;
        break;
      case 3:
        flags |= D3DCOMPILE_OPTIMIZATION_LEVEL3;
        break;
      }

      //Run compilation
      com_ptr<ID3DBlob> binary, errors;
      auto result = D3DCompileFromFile(
        context.Shader->Path.c_str(),
        macros.data(),
        D3D_COMPILE_STANDARD_FILE_INCLUDE,
        context.Shader->EntryPoint.c_str(),
        context.Shader->Target.c_str(),
        flags,
        0u,
        binary.put(),
        errors.put());

      //Print out messages
      stringstream messages{ (char*)errors->GetBufferPointer() };
      string message;
      static regex warningIgnoreRegex(".*: warning X3568: '(target|namespace|entry|option)' : unknown pragma ignored");
      {
        lock_guard<mutex> lock(context.MessagesMutex);
        while (getline(messages, message, '\n'))
        {
          if (!regex_match(message, warningIgnoreRegex) && context.Messages.emplace(message).second)
          {
            printf("%s\n", message.c_str());
          }
        }
      }

      //If successful return binary
      if (SUCCEEDED(result))
      {
        vector<uint8_t> data(binary->GetBufferSize());
        memcpy(data.data(), binary->GetBufferPointer(), binary->GetBufferSize());

        lock_guard<mutex> lock(context.OutputMutex);        
        context.Output.push_back({ permutation->Key, move(data) });
      }
      else
      {
        context.IsFailed = true;
      }
    }

    return 0;
  }

  vector<CompiledShader> ShaderGenerator::CompileShader(const ShaderInfo& shader, const ShaderCompilationArguments& options)
  {
    auto permutations = ShaderOption::Permutate(shader.Options);
    ShaderCompilationContext context{shader, options, permutations};

    printf("Compiling %s at optimization level %d", shader.Path.string().c_str(), options.OptimizationLevel);
    if (options.IsDebug) printf(" with debug symbols");
    printf("...\n Generating %zu shader variants.\n", permutations.size());

    auto threadCount = min(thread::hardware_concurrency(), permutations.size());
    vector<HANDLE> threads(threadCount);
    for (auto& thread : threads)
    {
      thread = CreateThread(nullptr, 0, &CompileWorker, &context, 0, nullptr);
    }

    WaitForMultipleObjects(DWORD(threads.size()), threads.data(), true, INFINITE);
    if (context.IsFailed)
    {
      printf("Shader group compilation failed.\n");
      return {};
    }
    else
    {
      printf("Shader group compilation succeeded.\n");
      return move(context.Output);
    }
  }
}
