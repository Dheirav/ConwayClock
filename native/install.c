// Installer. The wallpaper executable installs itself: it copies into
// %LOCALAPPDATA%\LifeClock, makes Start-menu and startup shortcuts, can
// register itself as the screensaver, and writes an uninstall entry so it
// appears in Windows' Installed apps list. --setup is the window,
// --install / --uninstall are the silent equivalents.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shlobj.h>
#include <shellapi.h>
#include <objbase.h>
#include <stdio.h>
#include <string.h>
#include "install.h"

#define APP_NAME "Life Clock"
#define APP_KEY "LifeClock"
#define APP_VERSION "1.8"   // bump with the release tag; shown in Settings > Apps > Installed apps
#define UNINST_KEY "Software\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\" APP_KEY

int lc_make_shortcut(const char *target, const char *args, const char *workdir, const char *desc, const char *linkPath) {
  CoInitialize(NULL); IShellLinkA *sl = NULL; IPersistFile *pf = NULL; int ok = 0;
  if (SUCCEEDED(CoCreateInstance(&CLSID_ShellLink, NULL, CLSCTX_INPROC_SERVER, &IID_IShellLinkA, (void **)&sl))) {
    sl->lpVtbl->SetPath(sl, target); if (args && *args) sl->lpVtbl->SetArguments(sl, args);
    if (workdir) sl->lpVtbl->SetWorkingDirectory(sl, workdir); if (desc) sl->lpVtbl->SetDescription(sl, desc);
    sl->lpVtbl->SetIconLocation(sl, target, 0);
    if (SUCCEEDED(sl->lpVtbl->QueryInterface(sl, &IID_IPersistFile, (void **)&pf))) {
      wchar_t wp[MAX_PATH]; MultiByteToWideChar(CP_ACP, 0, linkPath, -1, wp, MAX_PATH);
      ok = SUCCEEDED(pf->lpVtbl->Save(pf, wp, TRUE)); pf->lpVtbl->Release(pf); }
    sl->lpVtbl->Release(sl); }
  CoUninitialize(); return ok;
}
void lc_install_dir(char *out, size_t n) { char base[MAX_PATH]; SHGetFolderPathA(NULL, CSIDL_LOCAL_APPDATA, NULL, 0, base); snprintf(out, n, "%s\\%s", base, APP_KEY); }
static void link_path(int csidl, char *out, size_t n) { char base[MAX_PATH]; SHGetFolderPathA(NULL, csidl, NULL, 0, base); snprintf(out, n, "%s\\%s.lnk", base, APP_NAME); }
int lc_is_installed(void) { char d[MAX_PATH], e[MAX_PATH]; lc_install_dir(d, sizeof d); snprintf(e, sizeof e, "%s\\life-clock.exe", d); return GetFileAttributesA(e) != INVALID_FILE_ATTRIBUTES; }

static void reg_str(HKEY k, const char *name, const char *val) { RegSetValueExA(k, name, 0, REG_SZ, (const BYTE *)val, (DWORD)strlen(val) + 1); }
static void reg_dw(HKEY k, const char *name, DWORD v) { RegSetValueExA(k, name, 0, REG_DWORD, (const BYTE *)&v, sizeof v); }

