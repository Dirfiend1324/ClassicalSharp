#include "Window.h"
#if CC_BUILD_WIN
#include "Platform.h"
#include "Input.h"
#include "Event.h"
#include "ErrorHandler.h"
#define WIN32_LEAN_AND_MEAN
#define NOSERVICE
#define NOMCX
#define NOIME
#define _WIN32_WINNT 0x0500
#include <windows.h>

#define win_Style WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN
#define win_ClassName L"ClassiCube_Window"
#define RECT_WIDTH(rect) (rect.right - rect.left)
#define RECT_HEIGHT(rect) (rect.bottom - rect.top)

HINSTANCE win_Instance;
HWND win_Handle;
HDC win_DC;
UInt8 win_State = WINDOW_STATE_NORMAL;
bool invisible_since_creation; /* Set by WindowsMessage.CREATE and consumed by Visible = true (calls BringWindowToFront) */
Int32 suppress_resize; /* Used in WindowBorder and WindowState in order to avoid rapid, consecutive resize events */
struct Rectangle2D previous_bounds; /* Used to restore previous size when leaving fullscreen mode */

static struct Rectangle2D Window_FromRect(RECT rect) {
	struct Rectangle2D r;
	r.X = rect.left; r.Y = rect.top;
	r.Width = RECT_WIDTH(rect);
	r.Height = RECT_HEIGHT(rect);
	return r;
}


void Window_Destroy(void) {
	if (!Window_Exists) return;
	DestroyWindow(win_Handle);
	Window_Exists = false;
}

static void Window_ResetWindowState(void) {
	suppress_resize++;
	Window_SetWindowState(WINDOW_STATE_NORMAL);
	Window_ProcessEvents();
	suppress_resize--;
}

bool win_hiddenBorder;
static void Window_DoSetHiddenBorder(bool value) {
	if (win_hiddenBorder == value) return;

	/* We wish to avoid making an invisible window visible just to change the border.
	However, it's a good idea to make a visible window invisible temporarily, to
	avoid garbage caused by the border change. */
	bool was_visible = Window_GetVisible();

	/* To ensure maximized/minimized windows work correctly, reset state to normal,
	change the border, then go back to maximized/minimized. */
	UInt8 state = win_State;
	Window_ResetWindowState();
	DWORD style = WS_CLIPCHILDREN | WS_CLIPSIBLINGS;
	style |= (value ? WS_POPUP : WS_OVERLAPPEDWINDOW);

	/* Make sure client size doesn't change when changing the border style.*/
	RECT rect;
	rect.left = Window_Bounds.X; rect.top = Window_Bounds.Y;
	rect.right = rect.left + Window_Bounds.Width;
	rect.bottom = rect.top + Window_Bounds.Height;
	AdjustWindowRect(&rect, style, false);

	/* This avoids leaving garbage on the background window. */
	if (was_visible) Window_SetVisible(false);

	SetWindowLongW(win_Handle, GWL_STYLE, style);
	SetWindowPos(win_Handle, NULL, 0, 0, RECT_WIDTH(rect), RECT_HEIGHT(rect),
		SWP_NOMOVE | SWP_NOZORDER | SWP_FRAMECHANGED);

	/* Force window to redraw update its borders, but only if it's
	already visible (invisible windows will change borders when
	they become visible, so no need to make them visiable prematurely).*/
	if (was_visible) Window_SetVisible(true);

	Window_SetWindowState(state);
}

static void Window_SetHiddenBorder(bool hidden) {
	suppress_resize++;
	Window_DoSetHiddenBorder(hidden);
	Window_ProcessEvents();
	suppress_resize--;
}

