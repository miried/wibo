#pragma once

#include "types.h"

namespace user32 {

using LPARAM = LONG_PTR;
using WNDENUMPROC = BOOL(WINAPI *)(HWND hwnd, LPARAM lParam);

int WINAPI LoadStringA(HMODULE hInstance, UINT uID, LPSTR lpBuffer, int cchBufferMax);
int WINAPI LoadStringW(HMODULE hInstance, UINT uID, LPWSTR lpBuffer, int cchBufferMax);
int WINAPI MessageBoxA(HWND hwnd, LPCSTR lpText, LPCSTR lpCaption, UINT uType);
HKL WINAPI GetKeyboardLayout(DWORD idThread);
HWINSTA WINAPI GetProcessWindowStation();
BOOL WINAPI GetUserObjectInformationA(HANDLE hObj, int nIndex, PVOID pvInfo, DWORD nLength, LPDWORD lpnLengthNeeded);
HWND WINAPI GetActiveWindow();
BOOL WINAPI EnumThreadWindows(DWORD dwThreadId, WNDENUMPROC lpfn, LPARAM lParam);

} // namespace user32