int lc_install(const char *srcExe, int startup, int screensaver, int startmenu) {
  char dir[MAX_PATH], exe[MAX_PATH], scr[MAX_PATH], link[MAX_PATH];
  lc_install_dir(dir, sizeof dir); CreateDirectoryA(dir, NULL);
  snprintf(exe, sizeof exe, "%s\\life-clock.exe", dir); snprintf(scr, sizeof scr, "%s\\life-clock.scr", dir);
  if (_stricmp(srcExe, exe)) { if (!CopyFileA(srcExe, exe, FALSE)) return 0; }
  CopyFileA(exe, scr, FALSE);
  { // keep the settings from wherever it was run, if the target has none yet
    char srcIni[MAX_PATH], dstIni[MAX_PATH]; strcpy(srcIni, srcExe); char *s = strrchr(srcIni, '\\'); if (s) strcpy(s + 1, "life-clock.ini");
    snprintf(dstIni, sizeof dstIni, "%s\\life-clock.ini", dir);
    if (GetFileAttributesA(dstIni) == INVALID_FILE_ATTRIBUTES) CopyFileA(srcIni, dstIni, TRUE); }
  if (startmenu) { link_path(CSIDL_PROGRAMS, link, sizeof link); lc_make_shortcut(exe, NULL, dir, "Conway's Game of Life clock wallpaper", link); }
  link_path(CSIDL_STARTUP, link, sizeof link);
  if (startup) lc_make_shortcut(exe, NULL, dir, APP_NAME, link); else DeleteFileA(link);
  if (screensaver) { HKEY k;
    if (RegCreateKeyExA(HKEY_CURRENT_USER, "Control Panel\\Desktop", 0, NULL, 0, KEY_SET_VALUE, NULL, &k, NULL) == ERROR_SUCCESS) {
      reg_str(k, "SCRNSAVE.EXE", scr); reg_str(k, "ScreenSaveActive", "1"); RegCloseKey(k);
      SystemParametersInfoA(SPI_SETSCREENSAVEACTIVE, TRUE, NULL, SPIF_UPDATEINIFILE | SPIF_SENDCHANGE); } }
  { HKEY k; if (RegCreateKeyExA(HKEY_CURRENT_USER, UNINST_KEY, 0, NULL, 0, KEY_SET_VALUE, NULL, &k, NULL) == ERROR_SUCCESS) {
      char icon[MAX_PATH + 4], un[MAX_PATH + 16];
      snprintf(icon, sizeof icon, "%s,0", exe); snprintf(un, sizeof un, "\"%s\" --uninstall", exe);
      reg_str(k, "DisplayName", "Conway Clock (Life Clock wallpaper)"); reg_str(k, "DisplayVersion", APP_VERSION);
      reg_str(k, "Publisher", "Dheirav"); reg_str(k, "InstallLocation", dir); reg_str(k, "DisplayIcon", icon);
      reg_str(k, "UninstallString", un); reg_str(k, "URLInfoAbout", "https://github.com/Dheirav/ConwayClock");
      reg_dw(k, "NoModify", 1); reg_dw(k, "NoRepair", 1); reg_dw(k, "EstimatedSize", 16400); RegCloseKey(k); } }
  return 1;
}
int lc_uninstall(void) {
  char dir[MAX_PATH], scr[MAX_PATH], link[MAX_PATH], cur[MAX_PATH];
  lc_install_dir(dir, sizeof dir); snprintf(scr, sizeof scr, "%s\\life-clock.scr", dir);
  { HANDLE ev = OpenEventA(EVENT_MODIFY_STATE, FALSE, "LifeClockWallpaperQuit"); if (ev) { SetEvent(ev); CloseHandle(ev); Sleep(700); } }
  link_path(CSIDL_PROGRAMS, link, sizeof link); DeleteFileA(link);
  link_path(CSIDL_STARTUP, link, sizeof link); DeleteFileA(link);
  { HKEY k; if (RegOpenKeyExA(HKEY_CURRENT_USER, "Control Panel\\Desktop", 0, KEY_READ | KEY_SET_VALUE, &k) == ERROR_SUCCESS) {
      char v[MAX_PATH] = ""; DWORD n = sizeof v; if (RegQueryValueExA(k, "SCRNSAVE.EXE", NULL, NULL, (LPBYTE)v, &n) == ERROR_SUCCESS && !_stricmp(v, scr)) RegDeleteValueA(k, "SCRNSAVE.EXE");
      RegCloseKey(k); } }
  RegDeleteKeyA(HKEY_CURRENT_USER, UNINST_KEY);
  GetModuleFileNameA(NULL, cur, MAX_PATH);
  if (!_strnicmp(cur, dir, strlen(dir))) { // running from the install folder: remove it after we exit
    char cmd[MAX_PATH * 2]; snprintf(cmd, sizeof cmd, "/c timeout /t 3 /nobreak >nul & rmdir /s /q \"%s\"", dir);
    ShellExecuteA(NULL, "open", "cmd.exe", cmd, NULL, SW_HIDE);
  } else { char c2[MAX_PATH * 2]; snprintf(c2, sizeof c2, "/c rmdir /s /q \"%s\"", dir); ShellExecuteA(NULL, "open", "cmd.exe", c2, NULL, SW_HIDE); }
  return 1;
}

// ---- setup window ---------------------------------------------------------------
#define ID_STARTUP 300
#define ID_MENU    301
#define ID_SAVER   302
#define ID_DO      303
#define ID_UNDO    304
#define ID_CLOSE   305
static HWND hStartup, hMenu, hSaver, hStatus, hDo, hUndo;
static char srcExe[MAX_PATH], srcDir[MAX_PATH];
static HFONT sfont; static int sdpi = 96;
#define SS(px) MulDiv(px, sdpi, 96)
static HWND smk(HWND p, const char *cls, const char *text, DWORD style, int x, int y, int w, int h, int id) {
  HWND c = CreateWindowExA(0, cls, text, WS_CHILD | WS_VISIBLE | style, x, y, w, h, p, (HMENU)(INT_PTR)id, GetModuleHandleA(NULL), NULL); SendMessageA(c, WM_SETFONT, (WPARAM)sfont, TRUE); return c; }
