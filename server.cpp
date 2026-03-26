//=====server.cpp（被控端）=====
#define _WINSOCK_DEPRECATED_NO_WARNINGS // 禁用 Winsock 过时函数警告
#include <winsock2.h> // 包含 Winsock 2 头文件，用于网络编程
#include <windows.h> // 包含 Windows API 头文件
#include <gdiplus.h> // 包含 GDI+ 头文件，用于图形图像处理
#include <iostream> // 包含 iostream 头文件，用于标准输入输出
#include <thread> // 包含 thread 头文件，用于多线程
#include <vector> // 包含 vector 头文件，用于动态数组
#include <string> // 包含 string 头文件，用于字符串
#include <objbase.h> // 包含 COM 基础头文件，用于 COM 初始化
#include <atlbase.h> // 包含 ATL 基础头文件，用于 COM 对象的智能指针

// 链接所需的库文件
#pragma comment(lib, "ws2_32.lib") // 链接 Winsock 2 库
#pragma comment(lib, "gdiplus.lib") // 链接 GDI+ 库
#pragma comment(lib, "Ole32.lib") // 链接 OLE32 库，用于 COM

using namespace Gdiplus; // 使用 GDI+ 命名空间

ULONG_PTR gdiplusToken; // GDI+ 初始化令牌
static std::string g_inputIP = ""; // 存储用户输入的控制端 IP 地址

// 数据包头结构体 (与服务器端一致)
struct FDataHeader {
    int type; // 数据类型
    int payload_size; // 有效载荷大小
};

// 鼠标事件类型枚举 (与服务器端一致)
enum MouseEventType {
    MOUSE_MOVE, // 鼠标移动
    MOUSE_LEFT_DOWN, // 鼠标左键按下
    MOUSE_LEFT_UP, // 鼠标左键抬起
    MOUSE_RIGHT_DOWN, // 鼠标右键按下
    MOUSE_RIGHT_UP, // 鼠标右键抬起
    MOUSE_WHEEL // 鼠标滚轮滚动
};

// 鼠标事件结构体 (与服务器端一致)
struct MouseEvent {
    int type; // 鼠标事件类型
    int x; // 鼠标 X 坐标
    int y; // 鼠标 Y 坐标
    int delta; // 鼠标滚轮滚动距离 (对于 MOUSE_WHEEL 类型)
};

// 键盘事件结构体 (与服务器端一致)
struct KeyEvent {
    int keycode; // 键盘按键码
    bool keydown; // 按键是否按下 (true 为按下，false 为抬起)
};

// 启用 DPI 感知 (与服务器端一致)
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
            SetDPIAwareFunc SetDPIAware = (SetDPIAwareFunc)GetProcAddress(user32, "SetProcessDPIAware"); // 获取 SetDPIAware 函数地址
            if (SetDPIAware) SetDPIAware(); // 调用 SetDPIAware 设置 DPI 感知模式
            FreeLibrary(user32); // 释放库
        }
    }
}

// 输入 IP 地址窗口过程函数
LRESULT CALLBACK InputWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    static HWND hEdit; // 编辑框句柄
    switch (msg) {
    case WM_CREATE: // 窗口创建消息
        // 创建静态文本控件显示提示信息
        CreateWindowA("STATIC", "请输入控制端 IP 地址：", WS_CHILD | WS_VISIBLE,
            20, 20, 200, 20, hwnd, NULL, NULL, NULL);
        // 创建编辑框用于输入 IP 地址
        hEdit = CreateWindowA("EDIT", "", WS_CHILD | WS_VISIBLE | WS_BORDER | ES_AUTOHSCROLL,
            20, 45, 240, 25, hwnd, (HMENU)101, NULL, NULL); // 101 是控件 ID
        // 创建确认按钮
        CreateWindowA("BUTTON", "确认", WS_CHILD | WS_VISIBLE,
            40, 80, 80, 30, hwnd, (HMENU)1, NULL, NULL); // 1 是按钮 ID
        // 创建取消按钮
        CreateWindowA("BUTTON", "取消", WS_CHILD | WS_VISIBLE,
            140, 80, 80, 30, hwnd, (HMENU)2, NULL, NULL); // 2 是按钮 ID
        break;
    case WM_COMMAND: // 控件命令消息
        if (LOWORD(wParam) == 1) { // 如果是确认按钮被点击
            char buffer[32] = {};
            GetWindowTextA(hEdit, buffer, sizeof(buffer)); // 获取编辑框文本
            g_inputIP = buffer; // 存储输入的 IP 地址
            DestroyWindow(hwnd); // 销毁窗口
        }
        else if (LOWORD(wParam) == 2) { // 如果是取消按钮被点击
            g_inputIP.clear(); // 清空存储的 IP 地址
            DestroyWindow(hwnd); // 销毁窗口
        }
        break;
    case WM_DESTROY: // 窗口销毁消息
        PostQuitMessage(0); // 发送退出消息
        break;
    default:
        return DefWindowProc(hwnd, msg, wParam, lParam); // 调用默认窗口过程处理其他消息
    }
    return 0;
}


