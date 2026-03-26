//=====client.cpp（控制端）=====
#define NOMINMAX // 防止 windows.h 和其他头文件中的 min/max 宏冲突
#include <winsock2.h> // 包含 Winsock 2 头文件，用于网络编程
#include <windows.h> // 包含 Windows API 头文件
#include <gdiplus.h> // 包含 GDI+ 头文件，用于图形图像处理
#include <thread> // 包含 thread 头文件，用于多线程
#include <vector> // 包含 vector 头文件，用于动态数组
#include <objbase.h> // 包含 COM 基础头文件，用于 COM 初始化
#include <atlbase.h> // 包含 ATL 基础头文件，用于 COM 对象的智能指针
#include <iostream> // 包含 iostream 头文件，用于标准输入输出

// 链接所需的库文件
#pragma comment(lib, "ws2_32.lib") // 链接 Winsock 2 库
#pragma comment(lib, "gdiplus.lib") // 链接 GDI+ 库
#pragma comment(lib, "Ole32.lib") // 链接 OLE32 库，用于 COM

using namespace Gdiplus; // 使用 GDI+ 命名空间

// 全局变量
LPCWSTR CLASS_NAME = L"RemoteDesktopViewer"; // 窗口类名
HWND hwnd; // 窗口句柄
HBITMAP hBitmap = nullptr; // 存储接收到的桌面图像的位图句柄
ULONG_PTR gdiplusToken; // GDI+ 初始化令牌
SOCKET client_socket; // 客户端套接字
int bmpWidth = 0; // 接收到的位图宽度
int bmpHeight = 0; // 接收到的位图高度

// 数据包头结构体
struct FDataHeader {
    int type; // 数据类型
    int payload_size; // 有效载荷大小
};

// 鼠标事件类型枚举
enum MouseEventType {
    MOUSE_MOVE, // 鼠标移动
    MOUSE_LEFT_DOWN, // 鼠标左键按下
    MOUSE_LEFT_UP, // 鼠标左键抬起
    MOUSE_RIGHT_DOWN, // 鼠标右键按下
    MOUSE_RIGHT_UP, // 鼠标右键抬起
    MOUSE_WHEEL // 鼠标滚轮滚动
};

// 鼠标事件结构体
struct MouseEvent {
    int type; // 鼠标事件类型
    int x; // 鼠标 X 坐标
    int y; // 鼠标 Y 坐标
    int delta; // 鼠标滚轮滚动距离 (对于 MOUSE_WHEEL 类型)
};

// 键盘事件结构体
struct KeyEvent {
    int keycode; // 键盘按键码
    bool keydown; // 按键是否按下 (true 为按下，false 为抬起)
};

// 启用 DPI 感知
void EnableDPIAwareness() {
    HMODULE shcore = LoadLibraryA("Shcore.dll"); // 加载 Shcore.dll
    if (shcore) {
        typedef HRESULT(WINAPI* SetDpiAwarenessFunc)(int);
        SetDpiAwarenessFunc SetDpiAwareness = (SetDpiAwarenessFunc)GetProcAddress(shcore, "SetProcessDpiAwareness"); // 获取 SetProcessDpiAwareness 函数地址
        if (SetDpiAwareness) SetDpiAwareness(2); // 调用 SetProcessDpiAwareness 设置 DPI 感知模式
        FreeLibrary(shcore); // 释放库
    }
    else {
        HMODULE user32 = LoadLibraryA("user32.dll"); // 如果 Shcore.dll 不可用，加载 user32.dll
        if (user32) {
            typedef BOOL(WINAPI* SetDPIAwareFunc)();
            SetDPIAwareFunc SetDPIAware = (SetDPIAwareFunc)GetProcAddress(user32, "SetProcessDPIAware"); // 获取 SetProcessDPIAware 函数地址
            if (SetDPIAware) SetDPIAware(); // 调用 SetProcessDPIAware 设置 DPI 感知模式
            FreeLibrary(user32); // 释放库
        }
    }
}

// 等待连接窗口过程函数
LRESULT CALLBACK WaitingWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_CREATE: // 窗口创建消息
        // 创建静态文本控件显示等待信息
        CreateWindowW(L"STATIC", L"正在等待客户端连接...", WS_CHILD | WS_VISIBLE,
            40, 40, 200, 30, hwnd, NULL, NULL, NULL);
        break;
    default:
        return DefWindowProc(hwnd, msg, wParam, lParam); // 调用默认窗口过程处理其他消息
    }
    return 0;
}

