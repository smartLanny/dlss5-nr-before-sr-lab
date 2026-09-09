#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <cstdlib>
int wmain(int argc, wchar_t** argv) {
  DWORD lifetime = 4500;
  if (argc == 2) { const auto value = wcstoul(argv[1], nullptr, 10); if (value >= 1000 && value <= 20000) lifetime = value; }
  Sleep(lifetime); return 0;
}