static Key Window_MapKey(WPARAM key) {
	if (key >= VK_F1 && key <= VK_F24) { return Key_F1 + (key - VK_F1); }
	if (key >= '0' && key <= '9') { return Key_0 + (key - '0'); }
	if (key >= 'A' && key <= 'Z') { return Key_A + (key - 'A'); }

	if (key >= VK_NUMPAD0 && key <= VK_NUMPAD9) { 
		return Key_Keypad0 + (key - VK_NUMPAD0); 
	}

	switch (key) {
	case VK_ESCAPE: return Key_Escape;
	case VK_TAB: return Key_Tab;
	case VK_CAPITAL: return Key_CapsLock;
	case VK_LCONTROL: return Key_ControlLeft;
	case VK_LSHIFT: return Key_ShiftLeft;
	case VK_LWIN: return Key_WinLeft;
	case VK_LMENU: return Key_AltLeft;
	case VK_SPACE: return Key_Space;
	case VK_RMENU: return Key_AltRight;
	case VK_RWIN: return Key_WinRight;
	case VK_APPS: return Key_Menu;
	case VK_RCONTROL: return Key_ControlRight;
	case VK_RSHIFT: return Key_ShiftRight;
	case VK_RETURN: return Key_Enter;
	case VK_BACK: return Key_BackSpace;

	case VK_OEM_1: return Key_Semicolon;      /* Varies by keyboard: return ;: on Win2K/US */
	case VK_OEM_2: return Key_Slash;          /* Varies by keyboard: return /? on Win2K/US */
	case VK_OEM_3: return Key_Tilde;          /* Varies by keyboard: return `~ on Win2K/US */
	case VK_OEM_4: return Key_BracketLeft;    /* Varies by keyboard: return [{ on Win2K/US */
	case VK_OEM_5: return Key_BackSlash;      /* Varies by keyboard: return \| on Win2K/US */
	case VK_OEM_6: return Key_BracketRight;   /* Varies by keyboard: return ]} on Win2K/US */
	case VK_OEM_7: return Key_Quote;          /* Varies by keyboard: return '" on Win2K/US */
	case VK_OEM_PLUS: return Key_Plus;        /* Invariant: +							   */
	case VK_OEM_COMMA: return Key_Comma;      /* Invariant: : return					   */
	case VK_OEM_MINUS: return Key_Minus;      /* Invariant: -							   */
	case VK_OEM_PERIOD: return Key_Period;    /* Invariant: .							   */

	case VK_HOME: return Key_Home;
	case VK_END: return Key_End;
	case VK_DELETE: return Key_Delete;
	case VK_PRIOR: return Key_PageUp;
	case VK_NEXT: return Key_PageDown;
	case VK_PRINT: return Key_PrintScreen;
	case VK_PAUSE: return Key_Pause;
	case VK_NUMLOCK: return Key_NumLock;

	case VK_SCROLL: return Key_ScrollLock;
	case VK_SNAPSHOT: return Key_PrintScreen;
	case VK_INSERT: return Key_Insert;

	case VK_DECIMAL: return Key_KeypadDecimal;
	case VK_ADD: return Key_KeypadAdd;
	case VK_SUBTRACT: return Key_KeypadSubtract;
	case VK_DIVIDE: return Key_KeypadDivide;
	case VK_MULTIPLY: return Key_KeypadMultiply;

	case VK_UP: return Key_Up;
	case VK_DOWN: return Key_Down;
	case VK_LEFT: return Key_Left;
	case VK_RIGHT: return Key_Right;
	}
	return Key_None;
}

static void Window_UpdateClientSize(HWND handle) {
	RECT rect;
	GetClientRect(handle, &rect);
	Window_ClientSize.Width  = RECT_WIDTH(rect);
	Window_ClientSize.Height = RECT_HEIGHT(rect);
}

