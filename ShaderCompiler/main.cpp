#include "pch.h"

using namespace winrt;
using namespace Windows::Foundation;

int main()
{
    init_apartment();
    Uri uri(L"http://aka.ms/cppwinrt");
    printf("Hello, %ls!\n", uri.AbsoluteUri().c_str());

    D3D_SHADER_MACRO defines[] = { "fail", "1", NULL, NULL };

    com_ptr<ID3DBlob> code, errors;
    D3DCompileFromFile(
      L"C:\\cae\\dev\\asgard\\Holomaps.MapService\\InsetVertexShader.hlsl",
      defines,
      D3D_COMPILE_STANDARD_FILE_INCLUDE,
      "main",
      "vs_5_0",
      D3DCOMPILE_DEBUG | D3DCOMPILE_DEBUG_NAME_FOR_SOURCE,
      0u,
      code.put(),
      errors.put());

    char* r = (char*)errors->GetBufferPointer();

    com_ptr<ID3DBlob> pdb;
    check_hresult(D3DGetBlobPart(
      code->GetBufferPointer(), 
      code->GetBufferSize(), 
      D3D_BLOB_PDB, 
      0, 
      pdb.put()));

    com_ptr<ID3DBlob> pdbName;
    check_hresult(D3DGetBlobPart(
      code->GetBufferPointer(),
      code->GetBufferSize(), 
      D3D_BLOB_DEBUG_NAME, 
      0, 
      pdbName.put()));

    struct ShaderDebugName
    {
      uint16_t Flags;
      uint16_t NameLength;      
    };

    auto debugNameData = reinterpret_cast<const ShaderDebugName*>(pdbName->GetBufferPointer());
    auto name = reinterpret_cast<const char*>(debugNameData + 1);



    system("pause");
}
