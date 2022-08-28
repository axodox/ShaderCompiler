#include "pch.h"
#include "FileAttributes.h"

using namespace std;
using namespace std::chrono;
using namespace std::filesystem;

using namespace winrt;

namespace ShaderGenerator
{
  handle open_file(const path& filePath, uint32_t access)
  {
    return handle(CreateFile(
      filePath.c_str(),
      access,
      FILE_SHARE_READ,
      NULL,
      OPEN_EXISTING,
      FILE_ATTRIBUTE_NORMAL,
      NULL));
  }

  time_point<system_clock> get_file_time(const path& filePath, file_time_kind kind)
  {
    auto file = open_file(filePath, FILE_READ_ATTRIBUTES);

    winrt::file_time fileTime = {};
    if (file)
    {
      FILETIME time;
      check_bool(GetFileTime(file.get(),
        kind == file_time_kind::creation ? &time : nullptr,
        kind == file_time_kind::access ? &time : nullptr,
        kind == file_time_kind::modification ? &time : nullptr));

      fileTime = time;
    }

    return system_clock::from_time_t(clock::to_time_t(clock::from_file_time(fileTime)));
  }
}