static LRESULT CALLBACK Window_Procedure(HWND handle, UINT message, WPARAM wParam, LPARAM lParam) {
	switch (message) {
	case WM_ACTIVATE:
	{
		bool wasFocused = Window_Focused;
		Window_Focused = LOWORD(wParam) != 0;
		if (Window_Focused != wasFocused) {
			Event_RaiseVoid(&WindowEvents_FocusChanged);
		}
	} break;

	case WM_ENTERMENULOOP:
	case WM_ENTERSIZEMOVE:
	case WM_EXITMENULOOP:
	case WM_EXITSIZEMOVE:
		break;

	case WM_ERASEBKGND:
		Event_RaiseVoid(&WindowEvents_Redraw);
		return 1;

	case WM_WINDOWPOSCHANGED:
	{
		WINDOWPOS* pos = (WINDOWPOS*)lParam;
		if (pos->hwnd == win_Handle) {
			struct Point2D loc = Window_GetLocation();
			if (loc.X != pos->x || loc.Y != pos->y) {
				Window_Bounds.X = pos->x; Window_Bounds.Y = pos->y;
				Event_RaiseVoid(&WindowEvents_Moved);
			}

			struct Size2D size = Window_GetSize();
			if (size.Width != pos->cx || size.Height != pos->cy) {
				Window_Bounds.Width = pos->cx; Window_Bounds.Height = pos->cy;
				Window_UpdateClientSize(handle);

				SetWindowPos(win_Handle, NULL,
					Window_Bounds.X, Window_Bounds.Y, Window_Bounds.Width, Window_Bounds.Height,
					SWP_NOZORDER | SWP_NOOWNERZORDER | SWP_NOACTIVATE | SWP_NOSENDCHANGING);

				if (suppress_resize <= 0) {
					Event_RaiseVoid(&WindowEvents_Resized);
				}
			}
		}
	} break;

	case WM_STYLECHANGED:
		if (wParam == GWL_STYLE) {
			DWORD style = ((STYLESTRUCT*)lParam)->styleNew;
			if (style & WS_POPUP) {
				win_hiddenBorder = true;
			} else if (style & WS_THICKFRAME) {
				win_hiddenBorder = false;
			}
		}
		break;

	case WM_SIZE:
	{
		UInt8 new_state = win_State;
		switch (wParam) {
		case SIZE_RESTORED:  new_state = WINDOW_STATE_NORMAL; break;
		case SIZE_MINIMIZED: new_state = WINDOW_STATE_MINIMISED; break;
		case SIZE_MAXIMIZED: new_state = win_hiddenBorder ? WINDOW_STATE_FULLSCREEN : WINDOW_STATE_MAXIMISED; break;
		}

		if (new_state != win_State) {
			win_State = new_state;
			Event_RaiseVoid(&WindowEvents_WindowStateChanged);
		}
	} break;


	case WM_CHAR:
	{
		UChar keyChar;
		if (Convert_TryUnicodeToCP437((UInt16)wParam, &keyChar)) {
			Event_RaiseInt(&KeyEvents_Press, keyChar);
		}
	} break;

	case WM_MOUSEMOVE:
	{
		/* set before position change, in case mouse buttons changed when outside window */
		Mouse_SetPressed(MouseButton_Left,   (wParam & 0x01) != 0);
		Mouse_SetPressed(MouseButton_Right,  (wParam & 0x02) != 0);
		Mouse_SetPressed(MouseButton_Middle, (wParam & 0x10) != 0);
		/* TODO: do we need to set XBUTTON1 / XBUTTON 2 here */

		WORD mouse_x = LOWORD(lParam);
		WORD mouse_y = HIWORD(lParam);
		Mouse_SetPosition(mouse_x, mouse_y);
	} break;

	case WM_MOUSEWHEEL:
	{
		Real32 wheel_delta = ((short)HIWORD(wParam)) / (Real32)WHEEL_DELTA;
		Mouse_SetWheel(Mouse_Wheel + wheel_delta);
	} return 0;

	case WM_LBUTTONDOWN:
		Mouse_SetPressed(MouseButton_Left, true);
		break;
	case WM_MBUTTONDOWN:
		Mouse_SetPressed(MouseButton_Middle, true);
		break;
	case WM_RBUTTONDOWN:
		Mouse_SetPressed(MouseButton_Right, true);
		break;
	case WM_XBUTTONDOWN:
		Key_SetPressed(HIWORD(wParam) == 1 ? Key_XButton1 : Key_XButton2, true);
		break;
	case WM_LBUTTONUP:
		Mouse_SetPressed(MouseButton_Left, false);
		break;
	case WM_MBUTTONUP:
		Mouse_SetPressed(MouseButton_Middle, false);
		break;
	case WM_RBUTTONUP:
		Mouse_SetPressed(MouseButton_Right, false);
		break;
	case WM_XBUTTONUP:
		Key_SetPressed(HIWORD(wParam) == 1 ? Key_XButton1 : Key_XButton2, false);
		break;

	case WM_KEYDOWN:
	case WM_KEYUP:
	case WM_SYSKEYDOWN:
	case WM_SYSKEYUP:
	{
		bool pressed = message == WM_KEYDOWN || message == WM_SYSKEYDOWN;
		/* Shift/Control/Alt behave strangely when e.g. ShiftRight is held down and ShiftLeft is pressed
		and released. It looks like neither key is released in this case, or that the wrong key is
		released in the case of Control and Alt.
		To combat this, we are going to release both keys when either is released. Hacky, but should work.
		Win95 does not distinguish left/right key constants (GetAsyncKeyState returns 0).
		In this case, both keys will be reported as pressed.	*/
		bool extended = (lParam & (1UL << 24)) != 0;

		bool lShiftDown, rShiftDown;
		Key mappedKey;
		switch (wParam)
		{
		case VK_SHIFT:
			/* The behavior of this key is very strange. Unlike Control and Alt, there is no extended bit
			to distinguish between left and right keys. Moreover, pressing both keys and releasing one
			may result in both keys being held down (but not always).*/
			lShiftDown = ((USHORT)GetKeyState(VK_LSHIFT)) >> 15;
			rShiftDown = ((USHORT)GetKeyState(VK_RSHIFT)) >> 15;

			if (!pressed || lShiftDown != rShiftDown) {
				Key_SetPressed(Key_ShiftLeft, lShiftDown);
				Key_SetPressed(Key_ShiftRight, rShiftDown);
			}
			return 0;

		case VK_CONTROL:
			if (extended) {
				Key_SetPressed(Key_ControlRight, pressed);
			} else {
				Key_SetPressed(Key_ControlLeft, pressed);
			}
			return 0;

		case VK_MENU:
			if (extended) {
				Key_SetPressed(Key_AltRight, pressed);
			} else {
				Key_SetPressed(Key_AltLeft, pressed);
			}
			return 0;

		case VK_RETURN:
			if (extended) {
				Key_SetPressed(Key_KeypadEnter, pressed);
			} else {
				Key_SetPressed(Key_Enter, pressed);
			}
			return 0;

		default:
			mappedKey = Window_MapKey(wParam);
			if (mappedKey != Key_None) {
				Key_SetPressed(mappedKey, pressed);
			}
			return 0;
		}
	} break;

	case WM_SYSCHAR:
		return 0;

	case WM_KILLFOCUS:
		Key_Clear();
		break;


	case WM_CREATE:
	{
		CREATESTRUCT* cs = (CREATESTRUCT*)lParam;
		if (!cs->hwndParent) {
			Window_Bounds.X = cs->x;      Window_Bounds.Y = cs->y;
			Window_Bounds.Width = cs->cx; Window_Bounds.Height = cs->cy;
			Window_UpdateClientSize(handle);
			invisible_since_creation = true;
		}
	} break;

	case WM_CLOSE:
		Event_RaiseVoid(&WindowEvents_Closing);
		Window_Destroy();
		break;

	case WM_DESTROY:
		Window_Exists = false;
		UnregisterClassW(win_ClassName, win_Instance);
		if (win_DC) ReleaseDC(win_Handle, win_DC);
		Event_RaiseVoid(&WindowEvents_Closed);
		break;
	}
	return DefWindowProcW(handle, message, wParam, lParam);
}


