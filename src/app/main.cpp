#include <windows.h>
#include <shellapi.h>
#include <imm.h>
#include <shobjidl.h>

#include <string>
#include <vector>

#include "app.h"

int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int) {
  HeapSetInformation(nullptr, HeapEnableTerminationOnCorruption, nullptr, 0);
  // TailDrop has no text fields; keeping IMEs out saves several MB.
  ImmDisableIME(static_cast<DWORD>(-1));
  SetCurrentProcessExplicitAppUserModelID(L"YuboCao.TailDrop");
  int argc = 0;
  wchar_t** argv = CommandLineToArgvW(GetCommandLineW(), &argc);
  std::vector<std::wstring> args;
  for (int i = 1; i < argc; ++i) args.emplace_back(argv[i]);
  LocalFree(argv);
  return App::Get().Run(args);
}
