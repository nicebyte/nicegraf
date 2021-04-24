#include "platform/window.h"
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

namespace ngf_samples {

window_event_callback* WINDOW_EVENT_CALLBACK = nullptr;

static LRESULT CALLBACK window_proc(HWND hwnd, UINT msgtype, WPARAM wparam, LPARAM lparam) {
  if (WINDOW_EVENT_CALLBACK) {
    window_event event;
    switch (msgtype) {
    case WM_MOUSEMOVE:
      event.type               = window_event_type::pointer;
      event.pointer_state.x    = lparam & 0xffff;
      event.pointer_state.y    = (lparam >> 16) & 0xffff;
      event.pointer_state.down = wparam & MK_LBUTTON;
      WINDOW_EVENT_CALLBACK(event, nullptr);
      break;
    case WM_SIZE:
      event.type                          = window_event_type::resize;
      event.framebuffer_dimensions.width  = lparam & 0xffff;
      event.framebuffer_dimensions.height = (lparam >> 16) & 0xffff;
      WINDOW_EVENT_CALLBACK(event, nullptr);
      break;
    case WM_CLOSE:
      event.type = window_event_type::close;
      WINDOW_EVENT_CALLBACK(event, nullptr);
      break;
    default:
      return DefWindowProc(hwnd, msgtype, wparam, lparam);
    }
  } else {
    return DefWindowProc(hwnd, msgtype, wparam, lparam);
  }
  return 0;
}

void window_set_event_callback(window_event_callback cb) {
  WINDOW_EVENT_CALLBACK = cb;
}

window window_create(
    const char* title,
    uint32_t    initial_framebuffer_width,
    uint32_t    initial_framebuffer_height) {
  static const char wndclass_name[] = "nicegraf_sample";

  // Register window class if necessary.
  WNDCLASS wndclass;
  memset(&wndclass, 0, sizeof(wndclass));
  if (!GetClassInfo(GetModuleHandle(NULL), wndclass_name, &wndclass)) {
    wndclass.style         = CS_OWNDC | CS_HREDRAW | CS_VREDRAW;
    wndclass.lpfnWndProc   = window_proc;
    wndclass.cbWndExtra    = 0;
    wndclass.lpszClassName = wndclass_name;
    RegisterClass(&wndclass);
  }

  // Set appropriate win32 styles.
  const LONG win_styles =
      WS_CLIPSIBLINGS | WS_CLIPCHILDREN | WS_SIZEBOX | WS_CAPTION | WS_SYSMENU | WS_MAXIMIZEBOX;
  const LONG win_ex_styles = WS_EX_APPWINDOW | WS_EX_WINDOWEDGE;

  // Calculate total window rect size based on desired client rect size.
  RECT win_rect = {
      .left   = 0,
      .top    = 0,
      .right  = static_cast<LONG>(initial_framebuffer_width),
      .bottom = static_cast<LONG>(initial_framebuffer_height)};
  AdjustWindowRectEx(&win_rect, win_styles, FALSE, win_ex_styles);

  // Create the window.
  const HWND win = CreateWindowEx(
      win_ex_styles,
      wndclass_name,
      wndclass_name,
      win_styles,
      CW_USEDEFAULT,
      CW_USEDEFAULT,
      win_rect.right - win_rect.left,
      win_rect.bottom - win_rect.top,
      NULL,
      NULL,
      GetModuleHandle(NULL),
      NULL);

  // Set window title.
  SetWindowTextA(win, title);

  // Display window on screen.
  UpdateWindow(win);
  ShowWindow(win, SW_SHOW);
  BringWindowToTop(win);
  SetForegroundWindow(win);
  SetFocus(win);

  return reinterpret_cast<window>(win);
}

bool window_poll_events() {
  MSG  msg;
  bool should_quit = false;
  while (PeekMessage(&msg, NULL, 0, 0, PM_REMOVE)) {
    if (msg.message == WM_QUIT) { should_quit = true; }
    TranslateMessage(&msg);
    DispatchMessage(&msg);
  }
  return !should_quit;
}

void window_destroy(window win) {
  DestroyWindow(reinterpret_cast<HWND>(win));
}

bool window_is_closed(window win) {
  // TODO: check PID and class too.
  return !IsWindow(reinterpret_cast<HWND>(win));
}

uintptr_t window_native_handle(window win) {
  return win;
}

void window_get_size(window win, uint32_t* w, uint32_t* h) {
  RECT rect;
  GetClientRect(reinterpret_cast<HWND>(win), &rect);
  *h = rect.bottom - rect.top;
  *w = rect.right - rect.left;
}

}  // namespace ngf_samples