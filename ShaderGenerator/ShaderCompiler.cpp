#include "pch.h"
#include "ShaderCompiler.h"

using namespace std;
using namespace winrt;

namespace ShaderGenerator
{
  struct ShaderCompilationContext
  {
    const ShaderInfo* Shader;
    const CompilationOptions* Options;
    queue<const OptionPermutation*> Input;
    bool IsFailed = false;

    mutex InputMutex, OutputMutex, MessagesMutex;
    vector<CompiledShader> Output;
    unordered_set<string> Messages;

    ShaderCompilationContext(const ShaderInfo& info, const CompilationOptions& options, const vector<OptionPermutation>& permutations) :
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
        macros.push_back({ define.first.c_str(), define.second.c_str() });
      }
      macros.push_back({ nullptr, nullptr });

      //Run compilation
      com_ptr<ID3DBlob> binary, errors;
      D3DCompileFromFile(
        context.Shader->Path.c_str(),
        macros.data(),
        D3D_COMPILE_STANDARD_FILE_INCLUDE,
        context.Shader->EntryPoint.c_str(),
        context.Shader->Target.c_str(),
        context.Options->IsDebug ? D3DCOMPILE_DEBUG : D3DCOMPILE_OPTIMIZATION_LEVEL3,
        0u,
        binary.put(),
        errors.put());

      //Print out messages
      stringstream messages{ (char*)errors->GetBufferPointer() };
      string message;
      static regex warningIgnoreRegex(".*: warning X3568: '(target|entry|option)' : unknown pragma ignored");
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
      if (binary)
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

  vector<CompiledShader> ShaderGenerator::CompileShader(const ShaderInfo& shader, const CompilationOptions& options)
  {
    auto permutations = ShaderOption::Permutate(shader.Options);
    ShaderCompilationContext context{shader, options, permutations};

    wprintf(L"Compiling %s...\n", shader.Path.c_str());
    printf("Generating %zu shader variants.\n", permutations.size());

    auto threadCount = min(thread::hardware_concurrency(), permutations.size());
    vector<HANDLE> threads(threadCount);
    for (auto& thread : threads)
    {
      thread = CreateThread(nullptr, 0, &CompileWorker, &context, 0, nullptr);
    }

    WaitForMultipleObjects(threads.size(), threads.data(), true, INFINITE);
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
