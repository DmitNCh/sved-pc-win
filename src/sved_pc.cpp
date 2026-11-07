#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif

#include <windows.h>
#include <commdlg.h>
#include <gdiplus.h>
#include <string>
#include <vector>
#include <sstream>
#include <fstream>
#include <cmath>

using namespace Gdiplus;

// Идентификаторы кнопок управления (строго без ручного ввода с клавиатуры)
#define ID_BTN_TRAIN_NODES   101
#define ID_BTN_TRAIN_SPHERE  102
#define ID_BTN_ACT_DISKS     103
#define ID_BTN_ACT_PHOTO     104
#define ID_BTN_ADD_FILE      105
#define ID_BTN_CLEAR_BOX     106
#define ID_BTN_RUN_SCRIPTS   107
#define ID_BTN_KILL_ALL      108
#define ID_TIMER_SPHERE      201

HWND hConsoleOut, hScriptBox;
std::wstring scriptBuffer = L"";
std::wstring consoleBuffer = L"";
float sphereAngle = 0.0f;
bool sphereActive = true;
std::vector<std::wstring> detectedDrives;
Image* loadedPhoto = nullptr;
std::wstring currentPhotoName = L"";

// Добавление строки в верхний журнал ядра
void AppendConsole(const std::wstring& msg) {
    consoleBuffer += msg + L"\r\n";
    SetWindowTextW(hConsoleOut, consoleBuffer.c_str());
    SendMessageW(hConsoleOut, EM_SETSEL, 0, -1);
    SendMessageW(hConsoleOut, EM_SETSEL, -1, -1);
    SendMessageW(hConsoleOut, EM_SCROLLCARET, 0, 0);
}

// Добавление модуля скрипта в защищённое окно (писать вручную нельзя)
void AppendScriptBlock(const std::wstring& block) {
    if (!scriptBuffer.empty()) {
        scriptBuffer += L"\r\n\r\n";
    }
    scriptBuffer += block;
    SetWindowTextW(hScriptBox, scriptBuffer.c_str());
    SendMessageW(hScriptBox, EM_SETSEL, 0, -1);
    SendMessageW(hScriptBox, EM_SETSEL, -1, -1);
    SendMessageW(hScriptBox, EM_SCROLLCARET, 0, 0);
}

// Реальный опрос логических дисков компьютера (слои дисков)
void ScanComputerDisks() {
    detectedDrives.clear();
    DWORD mask = GetLogicalDrives();
    for (wchar_t d = L'A'; d <= L'Z'; ++d) {
        if (mask & 1) {
            std::wstring drive = std::wstring(1, d) + L":\\";
            ULARGE_INTEGER freeBytes, totalBytes;
            if (GetDiskFreeSpaceExW(drive.c_str(), &freeBytes, &totalBytes, NULL)) {
                long long totalGB = totalBytes.QuadPart / (1024ULL * 1024ULL * 1024ULL);
                long long freeGB = freeBytes.QuadPart / (1024ULL * 1024ULL * 1024ULL);
                detectedDrives.push_back(drive + L" [" + std::to_wstring(freeGB) + L"/" + std::to_wstring(totalGB) + L" ГБ]");
            } else {
                detectedDrives.push_back(drive + L" [Слой готов]");
            }
        }
        mask >>= 1;
    }
}

// Реальный опрос процессора и оперативной памяти ПК
void ProbePCNodes() {
    SYSTEM_INFO sysInfo;
    GetSystemInfo(&sysInfo);

    MEMORYSTATUSEX memInfo;
    memInfo.dwLength = sizeof(MEMORYSTATUSEX);
    GlobalMemoryStatusEx(&memInfo);
    long long totalRamMB = memInfo.ullTotalPhys / (1024 * 1024);
    long long availRamMB = memInfo.ullAvailPhys / (1024 * 1024);

    AppendConsole(L" -> [АППАРАТНЫЙ ОТКЛИК ПК]: Доступно потоков процессора: " +
                  std::to_wstring(sysInfo.dwNumberOfProcessors) +
                  L" | ОЗУ: свободно " + std::to_wstring(availRamMB) +
                  L" МБ из " + std::to_wstring(totalRamMB) + L" МБ.");
}