// 主窗口过程函数
LRESULT CALLBACK WindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
    switch (uMsg) {
    case WM_PAINT: // 窗口绘制消息
        if (hBitmap) { // 如果有位图数据
            PAINTSTRUCT ps;
            HDC hdc = BeginPaint(hwnd, &ps); // 开始绘制
            HDC hMemDC = CreateCompatibleDC(hdc); // 创建兼容的设备上下文
            HBITMAP oldBmp = (HBITMAP)SelectObject(hMemDC, hBitmap); // 选择位图到设备上下文
            BITMAP bmp;
            GetObject(hBitmap, sizeof(BITMAP), &bmp); // 获取位图信息
            BitBlt(hdc, 0, 0, bmp.bmWidth, bmp.bmHeight, hMemDC, 0, 0, SRCCOPY); // 将位图复制到窗口
            SelectObject(hMemDC, oldBmp); // 恢复旧的位图
            DeleteDC(hMemDC); // 删除兼容的设备上下文
            EndPaint(hwnd, &ps); // 结束绘制
        }
        break;
    case WM_LBUTTONDOWN: // 鼠标左键按下
    case WM_LBUTTONUP: // 鼠标左键抬起
    case WM_MOUSEMOVE: // 鼠标移动
    case WM_RBUTTONDOWN: // 鼠标右键按下
    case WM_RBUTTONUP: // 鼠标右键抬起
    case WM_MOUSEWHEEL:{
        if (client_socket == INVALID_SOCKET) break; // 如果客户端套接字无效，退出

        int x = LOWORD(lParam); // 获取鼠标 X 坐标
        int y = HIWORD(lParam); // 获取鼠标 Y 坐标

        MouseEvent evt{ 0, x, y, 0 }; // 创建鼠标事件结构体
        switch (uMsg) {
        case WM_LBUTTONDOWN: evt.type = MOUSE_LEFT_DOWN; break; // 设置事件类型为左键按下
        case WM_LBUTTONUP: evt.type = MOUSE_LEFT_UP; break; // 设置事件类型为左键抬起
        case WM_MOUSEMOVE: evt.type = MOUSE_MOVE; break; // 设置事件类型为鼠标移动
        case WM_RBUTTONDOWN: evt.type = MOUSE_RIGHT_DOWN; break; // 设置事件类型为右键按下
        case WM_RBUTTONUP: evt.type = MOUSE_RIGHT_UP; break; // 设置事件类型为右键抬起
        case WM_MOUSEWHEEL:evt.type = MOUSE_WHEEL; break;//设置事件类型为鼠标滚轮
        }

        FDataHeader header{ 2, sizeof(evt) }; // 创建数据包头 (类型 2 为鼠标事件)
        send(client_socket, (char*)&header, sizeof(header), 0); // 发送数据包头
        send(client_socket, (char*)&evt, sizeof(evt), 0); // 发送鼠标事件数据
        break;
    }
    case WM_KEYDOWN: // 键盘按下
    case WM_KEYUP: { // 键盘抬起
        if (client_socket == INVALID_SOCKET) break; // 如果客户端套接字无效，退出

        KeyEvent evt; // 创建键盘事件结构体
        evt.keycode = (int)wParam; // 获取按键码
        evt.keydown = (uMsg == WM_KEYDOWN); // 判断按键是否按下

        FDataHeader header{ 3, sizeof(evt) }; // 创建数据包头 (类型 3 为键盘事件)
        send(client_socket, (char*)&header, sizeof(header), 0); // 发送数据包头
        send(client_socket, (char*)&evt, sizeof(evt), 0); // 发送键盘事件数据
        break;
    }
    case WM_DESTROY: // 窗口销毁消息
        PostQuitMessage(0); // 发送退出消息
        break;
    default:
        return DefWindowProc(hwnd, uMsg, wParam, lParam); // 调用默认窗口过程处理其他消息
    }
    return 0;
}

