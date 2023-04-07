#include "doomkeys.h"
#include "doomgeneric.h"

#include <stdio.h>
#include <Windows.h>

#define BLOCKS_ROW_SIZE 8
#define BLOCK_SIZE (DOOMGENERIC_RESX / BLOCKS_ROW_SIZE)

static uint32_t blockBuffer[BLOCK_SIZE * BLOCK_SIZE] = { 0 };
static BITMAPINFO bmi = { sizeof(BITMAPINFOHEADER), BLOCK_SIZE, -BLOCK_SIZE, 1, 32 };
static NOTIFYICONDATA nid = { sizeof(NOTIFYICONDATA), 0, 0, NIF_ICON };
static ICONINFO ii = { TRUE };
static HHOOK hook = 0;

static HWND s_Hwnd = 0;
static HDC s_Hdc = 0;

#define KEYQUEUE_SIZE 16

static unsigned short s_KeyQueue[KEYQUEUE_SIZE];
static unsigned int s_KeyQueueWriteIndex = 0;
static unsigned int s_KeyQueueReadIndex = 0;

static unsigned char convertToDoomKey(unsigned char key)
{
	switch (key)
	{
	case VK_RETURN:
		key = KEY_ENTER;
		break;
	case VK_DELETE: // escape closes the tray
		key = KEY_ESCAPE;
		break;
	case VK_LEFT:
		key = KEY_LEFTARROW;
		break;
	case VK_RIGHT:
		key = KEY_RIGHTARROW;
		break;
	case VK_UP:
		key = KEY_UPARROW;
		break;
	case VK_DOWN:
		key = KEY_DOWNARROW;
		break;
	case VK_CONTROL:
		key = KEY_FIRE;
		break;
	case VK_SPACE:
		key = KEY_USE;
		break;
	case VK_SHIFT:
		key = KEY_RSHIFT;
		break;
	default:
		key = tolower(key);
		break;
	}

	return key;
}

static void addKeyToQueue(int pressed, unsigned char keyCode)
{
	unsigned char key = convertToDoomKey(keyCode);

	unsigned short keyData = (pressed << 8) | key;

	s_KeyQueue[s_KeyQueueWriteIndex] = keyData;
	s_KeyQueueWriteIndex++;
	s_KeyQueueWriteIndex %= KEYQUEUE_SIZE;
}

static void cleanup()
{
	for (nid.uID = 0; nid.uID < BLOCKS_ROW_SIZE * BLOCKS_ROW_SIZE; nid.uID++)
	{
		Shell_NotifyIcon(NIM_DELETE, &nid);
	}

	DeleteObject(ii.hbmMask);
	DeleteObject(ii.hbmColor);
}

BOOL WINAPI ctrlHandler(DWORD event)
{
	switch (event)
	{
	case CTRL_C_EVENT:
	case CTRL_BREAK_EVENT:
	case CTRL_CLOSE_EVENT:
		cleanup();
		PostQuitMessage(0);
		ExitProcess(0);
	default:
		break;
	}

	return FALSE;
}

LRESULT CALLBACK keyboardProc(const int code, const WPARAM wParam, const LPARAM lParam)
{
	switch (wParam)
	{
	case WM_KEYDOWN:
		addKeyToQueue(1, ((KBDLLHOOKSTRUCT *)lParam)->vkCode);
		break;
	case WM_KEYUP:
		addKeyToQueue(0, ((KBDLLHOOKSTRUCT *)lParam)->vkCode);
		break;
	default:
		break;
	}

	return CallNextHookEx(hook, code, wParam, lParam);
}

static LRESULT CALLBACK wndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
	switch (msg)
	{
	case WM_CLOSE:
		DestroyWindow(hwnd);
		break;
	case WM_DESTROY:
		cleanup();
		PostQuitMessage(0);
		ExitProcess(0);
		break;
	case WM_KEYDOWN:
		addKeyToQueue(1, wParam);
		break;
	case WM_KEYUP:
		addKeyToQueue(0, wParam);
		break;
	default:
		return DefWindowProcA(hwnd, msg, wParam, lParam);
	}
	return 0;
}

