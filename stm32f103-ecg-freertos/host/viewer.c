#include "session.h"
#include <windows.h>
#include <commdlg.h>
#include <wchar.h>
#include <stdio.h>

enum { ID_PORT = 100, ID_CONNECT, ID_CLOSE, ID_DROP, ID_CSV };
static session_t session;
static HWND port_box;
static wchar_t message[160] = L"选择串口后点击连接。115200 / 8N1";

static void draw_chart(HDC dc, RECT bounds)
{
    HBRUSH background = CreateSolidBrush(RGB(17,26,37));
    FillRect(dc, &bounds, background); DeleteObject(background);
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, RGB(206,220,230));
    receiver_t *r = &session.receiver;
    wchar_t line[512];
    swprintf(line, 512, L"%ls   收到 %lu  待补 %lu  过期 %lu  ADC故障 %lu  RX错误 %lu  帧错误 %lu",
             message,(unsigned long)r->received,(unsigned long)receiver_missing(r),(unsigned long)r->expired,
             (unsigned long)r->adc_faults,(unsigned long)r->rx_errors,(unsigned long)session.parser.errors);
    TextOutW(dc, 15, 57, line, (int)wcslen(line));
    uint32_t *d = r->diagnostics;
    swprintf(line, 512, L"FreeRTOS：ADC P%lu / 通信 P%lu / 监测 P%lu   栈余量(字)：%lu / %lu / %lu   最大唤醒 %lu us / 处理 %lu us",
             (unsigned long)d[9],(unsigned long)d[10],(unsigned long)d[11],(unsigned long)d[6],(unsigned long)d[7],(unsigned long)d[8],(unsigned long)d[4],(unsigned long)d[5]);
    TextOutW(dc, 15, 82, line, (int)wcslen(line));
    if (session_stale(&session, GetTickCount())) {
        SetTextColor(dc, RGB(255,110,100));
        const wchar_t *warning = L"数据超时：超过3秒没有有效帧。检查板子供电、PA2→RX及程序状态。";
        TextOutW(dc, 15, 105, warning, (int)wcslen(warning));
    }
    COLORREF colors[3] = {RGB(69,223,206),RGB(246,201,107),RGB(187,161,255)};
    const wchar_t *names[3] = {L"Ⅰ导联",L"Ⅱ导联",L"Ⅲ导联"};
    uint32_t end = r->next;
    uint32_t first = end > 1500 ? end - 1500 : 0;
    int chart_height = (bounds.bottom - 145) / 3;
    if (chart_height < 30) return;
    for (unsigned channel = 0; channel < 3; channel++) {
        int top = 120 + (int)channel * chart_height;
        int bottom = top + chart_height - 25;
        SetTextColor(dc, colors[channel]); TextOutW(dc, 15, top, names[channel], (int)wcslen(names[channel]));
        HPEN grid = CreatePen(PS_SOLID, 1, RGB(42,55,70));
        HGDIOBJ previous = SelectObject(dc, grid);
        for (unsigned value = 0; value <= 4096; value += 1024) {
            int y = bottom - (int)(value * (unsigned)(bottom-top) / 4096);
            MoveToEx(dc, 90, y, NULL); LineTo(dc, bounds.right - 20, y);
        }
        SelectObject(dc, previous); DeleteObject(grid);
        HPEN trace = CreatePen(PS_SOLID, 2, colors[channel]);
        previous = SelectObject(dc, trace);
        int continuous = 0;
        for (uint32_t seq = first; seq < end; seq++) {
            uint16_t values[3];
            if (!receiver_get(r, seq, values)) { continuous = 0; continue; }
            int x = 90 + (int)((seq-first) * (unsigned)(bounds.right-110) / 1500);
            int y = bottom - values[channel] * (bottom-top) / 4095;
            if (continuous) LineTo(dc, x, y); else MoveToEx(dc, x, y, NULL);
            continuous = 1;
        }
        SelectObject(dc, previous); DeleteObject(trace);
    }
    SetTextColor(dc, RGB(170,185,200));
    const wchar_t *footer = L"PWM 跳线 → ADC 三通道 → 数字平均；教学模拟，禁止接人体。纵轴原始ADC码 0–4095，横轴6秒。";
    TextOutW(dc, 15, bounds.bottom - 22, footer, (int)wcslen(footer));
}