// 接收数据线程函数
void ReceiveThread(SOCKET client_socket) {
    while (true) {
        FDataHeader header;
        int ret = recv(client_socket, (char*)&header, sizeof(header), 0);
        if (ret <= 0) {
            MessageBox(hwnd, L"客户端已断开连接", L"连接中断", MB_ICONERROR);
            ExitProcess(0); // 修改位置
            break;
        }

        if (header.type == 1) {
            std::vector<char> imageData(header.payload_size);
            int received_size = 0;
            while (received_size < header.payload_size) {
                int bytes = recv(client_socket, imageData.data() + received_size, header.payload_size - received_size, 0);
                if (bytes <= 0) {
                    MessageBox(hwnd, L"客户端数据传输中断", L"连接中断", MB_ICONERROR);
                    ExitProcess(0); // 同样替换此处
                    return;
                }
                received_size += bytes;
            }

            HGLOBAL hGlobal = GlobalAlloc(GMEM_MOVEABLE, imageData.size());
            if (!hGlobal) continue;
            LPVOID pData = GlobalLock(hGlobal);
            memcpy(pData, imageData.data(), imageData.size());
            GlobalUnlock(hGlobal);

            IStream* pStream = nullptr;
            if (FAILED(CreateStreamOnHGlobal(hGlobal, TRUE, &pStream)) || !pStream) {
                GlobalFree(hGlobal);
                continue;
            }

            Bitmap* bmp = new Bitmap(pStream);
            pStream->Release();

            if (!bmp || bmp->GetLastStatus() != Ok) {
                delete bmp;
                GlobalFree(hGlobal);
                continue;
            }

            if (hBitmap) {
                DeleteObject(hBitmap);
                hBitmap = nullptr;
            }
            if (bmp->GetHBITMAP(Color(0, 0, 0), &hBitmap) == Ok && hBitmap) {
                bmpWidth = bmp->GetWidth();
                bmpHeight = bmp->GetHeight();
                RECT rect = { 0, 0, bmpWidth, bmpHeight };
                AdjustWindowRectEx(&rect, WS_OVERLAPPEDWINDOW, FALSE, 0);
                int newWidth = rect.right - rect.left;
                int newHeight = rect.bottom - rect.top;
                SetWindowPos(hwnd, nullptr, 0, 0, newWidth, newHeight, SWP_NOMOVE | SWP_NOZORDER);
                InvalidateRect(hwnd, nullptr, TRUE);
            }

            delete bmp;
            GlobalFree(hGlobal);
        }
    }
}


// WinMain 入口函数
int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE, LPSTR, int) {

    EnableDPIAwareness(); // 启用 DPI 感知
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED); // 初始化 COM
    GdiplusStartupInput gdiInput;
    GdiplusStartup(&gdiplusToken, &gdiInput, nullptr); // 初始化 GDI+

    WNDCLASS wc_wait = {}; // 等待连接窗口类
    wc_wait.lpfnWndProc = WaitingWndProc; // 设置窗口过程函数
    wc_wait.hInstance = hInstance; // 设置实例句柄
    wc_wait.lpszClassName = L"WaitingClass"; // 设置窗口类名
    RegisterClass(&wc_wait); // 注册窗口类

    // 创建等待连接窗口
    HWND hwnd_wait = CreateWindowW(L"WaitingClass", L"等待连接", WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU,
        CW_USEDEFAULT, CW_USEDEFAULT, 300, 150, NULL, NULL, hInstance, NULL);
    ShowWindow(hwnd_wait, SW_SHOW); // 显示窗口
    UpdateWindow(hwnd_wait); // 更新窗口

    WSADATA wsa;
    WSAStartup(MAKEWORD(2, 2), &wsa); // 初始化 Winsock

    SOCKET listen_socket = socket(AF_INET, SOCK_STREAM, 0); // 创建监听套接字
    sockaddr_in server = {}; // 服务器地址结构体
    server.sin_family = AF_INET; // 地址族为 IPv4
    server.sin_port = htons(8080); // 端口号为 8080
    server.sin_addr.s_addr = inet_addr("0.0.0.0"); // 绑定所有可用 IP 地址
    bind(listen_socket, (sockaddr*)&server, sizeof(server)); // 绑定套接字
    listen(listen_socket, 1); // 监听连接请求
    client_socket = accept(listen_socket, nullptr, nullptr); // 接受客户端连接
    closesocket(listen_socket); // 关闭监听套接字
    DestroyWindow(hwnd_wait); // 销毁等待连接窗口

    WNDCLASS wc = {}; // 主窗口类
    wc.lpfnWndProc = WindowProc; // 设置窗口过程函数
    wc.hInstance = hInstance; // 设置实例句柄
    wc.lpszClassName = CLASS_NAME; // 设置窗口类名
    RegisterClass(&wc); // 注册窗口类

    // 创建主窗口
    hwnd = CreateWindowEx(0, CLASS_NAME, L"远程桌面画面", WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT, CW_USEDEFAULT, 800, 600,
        nullptr, nullptr, hInstance, nullptr);
    ShowWindow(hwnd, SW_SHOW); // 显示窗口
    UpdateWindow(hwnd); // 更新窗口

    std::thread recvThread(ReceiveThread, client_socket); // 创建接收数据线程

    MSG msg = {};
    while (GetMessage(&msg, nullptr, 0, 0)) { // 消息循环
        TranslateMessage(&msg); // 翻译消息
        DispatchMessage(&msg); // 分发消息
    }

    recvThread.join(); // 等待接收线程结束
    closesocket(client_socket); // 关闭客户端套接字
    WSACleanup(); // 清理 Winsock
    GdiplusShutdown(gdiplusToken); // 关闭 GDI+
    CoUninitialize(); // 卸载 COM
    return (int)msg.wParam; // 返回退出码
}