// 捕获屏幕到位图!
HBITMAP CaptureScreenBitmap() {
    HDC hScreen = GetDC(NULL); // 获取屏幕设备上下文，像素数据
    HDC hMemDC = CreateCompatibleDC(hScreen); // 创建兼容的设备上下文（内存DC）
    int screenX = GetSystemMetrics(SM_CXSCREEN); // 获取屏幕宽度
    int screenY = GetSystemMetrics(SM_CYSCREEN); // 获取屏幕高度
    HBITMAP hBitmap = CreateCompatibleBitmap(hScreen, screenX, screenY); // 创建兼容的位图
    SelectObject(hMemDC, hBitmap); // 选择位图到设备上下文
    BitBlt(hMemDC, 0, 0, screenX, screenY, hScreen, 0, 0, SRCCOPY); // 将屏幕内容复制到位图
    SelectObject(hMemDC, 0); // 恢复旧的对象
    DeleteDC(hMemDC); // 删除兼容的设备上下文
    ReleaseDC(NULL, hScreen); // 释放屏幕设备上下文
    return hBitmap; // 返回捕获的位图句柄
}

// 获取指定图像格式编码器的 CLSID（类标识符）!
int GetEncoderClsid(const WCHAR* format, CLSID* pClsid) {
    UINT num = 0; // 编码器数量
    UINT size = 0; // 编码器信息数组大小
    ImageCodecInfo* pImageCodecInfo = nullptr; // 编码器信息数组指针

    GetImageEncodersSize(&num, &size); // 获取图像编码器数量和数组大小
    if (size == 0) return -1; // 如果没有编码器，返回 -1

    pImageCodecInfo = (ImageCodecInfo*)(malloc(size)); // 分配内存存储编码器信息
    if (pImageCodecInfo == nullptr) return -1; // 如果内存分配失败，返回 -1

    GetImageEncoders(num, size, pImageCodecInfo); // 获取图像编码器信息

    int result = -1;
    for (UINT j = 0; j < num; ++j) { // 遍历编码器
        if (wcscmp(pImageCodecInfo[j].MimeType, format) == 0) { // 如果找到匹配的格式
            *pClsid = pImageCodecInfo[j].Clsid; // 获取 CLSID
            result = j; // 记录索引
            break; // 退出循环
        }
    }
    free(pImageCodecInfo); // 释放内存
    return result; // 返回编码器索引或 -1
}

