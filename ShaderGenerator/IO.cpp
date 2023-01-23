#include "pch.h"
#include "IO.h"

using namespace std;
using namespace std::filesystem;

namespace ShaderGenerator
{
  std::string ReadAllText(const std::filesystem::path& path)
  {
    FILE* file = nullptr;
    _wfopen_s(&file, path.c_str(), L"rb");
    if (!file) return "";

    fseek(file, 0, SEEK_END);
    auto length = ftell(file);

    string buffer(length, '\0');

    fseek(file, 0, SEEK_SET);
    fread_s(buffer.data(), length, length, 1, file);
    fclose(file);

    return buffer;
  }

  bool WriteAllText(const std::filesystem::path& path, const std::string& text)
  {
    FILE* file = nullptr;
    _wfopen_s(&file, path.c_str(), L"wb");
    if (!file) return false;

    fwrite(text.data(), 1, text.size(), file);
    fclose(file);

    return true;
  }

  bool WriteAllBytes(const path& path, const vector<uint8_t>& bytes)
  {
    ofstream stream(path, ios::out | ios::binary);

    if (stream.good())
    {
      stream.write((const char*)bytes.data(), bytes.size());
    }

    return stream.good();
  }
}