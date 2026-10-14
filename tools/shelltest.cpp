// Developer tool: exercises TailDropShell.dll the way File Explorer does.
//
//   shelltest menu   <file>          dump the classic context menu (with submenus)
//   shelltest cmd    <file>          GetTitle/GetState/EnumSubCommands on the command
//   shelltest invoke <file> <index>  invoke subcommand <index> for <file>
//
// Build: cl /EHsc /std:c++20 /DUNICODE tools\shelltest.cpp ole32.lib shell32.lib user32.lib
#include <windows.h>
#include <shlobj.h>
#include <shobjidl.h>

#include <cstdio>
#include <string>

static const CLSID kClsid = {0x9639f786, 0x0c93, 0x4ce7, {0xbb, 0x89, 0x6f, 0x0e, 0x74, 0x9c, 0x03, 0x6d}};

static IContextMenu3* g_cm3 = nullptr;

static void Dump(HMENU menu, int depth) {
  int n = GetMenuItemCount(menu);
  for (int i = 0; i < n; ++i) {
    wchar_t text[256] = L"";
    MENUITEMINFOW mi{sizeof(mi)};
    mi.fMask = MIIM_STRING | MIIM_SUBMENU | MIIM_FTYPE | MIIM_ID | MIIM_STATE;
    mi.dwTypeData = text;
    mi.cch = 255;
    GetMenuItemInfoW(menu, i, TRUE, &mi);
    if (mi.fType & MFT_SEPARATOR) continue;
    wprintf(L"%*s%s%s\n", depth * 2, L"", text, mi.hSubMenu ? L"  >" : L"");
    if (mi.hSubMenu) {
      if (g_cm3) {
        LRESULT r;
        g_cm3->HandleMenuMsg2(WM_INITMENUPOPUP, reinterpret_cast<WPARAM>(mi.hSubMenu), i, &r);
      }
      Dump(mi.hSubMenu, depth + 1);
    }
  }
}

static IShellItemArray* ItemsFor(const wchar_t* path) {
  PIDLIST_ABSOLUTE pidl = nullptr;
  if (FAILED(SHParseDisplayName(path, nullptr, &pidl, 0, nullptr))) return nullptr;
  IShellItemArray* arr = nullptr;
  PCIDLIST_ABSOLUTE list[1] = {pidl};
  SHCreateShellItemArrayFromIDLists(1, list, &arr);
  CoTaskMemFree(pidl);
  return arr;
}

int wmain(int argc, wchar_t** argv) {
  if (argc < 3) return 2;
  CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
  std::wstring mode = argv[1];
  if (mode == L"menu") {
    PIDLIST_ABSOLUTE pidl = nullptr;
    if (FAILED(SHParseDisplayName(argv[2], nullptr, &pidl, 0, nullptr))) return 1;
    IShellFolder* parent = nullptr;
    PCUITEMID_CHILD child = nullptr;
    SHBindToParent(pidl, IID_PPV_ARGS(&parent), &child);
    IContextMenu* cm = nullptr;
    HRESULT hr = parent->GetUIObjectOf(nullptr, 1, &child, IID_IContextMenu, nullptr, reinterpret_cast<void**>(&cm));
    if (FAILED(hr)) return 1;
    cm->QueryInterface(IID_PPV_ARGS(&g_cm3));
    HMENU menu = CreatePopupMenu();
    cm->QueryContextMenu(menu, 0, 1, 0x7FFF, CMF_NORMAL | CMF_EXTENDEDVERBS);
    Dump(menu, 0);
    return 0;
  }
  IExplorerCommand* cmd = nullptr;
  HRESULT hr = CoCreateInstance(kClsid, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&cmd));
  if (FAILED(hr)) {
    wprintf(L"CoCreateInstance failed 0x%08lx\n", hr);
    return 1;
  }
  IShellItemArray* items = ItemsFor(argv[2]);
  LPWSTR title = nullptr, icon = nullptr;
  cmd->GetTitle(items, &title);
  cmd->GetIcon(items, &icon);
  EXPCMDSTATE state = 0;
  cmd->GetState(items, TRUE, &state);
  EXPCMDFLAGS flags = 0;
  cmd->GetFlags(&flags);
  wprintf(L"title=%s icon=%s state=%lu flags=%lu\n", title, icon, state, flags);
  IEnumExplorerCommand* e = nullptr;
  if (SUCCEEDED(cmd->EnumSubCommands(&e)) && e) {
    IExplorerCommand* sub = nullptr;
    int index = 0, want = argc > 3 ? _wtoi(argv[3]) : -1;
    while (e->Next(1, &sub, nullptr) == S_OK) {
      LPWSTR t = nullptr, ic = nullptr;
      sub->GetTitle(items, &t);
      sub->GetIcon(items, &ic);
      wprintf(L"  [%d] %s  (%s)\n", index, t, ic);
      if (mode == L"invoke" && index == want) wprintf(L"  invoke -> 0x%08lx\n", sub->Invoke(items, nullptr));
      CoTaskMemFree(t);
      CoTaskMemFree(ic);
      sub->Release();
      ++index;
    }
    e->Release();
  }
  return 0;
}
