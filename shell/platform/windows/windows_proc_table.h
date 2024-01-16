// Copyright 2013 The Flutter Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef FLUTTER_SHELL_PLATFORM_WINDOWS_WINDOWS_PROC_TABLE_H_
#define FLUTTER_SHELL_PLATFORM_WINDOWS_WINDOWS_PROC_TABLE_H_

#include <optional>

#include "flutter/fml/macros.h"
#include "flutter/fml/native_library.h"

namespace flutter {

// Lookup table for Windows APIs that aren't available on all versions of
// Windows, or for mocking Windows API calls.
class WindowsProcTable {
 public:
  WindowsProcTable();
  virtual ~WindowsProcTable();

  // Dispatches incoming nonqueued messages, checks the thread message queue for
  // a posted message, and retrieves the message (if any exist).
  //
  // The "Win32" prefix avoids win32's "PeekMessage" preprocessor definition.
  //
  // See:
  // https://learn.microsoft.com/windows/win32/api/winuser/nf-winuser-peekmessagew
  virtual BOOL Win32PeekMessage(LPMSG lpMsg,
                                HWND hWnd,
                                UINT wMsgFilterMin,
                                UINT wMsgFilterMax,
                                UINT wRemoveMsg);

  // Sends the specified message to a window or windows. The SendMessage
  // function calls the window procedure for the specified window and does not
  // return until the window procedure has processed the message.
  //
  // The "Win32" prefix avoids win32's "SendMessage" preprocessor definition.
  //
  // See:
  // https://learn.microsoft.com/windows/win32/api/winuser/nf-winuser-sendmessagew
  virtual LRESULT Win32SendMessage(HWND hWnd,
                                   UINT Msg,
                                   WPARAM wParam,
                                   LPARAM lParam);

  // Translates a virtual-key code into a scan code or character value,
  // or translates a scan code into a virtual-key code.
  //
  // The "Win32" prefix circumvents win32's "MapVirtualKey" preprocessor
  // definition.
  //
  // See:
  // https://learn.microsoft.com/windows/win32/api/winuser/nf-winuser-mapvirtualkeyw
  virtual UINT Win32MapVirtualKey(UINT uCode, UINT uMapType);

  // Retrieves the pointer type for a specified pointer.
  //
  // Used to react differently to touch or pen inputs. Returns false on failure.
  // Available on Windows 8 and newer, otherwise returns false.
  virtual BOOL GetPointerType(UINT32 pointer_id,
                              POINTER_INPUT_TYPE* pointer_type) const;

  // Get the preferred languages for the thread, and optionally the process,
  // and system, in that order, depending on the flags.
  //
  // See:
  // https://learn.microsoft.com/windows/win32/api/winnls/nf-winnls-getthreadpreferreduilanguages
  virtual LRESULT GetThreadPreferredUILanguages(DWORD flags,
                                                PULONG count,
                                                PZZWSTR languages,
                                                PULONG length) const;

  // Get whether high contrast is enabled.
  //
  // Available on Windows 8 and newer, otherwise returns false.
  //
  // See:
  // https://learn.microsoft.com/windows/win32/winauto/high-contrast-parameter
  virtual bool GetHighContrastEnabled() const;

  // Get whether the system compositor, DWM, is enabled.
  //
  // See:
  // https://learn.microsoft.com/windows/win32/api/dwmapi/nf-dwmapi-dwmiscompositionenabled
  virtual bool DwmIsCompositionEnabled() const;

  // Issues a flush call that blocks the caller until all of the outstanding
  // surface updates have been made.
  //
  // See:
  // https://learn.microsoft.com/windows/win32/api/dwmapi/nf-dwmapi-dwmflush
  virtual HRESULT DwmFlush() const;

 private:
  using GetPointerType_ = BOOL __stdcall(UINT32 pointerId,
                                         POINTER_INPUT_TYPE* pointerType);

  // The User32.dll library, used to resolve functions at runtime.
  fml::RefPtr<fml::NativeLibrary> user32_;

  std::optional<GetPointerType_*> get_pointer_type_;

  FML_DISALLOW_COPY_AND_ASSIGN(WindowsProcTable);
};

}  // namespace flutter

#endif  // FLUTTER_SHELL_PLATFORM_WINDOWS_WINDOWS_PROC_TABLE_H_