// 捕获屏幕并发送线程函数!
void CaptureAndSend(SOCKET sock) {
    CLSID jpegClsid;
    if (GetEncoderClsid(L"image/jpeg", &jpegClsid) == -1) { // 获取 JPEG 编码器 CLSID
        std::cerr << "JPEG encoder not found." << std::endl; // 如果找不到，输出错误信息
        return; // 退出函数
    }

    while (true) { // 循环捕获和发送
        HBITMAP hBitmap = CaptureScreenBitmap(); // 捕获屏幕到位图
        if (!hBitmap) { // 如果捕获失败
            std::this_thread::sleep_for(std::chrono::milliseconds(0)); // 等待一段时间后重试
            continue; // 继续下一次循环
        }

        Bitmap bmp(hBitmap, nullptr); // 从 HBITMAP 创建 GDI+ Bitmap 对象
        DeleteObject(hBitmap); // 删除 HBITMAP (GDI+ Bitmap 已经复制了数据)

        IStream* pStream = nullptr; // IStream 指针
        HGLOBAL hGlobal = nullptr; // 全局内存句柄

        hGlobal = GlobalAlloc(GMEM_MOVEABLE, 0); // 分配可移动的全局内存
        if (!hGlobal) { // 如果分配失败
            std::cerr << "GlobalAlloc failed." << std::endl; // 输出错误信息
            std::this_thread::sleep_for(std::chrono::milliseconds(0)); // 等待一段时间后重试
            continue; // 继续下一次循环
        }

        HRESULT hr_stream = CreateStreamOnHGlobal(hGlobal, TRUE, &pStream); // 从全局内存创建 IStream
        if (FAILED(hr_stream) || !pStream) { // 如果创建 IStream 失败
            std::cerr << "CreateStreamOnHGlobal failed." << std::endl; // 输出错误信息
            GlobalFree(hGlobal); // 释放全局内存
            std::this_thread::sleep_for(std::chrono::milliseconds(0)); // 等待一段时间后重试
            continue; // 继续下一次循环
        }

        EncoderParameters encoderParameters; // 编码器参数
        encoderParameters.Count = 1; // 参数数量为 1
        encoderParameters.Parameter[0].Guid = EncoderQuality; // 参数 GUID 为 EncoderQuality
        encoderParameters.Parameter[0].NumberOfValues = 1; // 参数值为 1 个
        encoderParameters.Parameter[0].Type = EncoderParameterValueTypeLong; // 参数类型为 Long

        ULONG quality = 75; // 设置 JPEG 质量为 75

        encoderParameters.Parameter[0].Value = &quality; // 设置参数值

        Status status = bmp.Save(pStream, &jpegClsid, &encoderParameters); // 将 Bitmap 保存为 JPEG 格式到 IStream

        if (status != Ok) { // 如果保存失败
            std::cerr << "Bitmap Save failed: " << status << std::endl; // 输出错误信息和状态码
            pStream->Release(); // 释放 IStream
            GlobalFree(hGlobal); // 释放全局内存
            std::this_thread::sleep_for(std::chrono::milliseconds(0)); // 等待一段时间后重试
            continue; // 继续下一次循环
        }

        STATSTG statstg;
        pStream->Stat(&statstg, STATFLAG_NONAME); // 获取 IStream 的统计信息
        int size = static_cast<int>(statstg.cbSize.LowPart); // 获取数据大小

        LPVOID pData = GlobalLock(hGlobal); // 锁定全局内存
        if (!pData) { // 如果锁定失败
            std::cerr << "GlobalLock failed." << std::endl; // 输出错误信息
            pStream->Release(); // 释放 IStream
            GlobalFree(hGlobal); // 释放全局内存
            std::this_thread::sleep_for(std::chrono::milliseconds(0)); // 等待一段时间后重试
            continue; // 继续下一次循环
        }

        FDataHeader header{ 1, size }; // 创建数据包头 (类型 1 为图像数据)
        send(sock, (char*)&header, sizeof(header), 0); // 发送数据包头
        send(sock, (char*)pData, size, 0); // 发送图像数据

        GlobalUnlock(hGlobal); // 解锁全局内存
        GlobalFree(hGlobal); // 释放全局内存
        pStream->Release(); // 释放 IStream

        std::this_thread::sleep_for(std::chrono::milliseconds(0)); // 短暂等待
    }
}



// 通过对话框获取 IP 地址
std::string GetIPAddressFromDialog() {
    WNDCLASSA wc = {}; // 窗口类
    wc.lpfnWndProc = InputWndProc; // 设置窗口过程函数
    wc.hInstance = GetModuleHandle(NULL); // 获取实例句柄
    wc.lpszClassName = "InputDialog"; // 设置窗口类名
    RegisterClassA(&wc); // 注册窗口类

    // 创建输入 IP 地址的窗口
    HWND hwnd = CreateWindowA("InputDialog", "连接控制端", WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU,
        CW_USEDEFAULT, CW_USEDEFAULT, 300, 170, NULL, NULL, GetModuleHandle(NULL), NULL);

    ShowWindow(hwnd, SW_SHOW); // 显示窗口
    UpdateWindow(hwnd); // 更新窗口

    MSG msg;
    while (GetMessage(&msg, NULL, 0, 0)) { // 消息循环
        TranslateMessage(&msg); // 翻译消息
        DispatchMessage(&msg); // 分发消息
        if (!IsWindow(hwnd)) break; // 如果窗口被销毁，退出循环
    }

    return g_inputIP; // 返回输入的 IP 地址
}

