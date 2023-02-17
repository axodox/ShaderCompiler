#include "pch.h"
#include "ComputeShader.h"
#include "ShaderGenerator.h"

using namespace std;
using namespace std::filesystem;
using namespace winrt;
using namespace DirectX;

int main()
{
  init_apartment();

  //Get application directory
  wchar_t executablePath[MAX_PATH];
  GetModuleFileName(NULL, executablePath, MAX_PATH);

  auto applicationRoot = path(executablePath).parent_path();

  //Create Direct3D 11 device
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
  
  //Load shader from file
  auto shaderGroup = ShaderGenerator::CompiledShaderGroup::FromFile(applicationRoot / "ComputeShader.csg");

  using namespace ShaderImporter::Shaders;

  //Select shader variant
  auto shaderVariant = shaderGroup.Shader((int)ComputeShaderFlags::BooleanOption | (int)ComputeShaderFlags::EnumOptionval3 | (int)ComputeShaderFlags::IntegerOption4);

  //Load shader on GPU
  com_ptr<ID3D11ComputeShader> shader;
  check_hresult(device->CreateComputeShader(shaderVariant->ByteCode.data(), shaderVariant->ByteCode.size(), nullptr, shader.put()));

  //Print success message
  printf("Import works!\n");
}
