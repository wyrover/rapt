/* rapt - RichieSam's Adventures in Path Tracing
 *
 * rapt is the legal property of Adrian Astley
 * Copyright Adrian Astley 2015
 */

#include "engine/rapt_engine.h"

#include "graphics/d3d_util.h"


LRESULT CALLBACK MainWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
	return g_engine->MsgProc(hwnd, msg, wParam, lParam);
}

#define WINDOW_CLASS_NAME L"raptEngineWindow"


namespace Engine {

RAPTEngine::RAPTEngine(HINSTANCE hinstance)
		: m_hinstance(hinstance),
		  m_mainWndCaption(WINDOW_CLASS_NAME),
		  m_appPaused(false),
		  m_isMinOrMaximized(false),
		  m_fullscreen(false),
		  m_resizing(false),
		  m_device(nullptr),
		  m_immediateContext(nullptr),
		  m_swapChain(nullptr),
		  m_d3dInitialized(false) {
	// Get a pointer to the application object so we can forward Windows messages to 
	// the object's window procedure through the global window procedure.
	g_engine = this;
}

RAPTEngine::~RAPTEngine() {
	g_engine = nullptr;
}

bool RAPTEngine::Initialize(LPCTSTR mainWndCaption, uint32 screenWidth, uint32 screenHeight, bool fullscreen) {
	m_mainWndCaption = mainWndCaption;
	m_clientWidth = screenWidth;
	m_clientHeight = screenHeight;
	m_fullscreen = fullscreen;

	// Initialize the window
	InitializeWindow();

	// Create the device and device context.
	UINT createDeviceFlags = 0;
#if defined(DEBUG) || defined(_DEBUG)
	createDeviceFlags |= D3D11_CREATE_DEVICE_DEBUG;
#endif

	D3D_FEATURE_LEVEL featureLevel;
	HRESULT hr = D3D11CreateDevice(NULL,
								   D3D_DRIVER_TYPE_HARDWARE,
								   NULL,
								   createDeviceFlags,
								   NULL, 0,
								   D3D11_SDK_VERSION,
								   &m_device,
								   &featureLevel,
								   &m_immediateContext);

	if (FAILED(hr)) {
		DXTRACE_ERR_MSGBOX(L"D3D11CreateDevice Failed.", hr);
		return false;
	}

	// Describe the swap chain
	DXGI_SWAP_CHAIN_DESC sd;
	sd.BufferDesc.Width = m_clientWidth;
	sd.BufferDesc.Height = m_clientHeight;
	sd.BufferDesc.RefreshRate.Numerator = 60;
	sd.BufferDesc.RefreshRate.Denominator = 1;
	sd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
	sd.BufferDesc.ScanlineOrdering = DXGI_MODE_SCANLINE_ORDER_UNSPECIFIED;
	sd.BufferDesc.Scaling = DXGI_MODE_SCALING_UNSPECIFIED;
	sd.SampleDesc.Count = 1;
	sd.SampleDesc.Quality = 0;
	sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
	sd.BufferCount = 1;
	sd.OutputWindow = m_hwnd;
	sd.Windowed = !fullscreen;
	sd.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;
	sd.Flags = 0;

	IDXGIDevice *dxgiDevice;
	HR(m_device->QueryInterface(__uuidof(IDXGIDevice), (void **)&dxgiDevice));

	IDXGIAdapter *dxgiAdapter;
	HR(dxgiDevice->GetParent(__uuidof(IDXGIAdapter), (void **)&dxgiAdapter));

	IDXGIFactory *dxgiFactory;
	HR(dxgiAdapter->GetParent(__uuidof(IDXGIFactory), (void **)&dxgiFactory));

	HR(dxgiFactory->CreateSwapChain((IUnknown *)m_device, &sd, &m_swapChain));

	// Cleanup
	ReleaseCOM(dxgiFactory);
	ReleaseCOM(dxgiAdapter);
	ReleaseCOM(dxgiDevice);

	m_d3dInitialized = true;

	// The remaining steps that need to be carried out for d3d creation
	// also need to be executed every time the window is resized.  So
	// just call the OnResize method here to avoid code duplication.
	OnResize();

	return true;
}

void RAPTEngine::Shutdown() {
	// Shutdown in reverse order
	ReleaseCOM(m_swapChain);

	// Restore all default settings.
	if (m_immediateContext) {
		m_immediateContext->ClearState();
	}

	ReleaseCOM(m_immediateContext);
	ReleaseCOM(m_device);

	// Shutdown the window.
	ShutdownWindow();

	g_engine = nullptr;
}

void RAPTEngine::Run() {
	MSG msg;
	// Initialize the message structure.
	ZeroMemory(&msg, sizeof(MSG));


	bool done = false;

	// Loop until there is a quit message from the window or the user.
	while (!done) {
		// Handle the windows messages
		// *ALL* messages must be handled before game logic is done
		// This allows for the highest response time, but lags the game
		// during high message count.
		// TODO: Add logic to only allow a certain amount of low priority messages through each frame and buffer the rest
		if (PeekMessage(&msg, NULL, 0, 0, PM_REMOVE)) {
			// If windows signals to end the application then exit out.
			if (msg.message == WM_QUIT) {
				done = true;
			}

			TranslateMessage(&msg);
			DispatchMessage(&msg);
		} else {
			// Otherwise do the frame processing
			DrawFrame();
		}
	}

	return;
}

LRESULT RAPTEngine::MsgProc(HWND hwnd, uint msg, WPARAM wParam, LPARAM lParam) {
	switch (msg) {
		// WM_SIZE is sent when the user resizes the window.
	case WM_SIZE:
		// Save the new client area dimensions.
		m_clientWidth = LOWORD(lParam);
		m_clientHeight = HIWORD(lParam);
		if (wParam == SIZE_MINIMIZED) {
			m_isMinOrMaximized = true;
		} else if (wParam == SIZE_MAXIMIZED) {
			m_isMinOrMaximized = true;
			OnResize();
		} else if (wParam == SIZE_RESTORED) {
			// Restoring from minimized state?
			if (m_isMinOrMaximized) {
				m_isMinOrMaximized = false;
				OnResize();
			} else if (m_resizing) {
				// If user is dragging the resize bars, we do not resize
				// the buffers here because as the user continuously
				// drags the resize bars, a stream of WM_SIZE messages are
				// sent to the window, and it would be pointless (and slow)
				// to resize for each WM_SIZE message received from dragging
				// the resize bars.  So instead, we reset after the user is
				// done resizing the window and releases the resize bars, which
				// sends a WM_EXITSIZEMOVE message.
			} else {
				// API call such as SetWindowPos or mSwapChain->SetFullscreenState.
				OnResize();
			}
		}

		return 0;

		// WM_EXITSIZEMOVE is sent when the user grabs the resize bars.
	case WM_ENTERSIZEMOVE:
		m_resizing = true;
		return 0;

		// WM_EXITSIZEMOVE is sent when the user releases the resize bars.
		// Here we reset everything based on the new window dimensions.
	case WM_EXITSIZEMOVE:
		m_resizing = false;
		OnResize();
		return 0;

		// WM_DESTROY is sent when the window is being destroyed.
	case WM_DESTROY:
		PostQuitMessage(0);
		return 0;

		// The WM_MENUCHAR message is sent when a menu is active and the user presses
		// a key that does not correspond to any mnemonic or accelerator key.
	case WM_MENUCHAR:
		// Don't beep when we alt-enter.
		return MAKELRESULT(0, MNC_CLOSE);

		// Catch this message so to prevent the window from becoming too small.
	case WM_GETMINMAXINFO:
		((MINMAXINFO *)lParam)->ptMinTrackSize.x = 200;
		((MINMAXINFO *)lParam)->ptMinTrackSize.y = 200;
		return 0;

	case WM_LBUTTONDOWN:
	case WM_MBUTTONDOWN:
	case WM_RBUTTONDOWN:
		MouseDown(wParam, GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));
		return 0;
	case WM_LBUTTONUP:
	case WM_MBUTTONUP:
	case WM_RBUTTONUP:
		MouseUp(wParam, GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));
		return 0;
	case WM_MOUSEMOVE:
		MouseMove(wParam, GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));
		return 0;
	case WM_MOUSEWHEEL:
		MouseWheel(GET_WHEEL_DELTA_WPARAM(wParam));
		return 0;
	case WM_CHAR:
		CharacterInput(LOWORD(wParam));
		return 0;
	}

	return DefWindowProc(hwnd, msg, wParam, lParam);
}