void Window_Create(Int32 x, Int32 y, Int32 width, Int32 height, STRING_REF String* title, 
	struct GraphicsMode* mode, struct DisplayDevice* device) {
	win_Instance = GetModuleHandleW(NULL);
	/* TODO: UngroupFromTaskbar(); */

	/* Find out the final window rectangle, after the WM has added its chrome (titlebar, sidebars etc). */
	RECT rect; rect.left = x; rect.top = y; 
	rect.right = x + width; rect.bottom = y + height;
	AdjustWindowRect(&rect, win_Style, false);

	WNDCLASSEXW wc = { 0 };
	wc.cbSize = sizeof(WNDCLASSEXW);
	wc.style = CS_OWNDC;
	wc.hInstance = win_Instance;
	wc.lpfnWndProc = Window_Procedure;
	wc.lpszClassName = win_ClassName;
	/* TODO: Set window icons here */
	wc.hCursor = LoadCursorW(NULL, IDC_ARROW);

	ATOM atom = RegisterClassExW(&wc);
	if (atom == 0) {
		ErrorHandler_FailWithCode(GetLastError(), "Failed to register window class");
	}
	WCHAR data[512]; Platform_ConvertString(data, title);

	win_Handle = CreateWindowExW(0, atom, data, win_Style,
		rect.left, rect.top, RECT_WIDTH(rect), RECT_HEIGHT(rect),
		NULL, NULL, win_Instance, NULL);

	if (!win_Handle) {
		ErrorHandler_FailWithCode(GetLastError(), "Failed to create window");
	}
	win_DC = GetDC(win_Handle);
	if (!win_DC) {
		ErrorHandler_FailWithCode(GetLastError(), "Failed to get device context");
	}
	Window_Exists = true;
}

