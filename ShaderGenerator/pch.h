#pragma once
#include <filesystem>
#include <fstream>
#include <regex>
#include <queue>
#include <mutex>
#include <thread>
#include <sstream>
#include <unordered_set>
#include <functional>

#define NOMINMAX

#include <Windows.h>
#include <winrt/base.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Storage.h>
#include <winrt/Windows.Storage.Streams.h>
#include <winrt/Windows.Storage.Compression.h>

#include <d3dcompiler.h>
#pragma comment (lib, "D3DCompiler.lib")