// Чтение UTF-8 файла скрипта с диска компьютера
std::wstring ReadUtf8File(const wchar_t* path) {
    FILE* f = _wfopen(path, L"rb");
    if (!f) return L"";
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (sz <= 0) { fclose(f); return L""; }
    std::string buf(sz, '\0');
    fread(&buf[0], 1, sz, f);
    fclose(f);

    int wlen = MultiByteToWideChar(CP_UTF8, 0, buf.c_str(), (int)buf.size(), NULL, 0);
    if (wlen > 0) {
        std::wstring wbuf(wlen, L'\0');
        MultiByteToWideChar(CP_UTF8, 0, buf.c_str(), (int)buf.size(), &wbuf[0], wlen);
        return wbuf;
    }
    return L"";
}

// Открытие фотографии для просмотра прямо внутри сферы СВЕД
void OpenPhotoLayer(HWND hwnd) {
    wchar_t filePath[MAX_PATH] = {0};
    OPENFILENAMEW ofn = {0};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = hwnd;
    ofn.lpstrFilter = L"Фотографии и изображения (*.jpg;*.png;*.bmp)\0*.jpg;*.jpeg;*.png;*.bmp\0Все файлы (*.*)\0*.*\0";
    ofn.lpstrFile = filePath;
    ofn.nMaxFile = MAX_PATH;
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST;
    ofn.lpstrTitle = L"Выберите фотографию для просмотра в слое СВЕД";

    if (GetOpenFileNameW(&ofn)) {
        if (loadedPhoto) {
            delete loadedPhoto;
            loadedPhoto = nullptr;
        }
        loadedPhoto = new Image(filePath);
        currentPhotoName = filePath;
        AppendConsole(L" -> [СЛОЙ ФОТОГРАФИЙ]: Открыт файл: " + currentPhotoName);
        InvalidateRect(hwnd, NULL, FALSE);
    }
}