void Window_GetClipboardText(STRING_TRANSIENT String* value) {
	/* retry up to 10 times*/
	Int32 i;
	String_Clear(value);

	for (i = 0; i < 10; i++) {
		if (!OpenClipboard(win_Handle)) {
			Thread_Sleep(100);
			continue;
		}

		bool isUnicode = true;
		HANDLE hGlobal = GetClipboardData(CF_UNICODETEXT);
		if (!hGlobal) {
			hGlobal = GetClipboardData(CF_TEXT);
			isUnicode = false;
		}
		if (!hGlobal) { CloseClipboard(); return; }
		LPVOID src = GlobalLock(hGlobal);

		UChar c;
		if (isUnicode) {
			UInt16* text = (UInt16*)src;
			for (; *text; text++) {
				if (Convert_TryUnicodeToCP437(*text, &c)) String_Append(value, c);
			}
		} else {
			UChar* text = (UChar*)src;
			for (; *text; text++) {
				if (Convert_TryUnicodeToCP437(*text, &c)) String_Append(value, c);
			}
		}

		GlobalUnlock(hGlobal);
		CloseClipboard();
		return;
	}
}

void Window_SetClipboardText(STRING_PURE String* value) {
	/* retry up to 10 times*/
	Int32 i;
	for (i = 0; i < 10; i++) {
		if (!OpenClipboard(win_Handle)) {
			Thread_Sleep(100);
			continue;
		}

		HANDLE hGlobal = GlobalAlloc(GMEM_MOVEABLE, String_BufferSize(value->length) * sizeof(UInt16));
		if (!hGlobal) { CloseClipboard(); return; }

		LPVOID dst = GlobalLock(hGlobal);
		UInt16* text = (UInt16*)dst;
		for (i = 0; i < value->length; i++) {
			*text = Convert_CP437ToUnicode(value->buffer[i]); text++;
		}
		*text = '\0';

		GlobalUnlock(hGlobal);
		EmptyClipboard();
		SetClipboardData(CF_UNICODETEXT, hGlobal);
		CloseClipboard();
		return;
	}
}


void Window_SetBounds(struct Rectangle2D rect) {
	/* Note: the bounds variable is updated when the resize/move message arrives.*/
	SetWindowPos(win_Handle, NULL, rect.X, rect.Y, rect.Width, rect.Height, 0);
}

void Window_SetLocation(struct Point2D point) {
	SetWindowPos(win_Handle, NULL, point.X, point.Y, 0, 0, SWP_NOSIZE);
}

void Window_SetSize(struct Size2D size) {
	SetWindowPos(win_Handle, NULL, 0, 0, size.Width, size.Height, SWP_NOMOVE);
}