void DG_Init()
{
	// window creation
	const char windowClassName[] = "DoomWindowClass";
	const char windowTitle[] = "Doom";
	WNDCLASSEXA wc;

	wc.cbSize = sizeof(WNDCLASSEXA);
	wc.style = 0;
	wc.lpfnWndProc = wndProc;
	wc.cbClsExtra = 0;
	wc.cbWndExtra = 0;
	wc.hInstance = 0;
	wc.hIcon = 0;
	wc.hCursor = 0;
	wc.hbrBackground = 0;
	wc.lpszMenuName = 0;
	wc.lpszClassName = windowClassName;
	wc.hIconSm = 0;

	if (!RegisterClassExA(&wc))
	{
		printf("Window Registration Failed!");

		exit(-1);
	}

	s_Hwnd = CreateWindowExA(0, windowClassName, windowTitle, 0, 0, 0, 0, 0, 0, 0, 0, 0);

	if (!s_Hwnd)
	{
		printf("Window Creation Failed!");

		exit(-1);
	}

	s_Hdc = GetDC(s_Hwnd);

	memset(s_KeyQueue, 0, KEYQUEUE_SIZE * sizeof(unsigned short));

	ii.hbmMask = CreateBitmap(BLOCK_SIZE, BLOCK_SIZE, 1, 1, 0);
	ii.hbmColor = CreateCompatibleBitmap(s_Hdc, BLOCK_SIZE, BLOCK_SIZE);
	SetDIBits(s_Hdc, ii.hbmColor, 0, BLOCK_SIZE, blockBuffer, &bmi, DIB_RGB_COLORS);
	nid.hWnd = s_Hwnd;

	Shell_NotifyIcon(NIM_ADD, &nid);
	Shell_NotifyIcon(NIM_DELETE, &nid);	// HACK: the first icon added often has its position set wrong, so we delete it immediately

	for (nid.uID = 0; nid.uID < BLOCKS_ROW_SIZE * BLOCKS_ROW_SIZE; nid.uID++)
	{
		//nid.hIcon = CreateIconIndirect(&ii); // this usually reverses the order of icons
		Shell_NotifyIcon(NIM_ADD, &nid);
		//DestroyIcon(nid.hIcon);
	}
}

void DG_DrawFrame()
{
	MSG msg;
	memset(&msg, 0, sizeof(msg));

	while (PeekMessageA(&msg, 0, 0, 0, PM_REMOVE) > 0)
	{
		TranslateMessage(&msg);
		DispatchMessageA(&msg);
	}

	for (int i = 0; i < BLOCKS_ROW_SIZE; i++)
	{
		for (int j = 0; j < BLOCKS_ROW_SIZE; j++)
		{
			int blockStart = i * BLOCK_SIZE * DOOMGENERIC_RESX + j * BLOCK_SIZE;

			if (blockStart >= DOOMGENERIC_RESX * DOOMGENERIC_RESY)
				return; // no need to update empty icons

			// copy block pixels
			for (int k = 0; k < BLOCK_SIZE; k++)
			{
				int rowStart = blockStart + k * DOOMGENERIC_RESX;
				if (rowStart < DOOMGENERIC_RESX * DOOMGENERIC_RESY)
					memcpy(blockBuffer + k * BLOCK_SIZE, DG_ScreenBuffer + rowStart, BLOCK_SIZE * sizeof(uint32_t));
				else
					memset(blockBuffer + k * BLOCK_SIZE, 0, BLOCK_SIZE * sizeof(uint32_t));
			}

			// update icon
			SetDIBits(s_Hdc, ii.hbmColor, 0, BLOCK_SIZE, blockBuffer, &bmi, DIB_RGB_COLORS);
			nid.uID = i * BLOCKS_ROW_SIZE + j;
			nid.hIcon = CreateIconIndirect(&ii);
			Shell_NotifyIcon(NIM_MODIFY, &nid);
			DestroyIcon(nid.hIcon);
		}
	}
}

void DG_SleepMs(uint32_t ms)
{
	Sleep(ms);
}

uint32_t DG_GetTicksMs()
{
	return GetTickCount();
}

int DG_GetKey(int *pressed, unsigned char *doomKey)
{
	if (s_KeyQueueReadIndex == s_KeyQueueWriteIndex)
	{
		//key queue is empty

		return 0;
	}
	else
	{
		unsigned short keyData = s_KeyQueue[s_KeyQueueReadIndex];
		s_KeyQueueReadIndex++;
		s_KeyQueueReadIndex %= KEYQUEUE_SIZE;

		*pressed = keyData >> 8;
		*doomKey = keyData & 0xFF;

		return 1;
	}
}

void DG_SetWindowTitle(const char *title)
{
	if (s_Hwnd)
	{
		SetWindowTextA(s_Hwnd, title);
	}
}

int main(int argc, char **argv)
{
	SetConsoleCtrlHandler(ctrlHandler, TRUE);
	hook = SetWindowsHookEx(WH_KEYBOARD_LL, keyboardProc, NULL, 0);

	doomgeneric_Create(argc, argv);

	for (int i = 0; ; i++)
	{
		doomgeneric_Tick();
	}

	return 0;
}