static LRESULT CALLBACK window_proc(HWND window, UINT msg, WPARAM wparam, LPARAM lparam)
{
    switch (msg) {
    case WM_GETMINMAXINFO: {
        MINMAXINFO *limits = (MINMAXINFO *)lparam;
        limits->ptMinTrackSize.x = 950;
        limits->ptMinTrackSize.y = 550;
        return 0;
    }
    case WM_CREATE:
        port_box = CreateWindowW(L"COMBOBOX",L"",WS_CHILD|WS_VISIBLE|CBS_DROPDOWN,15,15,125,300,window,(HMENU)ID_PORT,NULL,NULL);
        for (unsigned i=1;i<=64;i++) {
            wchar_t port[16], device[256]; swprintf(port,16,L"COM%u",i);
            if (QueryDosDeviceW(port,device,256)) SendMessageW(port_box,CB_ADDSTRING,0,(LPARAM)port);
        }
        SendMessageW(port_box,CB_SETCURSEL,0,0);
        CreateWindowW(L"BUTTON",L"连接",WS_CHILD|WS_VISIBLE,155,13,80,28,window,(HMENU)ID_CONNECT,NULL,NULL);
        CreateWindowW(L"BUTTON",L"断开",WS_CHILD|WS_VISIBLE,245,13,80,28,window,(HMENU)ID_CLOSE,NULL,NULL);
        CreateWindowW(L"BUTTON",L"丢弃接收5秒",WS_CHILD|WS_VISIBLE,340,13,140,28,window,(HMENU)ID_DROP,NULL,NULL);
        CreateWindowW(L"BUTTON",L"保存CSV",WS_CHILD|WS_VISIBLE,495,13,110,28,window,(HMENU)ID_CSV,NULL,NULL);
        SetTimer(window,1,10,NULL);
        return 0;
    case WM_COMMAND:
        switch (LOWORD(wparam)) {
        case ID_CONNECT: {
            char port[32]; GetWindowTextA(port_box,port,sizeof(port));
            session_close(&session);
            if (session_open(&session,port)) wcscpy(message,L"连接中；如需CSV请在连接后开启");
            else swprintf(message,160,L"串口打开失败，错误码%lu（检查端口是否被占用）",GetLastError());
            break;
        }
        case ID_CLOSE: session_close(&session); wcscpy(message,L"已断开，CSV记录已关闭"); break;
        case ID_DROP:
            if (session.connected) { session_drop(&session,GetTickCount()); wcscpy(message,L"故意丢弃5秒数据，随后补传"); }
            break;
        case ID_CSV: {
            if (!session.connected) { wcscpy(message,L"请先连接串口，再开启CSV记录"); break; }
            char path[MAX_PATH] = "capture.csv";
            OPENFILENAMEA dialog = {0}; dialog.lStructSize=sizeof(dialog); dialog.hwndOwner=window;
            dialog.lpstrFilter="CSV files\0*.csv\0"; dialog.lpstrFile=path; dialog.nMaxFile=MAX_PATH;
            dialog.lpstrDefExt="csv"; dialog.Flags=OFN_OVERWRITEPROMPT|OFN_PATHMUSTEXIST;
            if (GetSaveFileNameA(&dialog)) {
                if (session_csv(&session,path)) wcscpy(message,L"CSV记录已开启，补传记录按序号归档");
                else wcscpy(message,L"无法创建CSV文件");
            }
            break;
        }
        }
        return 0;
    case WM_TIMER: {
        uint32_t now=GetTickCount();
        if (session.connected && session_step(&session,now)<0) { session_close(&session); wcscpy(message,L"串口或CSV出错，连接及记录已关闭"); }
        if (session.connected && session.receiver.started && session.drop_length && now-session.drop_start>=5000) {
            if (!receiver_missing(&session.receiver)) wcscpy(message,L"接收已恢复；检查过期计数判断是否全部补齐");
        }
        static unsigned refresh;
        if (++refresh % 4 == 0) InvalidateRect(window,NULL,FALSE);
        return 0;
    }
    case WM_ERASEBKGND: return 1;
    case WM_PAINT: {
        PAINTSTRUCT paint; HDC dc=BeginPaint(window,&paint); RECT rect; GetClientRect(window,&rect);
        HDC memory=CreateCompatibleDC(dc); HBITMAP bitmap=CreateCompatibleBitmap(dc,rect.right,rect.bottom);
        HGDIOBJ old=SelectObject(memory,bitmap);
        draw_chart(memory,rect); BitBlt(dc,0,45,rect.right,rect.bottom-45,memory,0,45,SRCCOPY);
        SelectObject(memory,old); DeleteObject(bitmap); DeleteDC(memory); EndPaint(window,&paint); return 0;
    }
    case WM_DESTROY: session_close(&session); PostQuitMessage(0); return 0;
    }
    return DefWindowProcW(window,msg,wparam,lparam);
}

int main(int argc, char **argv)
{
    HINSTANCE instance=GetModuleHandleW(NULL);
    WNDCLASSW klass={0}; klass.lpfnWndProc=window_proc; klass.hInstance=instance;
    klass.lpszClassName=L"EcgFreeRtosViewer"; klass.hCursor=LoadCursor(NULL,IDC_ARROW);
    klass.hbrBackground=(HBRUSH)(COLOR_BTNFACE+1);
    if (!RegisterClassW(&klass)) return 1;
    HWND window=CreateWindowW(klass.lpszClassName,L"三导联 ADC · FreeRTOS / C语言上位机",WS_OVERLAPPEDWINDOW,
                             CW_USEDEFAULT,CW_USEDEFAULT,1180,780,NULL,NULL,instance,NULL);
    if (!window) return 1;
    ShowWindow(window,SW_SHOW); UpdateWindow(window);
    if (argc > 1) {
        SetWindowTextA(port_box, argv[1]);
        PostMessageW(window, WM_COMMAND, ID_CONNECT, 0);
    }
    MSG message_event;
    while (GetMessageW(&message_event,NULL,0,0)>0) { TranslateMessage(&message_event); DispatchMessageW(&message_event); }
    return 0;
}