void Window_SetClientSize(struct Size2D size) {
	DWORD style = GetWindowLongW(win_Handle, GWL_STYLE);
	RECT rect; rect.left = 0; rect.top = 0;
	rect.right = size.Width; rect.bottom = size.Height;

	AdjustWindowRect(&rect, style, false);
	struct Size2D adjSize = { RECT_WIDTH(rect), RECT_HEIGHT(rect) };
	Window_SetSize(adjSize);
}

void* Window_GetWindowHandle(void) { return win_Handle; }

bool Window_GetVisible(void) { return IsWindowVisible(win_Handle); }
void Window_SetVisible(bool visible) {
	if (visible) {
		ShowWindow(win_Handle, SW_SHOW);
		if (invisible_since_creation) {
			BringWindowToTop(win_Handle);
			SetForegroundWindow(win_Handle);
		}
	} else {
		ShowWindow(win_Handle, SW_HIDE);
	}
}


void Window_Close(void) {
	PostMessageW(win_Handle, WM_CLOSE, 0, 0);
}

UInt8 Window_GetWindowState(void) { return win_State; }
void Window_SetWindowState(UInt8 state) {
	if (win_State == state) return;

	DWORD command = 0;
	bool exiting_fullscreen = false;

	switch (state) {
	case WINDOW_STATE_NORMAL:
		command = SW_RESTORE;

		/* If we are leaving fullscreen mode we need to restore the border. */
		if (win_State == WINDOW_STATE_FULLSCREEN)
			exiting_fullscreen = true;
		break;

	case WINDOW_STATE_MAXIMISED:
		/* Reset state to avoid strange interactions with fullscreen/minimized windows. */
		Window_ResetWindowState();
		command = SW_MAXIMIZE;
		break;

	case WINDOW_STATE_MINIMISED:
		command = SW_MINIMIZE;
		break;

	case WINDOW_STATE_FULLSCREEN:
		/* We achieve fullscreen by hiding the window border and sending the MAXIMIZE command.
		We cannot use the WindowState.Maximized directly, as that will not send the MAXIMIZE
		command for windows with hidden borders. */

		/* Reset state to avoid strange side-effects from maximized/minimized windows. */
		Window_ResetWindowState();
		previous_bounds = Window_Bounds;
		Window_SetHiddenBorder(true);

		command = SW_MAXIMIZE;
		SetForegroundWindow(win_Handle);
		break;
	}

	if (command != 0) ShowWindow(win_Handle, command);

	/* Restore previous window border or apply pending border change when leaving fullscreen mode. */
	if (exiting_fullscreen) Window_SetHiddenBorder(false);

	/* Restore previous window size/location if necessary */
	if (command == SW_RESTORE && !Rectangle2D_Equals(previous_bounds, Rectangle2D_Empty)) {
		Window_SetBounds(previous_bounds);
		previous_bounds = Rectangle2D_Empty;
	}
}

struct Point2D Window_PointToClient(struct Point2D point) {
	if (!ScreenToClient(win_Handle, &point)) {
		ErrorHandler_FailWithCode(GetLastError(), "Converting point from client to screen coordinates");
	}
	return point;
}

struct Point2D Window_PointToScreen(struct Point2D point) {
	if (!ClientToScreen(win_Handle, &point)) {
		ErrorHandler_FailWithCode(GetLastError(), "Converting point from screen to client coordinates");
	}
	return point;
}

void Window_ProcessEvents(void) {
	MSG msg;
	while (PeekMessageW(&msg, NULL, 0, 0, 1)) {
		TranslateMessage(&msg);
		DispatchMessageW(&msg);
	}

	HWND foreground = GetForegroundWindow();
	if (foreground) {
		Window_Focused = foreground == win_Handle;
	}
}

struct Point2D Window_GetDesktopCursorPos(void) {
	POINT p; GetCursorPos(&p);
	return Point2D_Make(p.x, p.y);
}
void Window_SetDesktopCursorPos(struct Point2D point) {
	SetCursorPos(point.X, point.Y);
}