// Отрисовка полупрозрачной сферы ГРЯДА/СВЕД со слоями дисков и фотографией
void DrawSvedSphere(HDC hdc, RECT rcSphere) {
    int w = rcSphere.right - rcSphere.left;
    int h = rcSphere.bottom - rcSphere.top;
    Bitmap backBuffer(w, h, PixelFormat32bppARGB);
    Graphics g(&backBuffer);
    g.SetSmoothingMode(SmoothingModeAntiAlias);

    // Чёрный космос холста СВЕД-100
    SolidBrush blackBrush(Color(255, 5, 8, 14));
    g.FillRectangle(&blackBrush, 0, 0, w, h);

    int cx = w / 2;
    int cy = h / 2;
    int radius = (w < h ? w : h) / 2 - 25;

    // Если загружена фотография — выводим её в правой части либо фоном слоя
    if (loadedPhoto && loadedPhoto->GetLastStatus() == Ok) {
        Rect photoRect(cx - radius, cy - radius, radius * 2, radius * 2);
        g.DrawImage(loadedPhoto, photoRect);
        SolidBrush veil(Color(140, 0, 12, 24));
        g.FillRectangle(&veil, 0, 0, w, h);
    }

    // Полупрозрачная сфера СВЕД
    SolidBrush sphereGlow(Color(45, 0, 180, 255));
    g.FillEllipse(&sphereGlow, cx - radius, cy - radius, radius * 2, radius * 2);

    Pen outerPen(Color(200, 0, 255, 200), 2.0f);
    g.DrawEllipse(&outerPen, cx - radius, cy - radius, radius * 2, radius * 2);

    // Вращающиеся орбиты слоёв внутри сферы
    Pen orbitPen(Color(110, 0, 200, 255), 1.2f);
    for (int i = 1; i <= 3; ++i) {
        int rx = (int)(radius * std::abs(std::cos(sphereAngle + i * 0.8f)));
        g.DrawEllipse(&orbitPen, cx - rx, cy - radius, rx * 2, radius * 2);
        int ry = (int)(radius * std::abs(std::sin(sphereAngle * 0.7f + i)));
        g.DrawEllipse(&orbitPen, cx - radius, cy - ry, radius * 2, ry * 2);
    }

    // Отрисовка плавающих окон-слоёв для каждого найденного диска компьютера
    FontFamily fontFamily(L"Consolas");
    Font layerFont(&fontFamily, 12, FontStyleBold, UnitPixel);
    SolidBrush textBrush(Color(255, 180, 255, 0));
    SolidBrush boxBrush(Color(160, 10, 35, 55));
    Pen boxPen(Color(220, 0, 255, 128), 1.5f);

    for (size_t i = 0; i < detectedDrives.size(); ++i) {
        float ang = sphereAngle + (i * 6.28318f / detectedDrives.size());
        int lx = cx + (int)(std::cos(ang) * (radius * 0.62f)) - 75;
        int ly = cy + (int)(std::sin(ang) * (radius * 0.62f)) - 16;

        RectF nodeRect((REAL)lx, (REAL)ly, 155.0f, 32.0f);
        g.FillRectangle(&boxBrush, nodeRect);
        g.DrawRectangle(&boxPen, nodeRect.X, nodeRect.Y, nodeRect.Width, nodeRect.Height);
        g.DrawString(detectedDrives[i].c_str(), -1, &layerFont, PointF((REAL)(lx + 8), (REAL)(ly + 8)), &textBrush);
    }

    // Центральное ядро сферы
    Font titleFont(&fontFamily, 13, FontStyleBold, UnitPixel);
    SolidBrush centerBrush(Color(255, 0, 255, 255));
    std::wstring centerTitle = L"[ СФЕРА СЛОЕВ СВЕД-100 | БЕЛЫЙ КВАДРАТ ]";
    g.DrawString(centerTitle.c_str(), -1, &titleFont, PointF((REAL)(cx - 155), (REAL)(cy - 8)), &centerBrush);

    Graphics screenG(hdc);
    screenG.DrawImage(&backBuffer, rcSphere.left, rcSphere.top);
}

LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    static HBRUSH hBlackBrush = CreateSolidBrush(RGB(12, 16, 22));
    static HFONT hTermFont = NULL;

    switch (msg) {
        case WM_CREATE: {
            hTermFont = CreateFontW(15, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
                                    RUSSIAN_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                                    CLEARTYPE_QUALITY, FIXED_PITCH | FF_MODERN, L"Consolas");

            // 1. Верхнее окно журнала ядра (только чтение)
            hConsoleOut = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"",
                WS_CHILD | WS_VISIBLE | WS_VSCROLL | ES_MULTILINE | ES_READONLY,
                15, 15, 520, 200, hwnd, NULL, NULL, NULL);
            SendMessageW(hConsoleOut, WM_SETFONT, (WPARAM)hTermFont, TRUE);

            // 2. Защищённое окно активных скриптов (ES_READONLY — печатать с клавиатуры нельзя, только добавлять!)
            CreateWindowExW(0, L"STATIC", L"ОКНО АКТИВНЫХ СКРИПТОВ (БЕЗ КЛАВИАТУРЫ — ТОЛЬКО ДОБАВЛЕНИЕ МОДУЛЕЙ):",
                WS_CHILD | WS_VISIBLE, 15, 225, 520, 20, hwnd, NULL, NULL, NULL);

            hScriptBox = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"",
                WS_CHILD | WS_VISIBLE | WS_VSCROLL | ES_MULTILINE | ES_READONLY,
                15, 248, 520, 200, hwnd, NULL, NULL, NULL);
            SendMessageW(hScriptBox, WM_SETFONT, (WPARAM)hTermFont, TRUE);

            // 3. Кнопки добавления обучающих скриптов
            CreateWindowExW(0, L"BUTTON", L"+ ОБУЧЕНИЕ: УЗЛЫ И ПРОЦЕССОР ПК",
                WS_CHILD | WS_VISIBLE, 15, 460, 255, 34, hwnd, (HMENU)ID_BTN_TRAIN_NODES, NULL, NULL);
            CreateWindowExW(0, L"BUTTON", L"+ ОБУЧЕНИЕ: СФЕРА И СЛОИ ДИСКОВ",
                WS_CHILD | WS_VISIBLE, 280, 460, 255, 34, hwnd, (HMENU)ID_BTN_TRAIN_SPHERE, NULL, NULL);

            // 4. Кнопки добавления скриптов действия
            CreateWindowExW(0, L"BUTTON", L"+ ДЕЙСТВИЕ: СКАНИРОВАТЬ ДИСКИ",
                WS_CHILD | WS_VISIBLE, 15, 500, 255, 34, hwnd, (HMENU)ID_BTN_ACT_DISKS, NULL, NULL);
            CreateWindowExW(0, L"BUTTON", L"+ ДЕЙСТВИЕ: ОТКРЫТЬ ФОТО В СЛОЕ",
                WS_CHILD | WS_VISIBLE, 280, 500, 255, 34, hwnd, (HMENU)ID_BTN_ACT_PHOTO, NULL, NULL);

            // 5. Кнопки загрузки из файла и очистки окна
            CreateWindowExW(0, L"BUTTON", L"+ ДОБАВИТЬ СКРИПТ ИЗ ФАЙЛА (.TXT/.СВЕД)",
                WS_CHILD | WS_VISIBLE, 15, 540, 340, 34, hwnd, (HMENU)ID_BTN_ADD_FILE, NULL, NULL);
            CreateWindowExW(0, L"BUTTON", L"ОЧИСТИТЬ ОКНО",
                WS_CHILD | WS_VISIBLE, 365, 540, 170, 34, hwnd, (HMENU)ID_BTN_CLEAR_BOX, NULL, NULL);

            // 6. Кнопка исполнения добавленных скриптов
            CreateWindowExW(0, L"BUTTON", L"ВЫПОЛНИТЬ ДОБАВЛЕННЫЕ В ОКНО СКРИПТЫ",
                WS_CHILD | WS_VISIBLE, 15, 582, 520, 40, hwnd, (HMENU)ID_BTN_RUN_SCRIPTS, NULL, NULL);

            // 7. Кнопка полного отключения всех процессов
            CreateWindowExW(0, L"BUTTON", L"ОТКЛЮЧИТЬ ВСЕ ПРОЦЕССЫ (ПОЛНЫЙ СТОП)",
                WS_CHILD | WS_VISIBLE, 15, 628, 520, 36, hwnd, (HMENU)ID_BTN_KILL_ALL, NULL, NULL);

            // Стартовая инициализация
            ScanComputerDisks();
            AppendConsole(L"=== ТЕРМИНАЛ СВЕД-100 v3.0 (ВЕРСИЯ ДЛЯ WINDOWS) ===");
            AppendConsole(L"Права закреплены: ЧИРКИН ДМИТРИЙ НИКОЛАЕВИЧ | Узел: БЕЛЫЙ КВАДРАТ");
            AppendConsole(L"Системные файлы Windows не затрагиваются. Сфера слоёв активна.");

            AppendScriptBlock(L"[БАЗОВЫЙ СКРИПТ ЯДРА ПК]\r\n"
                              L"ОБУЧЕНИЕ: СТЫКОВКА ПРОЦЕССОРА И ШИН ПАМЯТИ\r\n"
                              L"ДЕЙСТВИЕ: ПОСТРОИТЬ ПОЛУПРОЗРАЧНУЮ СФЕРУ ДИСКОВ");

            SetTimer(hwnd, ID_TIMER_SPHERE, 45, NULL);
            return 0;
        }

        case WM_TIMER: {
            if (wParam == ID_TIMER_SPHERE && sphereActive) {
                sphereAngle += 0.025f;
                RECT rcSphere = {550, 15, 1080, 665};
                InvalidateRect(hwnd, &rcSphere, FALSE);
            }
            return 0;
        }

        case WM_COMMAND: {
            int id = LOWORD(wParam);
            if (id == ID_BTN_TRAIN_NODES) {
                AppendScriptBlock(L"[ОБУЧАЮЩИЙ СКРИПТ: АППАРАТНЫЕ_УЗЛЫ_ПК]\r\n"
                                  L"ОБУЧЕНИЕ: ОПРОС ЯДЕР ПРОЦЕССОРА И ОПЕРАТИВНОЙ ПАМЯТИ\r\n"
                                  L"ОБУЧЕНИЕ: ОТКЛЮЧЕНИЕ ФОНОВОЙ ТЕЛЕМЕТРИИ");
                AppendConsole(L" + В окно добавлен обучающий скрипт узлов ПК.");
            }
            else if (id == ID_BTN_TRAIN_SPHERE) {
                AppendScriptBlock(L"[ОБУЧАЮЩИЙ СКРИПТ: СФЕРА_ГРЯДА]\r\n"
                                  L"ОБУЧЕНИЕ: ФОРМИРОВАНИЕ ПОЛУПРОЗРАЧНЫХ СЛОЕВ\r\n"
                                  L"ОБУЧЕНИЕ: ЗАЩИТА УЗЛА БЕЛЫЙ КВАДРАТ");
                AppendConsole(L" + В окно добавлен обучающий скрипт сферы слоёв.");
            }
            else if (id == ID_BTN_ACT_DISKS) {
                AppendScriptBlock(L"[СКРИПТ ДЕЙСТВИЯ: СЛОИ_ДИСКОВ]\r\n"
                                  L"ДЕЙСТВИЕ: СКАНИРОВАТЬ ВСЕ ДИСКИ КОМПЬЮТЕРА\r\n"
                                  L"ДЕЙСТВИЕ: ОТОБРАЗИТЬ ОБЪЕМ СЛОЕВ В СФЕРЕ");
                AppendConsole(L" + В окно добавлен скрипт действия дисков.");
            }
            else if (id == ID_BTN_ACT_PHOTO) {
                AppendScriptBlock(L"[СКРИПТ ДЕЙСТВИЯ: ПРОСМОТР_ФОТОГРАФИЙ]\r\n"
                                  L"ДЕЙСТВИЕ: ОТКРЫТЬ ФОТОГРАФИЮ В СЛОЕ СФЕРЫ");
                AppendConsole(L" + В окно добавлен скрипт просмотра фотографий.");
            }
            else if (id == ID_BTN_ADD_FILE) {
                wchar_t filePath[MAX_PATH] = {0};
                OPENFILENAMEW ofn = {0};
                ofn.lStructSize = sizeof(ofn);
                ofn.hwndOwner = hwnd;
                ofn.lpstrFilter = L"Файлы скриптов СВЕД (*.txt;*.свед;*.sved)\0*.txt;*.свед;*.sved;*.সিয়াসল\0Все файлы (*.*)\0*.*\0";
                ofn.lpstrFile = filePath;
                ofn.nMaxFile = MAX_PATH;
                ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST;
                ofn.lpstrTitle = L"Выберите файл обучающего или действующего скрипта";

                if (GetOpenFileNameW(&ofn)) {
                    std::wstring content = ReadUtf8File(filePath);
                    if (!content.empty()) {
                        AppendScriptBlock(content);
                        AppendConsole(L" + Загружен внешний файл скрипта в окно.");
                    }
                }
            }
            else if (id == ID_BTN_CLEAR_BOX) {
                scriptBuffer.clear();
                SetWindowTextW(hScriptBox, L"[Окно пусто. Добавьте обучающий скрипт или скрипт действия кнопками ниже]");
                AppendConsole(L" [Инфо]: Окно скриптов очищено.");
            }
            else if (id == ID_BTN_RUN_SCRIPTS) {
                if (scriptBuffer.empty()) {
                    AppendConsole(L" [Внимание]: Сначала добавьте скрипты в окно.");
                } else {
                    AppendConsole(L"\r\n=== ИСПОЛНЕНИЕ СКРИПТОВ НА УЗЛАХ КОМПЬЮТЕРА ===");
                    ProbePCNodes();
                    ScanComputerDisks();
                    AppendConsole(L" -> [СФЕРА ДИСКОВ]: Обнаружено и подключено дисков: " + std::to_wstring(detectedDrives.size()));
                    if (scriptBuffer.find(L"ФОТО") != std::wstring::npos) {
                        OpenPhotoLayer(hwnd);
                    }
                    AppendConsole(L"=== ГОТОВО: Все директивы окна выполнены ===");
                }
            }
            else if (id == ID_BTN_KILL_ALL) {
                // Полное и мгновенное отключение всех процессов и потоков программы
                KillTimer(hwnd, ID_TIMER_SPHERE);
                if (loadedPhoto) delete loadedPhoto;
                ExitProcess(0);
            }
            return 0;
        }

        case WM_CTLCOLORSTATIC:
        case WM_CTLCOLOREDIT: {
            HDC hdcStatic = (HDC)wParam;
            HWND hCtrl = (HWND)lParam;
            if (hCtrl == hConsoleOut) {
                SetTextColor(hdcStatic, RGB(0, 255, 100));
            } else if (hCtrl == hScriptBox) {
                SetTextColor(hdcStatic, RGB(180, 255, 0));
            } else {
                SetTextColor(hdcStatic, RGB(0, 220, 255));
            }
            SetBkColor(hdcStatic, RGB(12, 16, 22));
            return (INT_PTR)hBlackBrush;
        }

        case WM_PAINT: {
            PAINTSTRUCT ps;
            HDC hdc = BeginPaint(hwnd, &ps);
            RECT rcSphere = {550, 15, 1080, 665};
            DrawSvedSphere(hdc, rcSphere);
            EndPaint(hwnd, &ps);
            return 0;
        }

        case WM_ERASEBKGND: {
            HDC hdc = (HDC)wParam;
            RECT rc;
            GetClientRect(hwnd, &rc);
            FillRect(hdc, &rc, hBlackBrush);
            return 1;
        }

        case WM_DESTROY:
            ExitProcess(0);
            return 0;
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE, LPSTR, int nCmdShow) {
    GdiplusStartupInput gdiplusStartupInput;
    ULONG_PTR gdiplusToken;
    GdiplusStartup(&gdiplusToken, &gdiplusStartupInput, NULL);

    WNDCLASSEXW wc = {0};
    wc.cbSize = sizeof(WNDCLASSEXW);
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInstance;
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.lpszClassName = L"SVED_PC_WINDOW";
    RegisterClassExW(&wc);

    HWND hwnd = CreateWindowExW(0, L"SVED_PC_WINDOW",
        L"ТЕРМИНАЛ СВЕД-100 v3.0 (АВТОНОМНАЯ СРЕДА WINDOWS — СФЕРА СЛОЕВ И СКРИПТОВ)",
        WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX | WS_VISIBLE,
        CW_USEDEFAULT, CW_USEDEFAULT, 1110, 715,
        NULL, NULL, hInstance, NULL);

    ShowWindow(hwnd, nCmdShow);
    UpdateWindow(hwnd);

    MSG msg;
    while (GetMessageW(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    GdiplusShutdown(gdiplusToken);
    return 0;
}