static void refresh(HWND h) {
  int inst = lc_is_installed(); char dir[MAX_PATH], msg[MAX_PATH + 128];
  lc_install_dir(dir, sizeof dir);
  snprintf(msg, sizeof msg, inst ? "Installed in %s" : "Will install into %s", dir);
  SetWindowTextA(hStatus, msg); EnableWindow(hUndo, inst); SetWindowTextA(hDo, inst ? "Reinstall" : "Install");
  (void)h;
}
static LRESULT CALLBACK sproc(HWND h, UINT m, WPARAM w, LPARAM l) {
  if (m == WM_COMMAND) { int id = LOWORD(w);
    if (id == ID_CLOSE) { DestroyWindow(h); return 0; }
    if (id == ID_DO) {
      int ok = lc_install(srcExe, SendMessageA(hStartup, BM_GETCHECK, 0, 0) == BST_CHECKED, SendMessageA(hSaver, BM_GETCHECK, 0, 0) == BST_CHECKED, SendMessageA(hMenu, BM_GETCHECK, 0, 0) == BST_CHECKED);
      if (ok) { char dir[MAX_PATH], exe[MAX_PATH]; lc_install_dir(dir, sizeof dir); snprintf(exe, sizeof exe, "%s\\life-clock.exe", dir);
        HANDLE ev = OpenEventA(EVENT_MODIFY_STATE, FALSE, "LifeClockWallpaperQuit"); if (ev) { SetEvent(ev); CloseHandle(ev); Sleep(700); }
        ShellExecuteA(NULL, "open", exe, NULL, dir, SW_SHOWNORMAL);
        MessageBoxA(h, "Installed. The wallpaper is running from the install folder;\nyou can delete the copy you ran this from.", APP_NAME, MB_OK | MB_ICONINFORMATION); DestroyWindow(h); }
      else MessageBoxA(h, "Could not copy the files. Is another copy running?", APP_NAME, MB_OK | MB_ICONERROR);
      return 0; }
    if (id == ID_UNDO) { if (MessageBoxA(h, "Remove Life Clock, its shortcuts and its settings?", APP_NAME, MB_YESNO | MB_ICONQUESTION) == IDYES) { lc_uninstall(); MessageBoxA(h, "Removed.", APP_NAME, MB_OK | MB_ICONINFORMATION); DestroyWindow(h); } return 0; }
    return 0; }
  if (m == WM_CTLCOLORSTATIC) { SetBkMode((HDC)w, TRANSPARENT); return (LRESULT)GetSysColorBrush(COLOR_WINDOW); }
  if (m == WM_DESTROY) { PostQuitMessage(0); return 0; }
  return DefWindowProcA(h, m, w, l);
}
int lc_setup_gui(const char *exePath, const char *exeDir) {
  strcpy(srcExe, exePath); strcpy(srcDir, exeDir);
  typedef UINT (WINAPI *GDFS)(void); GDFS g = (GDFS)GetProcAddress(GetModuleHandleA("user32.dll"), "GetDpiForSystem"); if (g) sdpi = g();
  sfont = CreateFontA(-SS(12), 0, 0, 0, FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_SWISS, "Segoe UI");
  WNDCLASSA wc = { 0 }; wc.lpfnWndProc = sproc; wc.hInstance = GetModuleHandleA(NULL); wc.lpszClassName = "LifeClockSetup"; wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1); wc.hCursor = LoadCursor(NULL, IDC_ARROW); wc.hIcon = LoadIconA(wc.hInstance, MAKEINTRESOURCEA(1)); RegisterClassA(&wc);
  int W = SS(460), H = SS(240); RECT r = { 0, 0, W, H }; AdjustWindowRect(&r, WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU, FALSE);
  HWND h = CreateWindowExA(0, wc.lpszClassName, "Life Clock setup", WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_VISIBLE, CW_USEDEFAULT, CW_USEDEFAULT, r.right - r.left, r.bottom - r.top, NULL, NULL, wc.hInstance, NULL);
  int P = SS(16), y = P;
  smk(h, "STATIC", "Conway's Game of Life clock, as a desktop wallpaper.", SS_LEFT, P, y, W - 2 * P, SS(20), 0); y += SS(26);
  hStatus = smk(h, "STATIC", "", SS_LEFT | SS_PATHELLIPSIS, P, y, W - 2 * P, SS(20), 0); y += SS(30);
  hStartup = smk(h, "BUTTON", "Start with Windows", BS_AUTOCHECKBOX, P, y, W - 2 * P, SS(22), ID_STARTUP); y += SS(26);
  hMenu = smk(h, "BUTTON", "Add to the Start menu", BS_AUTOCHECKBOX, P, y, W - 2 * P, SS(22), ID_MENU); y += SS(26);
  hSaver = smk(h, "BUTTON", "Also set as the screensaver", BS_AUTOCHECKBOX, P, y, W - 2 * P, SS(22), ID_SAVER); y += SS(34);
  SendMessageA(hStartup, BM_SETCHECK, BST_CHECKED, 0); SendMessageA(hMenu, BM_SETCHECK, BST_CHECKED, 0);
  hDo = smk(h, "BUTTON", "Install", BS_DEFPUSHBUTTON, P, y, SS(120), SS(28), ID_DO);
  hUndo = smk(h, "BUTTON", "Uninstall", BS_PUSHBUTTON, P + SS(132), y, SS(120), SS(28), ID_UNDO);
  smk(h, "BUTTON", "Close", BS_PUSHBUTTON, W - P - SS(100), y, SS(100), SS(28), ID_CLOSE);
  refresh(h);
  MSG msg; while (GetMessageA(&msg, NULL, 0, 0)) { if (!IsDialogMessageA(h, &msg)) { TranslateMessage(&msg); DispatchMessageA(&msg); } }
  return 0;
}