bool win_cursorVisible = true;
bool Window_GetCursorVisible(void) { return win_cursorVisible; }
void Window_SetCursorVisible(bool visible) {
	win_cursorVisible = visible;
	ShowCursor(visible ? 1 : 0);
}

#if !CC_BUILD_D3D9

void GLContext_SelectGraphicsMode(struct GraphicsMode mode) {
	struct ColorFormat color = mode.Format;

	PIXELFORMATDESCRIPTOR pfd = { 0 };
	pfd.nSize = sizeof(PIXELFORMATDESCRIPTOR);
	pfd.nVersion = 1;
	pfd.dwFlags = PFD_SUPPORT_OPENGL | PFD_DRAW_TO_WINDOW;
	/* TODO: PFD_SUPPORT_COMPOSITION FLAG? CHECK IF IT WORKS ON XP */
	pfd.cColorBits = (UInt8)(color.R + color.G + color.B);

	pfd.iPixelType = color.IsIndexed ? PFD_TYPE_COLORINDEX : PFD_TYPE_RGBA;
	pfd.cRedBits   = color.R;
	pfd.cGreenBits = color.G;
	pfd.cBlueBits  = color.B;
	pfd.cAlphaBits = color.A;

	pfd.cDepthBits   = mode.DepthBits;
	pfd.cStencilBits = mode.StencilBits;
	if (mode.DepthBits <= 0) pfd.dwFlags |= PFD_DEPTH_DONTCARE;
	if (mode.Buffers > 1)    pfd.dwFlags |= PFD_DOUBLEBUFFER;

	int modeIndex = ChoosePixelFormat(win_DC, &pfd);
	if (modeIndex == 0) {
		ErrorHandler_Fail("Requested graphics mode not available");
	}

	Mem_Set(&pfd, 0, sizeof(PIXELFORMATDESCRIPTOR));
	pfd.nSize = sizeof(PIXELFORMATDESCRIPTOR);
	pfd.nVersion = 1;

	DescribePixelFormat(win_DC, modeIndex, pfd.nSize, &pfd);
	if (!SetPixelFormat(win_DC, modeIndex, &pfd)) {
		ErrorHandler_FailWithCode(GetLastError(), "SetPixelFormat failed");
	}
}

HGLRC ctx_Handle;
HDC ctx_DC;
typedef BOOL (WINAPI *FN_WGLSWAPINTERVAL)(int interval);
FN_WGLSWAPINTERVAL wglSwapIntervalEXT;
bool ctx_supports_vSync;

void GLContext_Init(struct GraphicsMode mode) {
	GLContext_SelectGraphicsMode(mode);
	ctx_Handle = wglCreateContext(win_DC);
	if (!ctx_Handle) {
		ctx_Handle = wglCreateContext(win_DC);
	}
	if (!ctx_Handle) {
		ErrorHandler_FailWithCode(GetLastError(), "Failed to create OpenGL context");
	}

	if (!wglMakeCurrent(win_DC, ctx_Handle)) {
		ErrorHandler_FailWithCode(GetLastError(), "Failed to make OpenGL context current");
	}

	ctx_DC = wglGetCurrentDC();
	wglSwapIntervalEXT = (FN_WGLSWAPINTERVAL)GLContext_GetAddress("wglSwapIntervalEXT");
	ctx_supports_vSync = wglSwapIntervalEXT != NULL;
}

void GLContext_Update(void) { }
void GLContext_Free(void) {
	if (!wglDeleteContext(ctx_Handle)) {
		ErrorHandler_FailWithCode(GetLastError(), "Failed to destroy OpenGL context");
	}
	ctx_Handle = NULL;
}

void* GLContext_GetAddress(const UChar* function) {
	void* address = wglGetProcAddress(function);
	return GLContext_IsInvalidAddress(address) ? NULL : address;
}

void GLContext_SwapBuffers(void) {
	if (!SwapBuffers(ctx_DC)) {
		ErrorHandler_FailWithCode(GetLastError(), "Failed to swap buffers");
	}
}

void GLContext_SetVSync(bool enabled) {
	if (ctx_supports_vSync) wglSwapIntervalEXT(enabled);
}
#endif
#endif
