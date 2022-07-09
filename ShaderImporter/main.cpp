#include "pch.h"
#include "ComputeShader.h"
#include "ShaderGenerator.h"

using namespace std;
using namespace winrt;
using namespace DirectX;

int main()
{
  init_apartment();

  D3D_FEATURE_LEVEL featureLevels[] = {
    D3D_FEATURE_LEVEL_11_0
  };

  com_ptr<ID3D11Device> device;
  com_ptr<ID3D11DeviceContext> context;
  check_hresult(D3D11CreateDevice(
    nullptr,
    D3D_DRIVER_TYPE_WARP,
    nullptr,
    0,
    featureLevels,
    ARRAYSIZE(featureLevels),
    D3D11_SDK_VERSION,
    device.put(),
    nullptr,
    context.put()));
  
  auto shaderGroup = ShaderGenerator::CompiledShaderGroup::FromFile("G:\\cae\\dev\\ShaderCompiler\\ShaderImporter\\bin\\Release\\x64\\ComputeShader.csg");

  auto shaderVariant = shaderGroup.Shader(ShaderImporter::Shaders::ComputeShaderFlags::BooleanOption);
  com_ptr<ID3D11ComputeShader> shader;
  check_hresult(device->CreateComputeShader(shaderVariant->ByteCode.data(), shaderVariant->ByteCode.size(), nullptr, shader.put()));

  printf("Import works!\n");
}