void RAPTEngine::InitializeWindow() {
	WNDCLASS wc;
	wc.style = CS_HREDRAW | CS_VREDRAW;
	wc.lpfnWndProc = MainWndProc;
	wc.cbClsExtra = 0;
	wc.cbWndExtra = 0;
	wc.hInstance = m_hinstance;
	wc.hIcon = LoadIcon(0, IDI_APPLICATION);
	wc.hCursor = LoadCursor(0, IDC_ARROW);
	wc.hbrBackground = (HBRUSH)GetStockObject(NULL_BRUSH);
	wc.lpszMenuName = 0;
	wc.lpszClassName = WINDOW_CLASS_NAME;

	if (!RegisterClass(&wc)) {
		MessageBox(0, L"RegisterClass Failed.", 0, 0);
		return;
	}

	// Compute window rectangle dimensions based on requested client area dimensions.
	RECT rect = {0, 0, m_clientWidth, m_clientHeight};
	AdjustWindowRect(&rect, WS_OVERLAPPEDWINDOW, false);
	int width = rect.right - rect.left;
	int height = rect.bottom - rect.top;

	DWORD windowType;
	if (m_fullscreen) {
		windowType = WS_EX_TOPMOST | WS_POPUP;
	} else {
		windowType = WS_OVERLAPPEDWINDOW;
	}

	m_hwnd = CreateWindow(WINDOW_CLASS_NAME, m_mainWndCaption, windowType, CW_USEDEFAULT, CW_USEDEFAULT, width, height, 0, 0, m_hinstance, 0);
	if (!m_hwnd) {
		LPVOID lpMsgBuf;
		LPVOID lpDisplayBuf;
		DWORD dw = GetLastError();

		FormatMessage(
			FORMAT_MESSAGE_ALLOCATE_BUFFER |
			FORMAT_MESSAGE_FROM_SYSTEM |
			FORMAT_MESSAGE_IGNORE_INSERTS,
			NULL,
			dw,
			MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
			(LPTSTR)&lpMsgBuf,
			0, NULL);

		// Display the error message and exit the process

		lpDisplayBuf = (LPVOID)LocalAlloc(LMEM_ZEROINIT,
										  (lstrlen((LPCTSTR)lpMsgBuf) + 40) * sizeof(TCHAR));
		MessageBox(NULL, (LPCTSTR)lpDisplayBuf, TEXT("Error"), MB_OK);
		MessageBox(0, L"CreateWindow Failed.", 0, 0);
		return;
	}

	ShowWindow(m_hwnd, SW_SHOW);
	UpdateWindow(m_hwnd);

	return;
}

void RAPTEngine::ShutdownWindow() {
	// Show the mouse cursor.
	//ShowCursor(true);

	// Fix the display settings if leaving full screen mode.
	if (m_fullscreen) {
		ChangeDisplaySettings(NULL, 0);
	}

	// Remove the window.
	DestroyWindow(m_hwnd);
	m_hwnd = NULL;

	// Remove the application instance.
	UnregisterClass(L"HalflingEngineWindow", m_hinstance);
	m_hinstance = NULL;

	return;
}

} // End of namespace Engine