// 处理输入事件线程函数
void InputThread(SOCKET sock) {
    while (true) { // 循环接收输入事件
        FDataHeader header;
        int ret = recv(sock, (char*)&header, sizeof(header), 0); // 接收数据包头
        if (ret <= 0) break; // 如果接收失败或连接断开，退出循环

        if (header.type == 2 && header.payload_size == sizeof(MouseEvent)) { // 如果数据类型为 2 (鼠标事件)
            MouseEvent evt;
            ret = recv(sock, (char*)&evt, sizeof(evt), 0); // 接收鼠标事件数据
            if (ret <= 0) break; // 如果接收失败，退出循环
            SetCursorPos(evt.x, evt.y); // 设置鼠标位置
            if (evt.type != MOUSE_MOVE) { // 如果不是鼠标移动事件
                INPUT input = {}; // 创建 INPUT 结构体
                input.type = INPUT_MOUSE; // 设置输入类型为鼠标
                switch (evt.type) { // 根据鼠标事件类型设置鼠标事件标志
                case MOUSE_MOVE:input.mi.dwFlags = MOUSEEVENTF_MOVE; break;
                case MOUSE_LEFT_DOWN: input.mi.dwFlags = MOUSEEVENTF_LEFTDOWN; break; // 左键按下
                case MOUSE_LEFT_UP: input.mi.dwFlags = MOUSEEVENTF_LEFTUP; break; // 左键抬起
                case MOUSE_RIGHT_DOWN: input.mi.dwFlags = MOUSEEVENTF_RIGHTDOWN; break; // 右键按下
                case MOUSE_RIGHT_UP: input.mi.dwFlags = MOUSEEVENTF_RIGHTUP; break; // 右键抬起
                case MOUSE_WHEEL: input.mi.dwFlags = MOUSEEVENTF_WHEEL; input.mi.mouseData = evt.delta; break; // 滚轮滚动
                }
                SendInput(1, &input, sizeof(INPUT)); // 发送模拟输入事件
            }
        }
        else if (header.type == 3 && header.payload_size == sizeof(KeyEvent)) { // 如果数据类型为 3 (键盘事件)
            KeyEvent evt;
            ret = recv(sock, (char*)&evt, sizeof(evt), 0); // 接收键盘事件数据
            if (ret <= 0) break; // 如果接收失败，退出循环
            INPUT input = {}; // 创建 INPUT 结构体
            input.type = INPUT_KEYBOARD; // 设置输入类型为键盘
            input.ki.wVk = evt.keycode; // 设置虚拟键码
            input.ki.dwFlags = evt.keydown ? 0 : KEYEVENTF_KEYUP; // 设置按键状态 (按下或抬起)
            SendInput(1, &input, sizeof(INPUT)); // 发送模拟输入事件
        }
    }
    // 新增：连接断开后立即退出程序
    ExitProcess(0);
}

// WinMain 入口函数
int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE, LPSTR, int) {
    EnableDPIAwareness(); // 启用 DPI 感知

    HRESULT hr_com = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED); // 初始化 COM
    if (FAILED(hr_com)) { // 如果初始化失败
        MessageBoxA(NULL, "Failed to initialize COM.", "Error", MB_ICONERROR); // 显示错误消息框
        return -1; // 返回错误码
    }

    GdiplusStartupInput gdiplusStartupInput;
    GdiplusStartup(&gdiplusToken, &gdiplusStartupInput, nullptr); // 初始化 GDI+

    std::string ip = GetIPAddressFromDialog(); // 调用函数获取控制端 IP 地址
    if (ip.empty()) { // 如果用户取消输入或输入为空
        GdiplusShutdown(gdiplusToken); // 关闭 GDI+
        CoUninitialize(); // 卸载 COM
        return 0; // 返回成功 (用户取消)
    }

    WSADATA wsaData;
    WSAStartup(MAKEWORD(2, 2), &wsaData); // 初始化 Winsock
    SOCKET sock = socket(AF_INET, SOCK_STREAM, 0); // 创建客户端套接字

    sockaddr_in server; // 服务器地址结构体
    server.sin_family = AF_INET; // 地址族为 IPv4
    server.sin_port = htons(8080); // 端口号为 8080
    server.sin_addr.s_addr = inet_addr(ip.c_str()); // 设置服务器 IP 地址

    if (connect(sock, (sockaddr*)&server, sizeof(server)) != 0) { // 连接到服务器
        MessageBoxA(NULL, "连接失败，请检查IP或控制端状态", "连接失败", MB_ICONERROR); // 如果连接失败，显示错误消息框
        closesocket(sock); // 关闭套接字
        WSACleanup(); // 清理 Winsock
        GdiplusShutdown(gdiplusToken); // 关闭 GDI+
        CoUninitialize(); // 卸载 COM
        return -1; // 返回错误码
    }

    std::thread t1(CaptureAndSend, sock); // 创建捕获屏幕并发送线程
    std::thread t2(InputThread, sock); // 创建处理输入事件线程
    t1.join(); // 等待捕获线程结束
    t2.join(); // 等待输入线程结束

    closesocket(sock); // 关闭套接字
    WSACleanup(); // 清理 Winsock
    GdiplusShutdown(gdiplusToken); // 关闭 GDI+
    CoUninitialize(); // 卸载 COM
    return 0; // 返回成功
}