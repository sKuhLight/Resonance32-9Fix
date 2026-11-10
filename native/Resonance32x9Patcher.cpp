#define UNICODE
#define _UNICODE
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <commctrl.h>
#include <shellapi.h>
#include <tlhelp32.h>
#include <uxtheme.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <cwchar>
#include <iterator>
#include <limits>
#include <string>
#include <vector>

#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "gdi32.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "user32.lib")
#pragma comment(lib, "uxtheme.lib")

namespace
{
constexpr wchar_t WindowClassName[] = L"Resonance32x9PatcherWindow";
constexpr wchar_t ProcessFileName[] = L"Resonance.exe";
constexpr float NativeAspectCap = 2.3333332538604736f;
constexpr float SuperUltrawideAspectCap = 3.555555582046509f;
constexpr UINT_PTR MonitorTimer = 1;
constexpr UINT_PTR LiveApplyTimer = 2;

constexpr int IdFovEnabled = 1001;
constexpr int IdFovMode = 1002;
constexpr int IdFovValue = 1003;
constexpr int IdFovSlider = 1004;
constexpr int IdApply = 1005;
constexpr int IdReset = 1006;

const COLORREF Canvas = RGB(13, 17, 23);
const COLORREF Card = RGB(22, 27, 34);
const COLORREF CardBorder = RGB(48, 54, 61);
const COLORREF TextPrimary = RGB(240, 246, 252);
const COLORREF TextMuted = RGB(139, 148, 158);
const COLORREF Accent = RGB(88, 166, 255);
const COLORREF Green = RGB(63, 185, 80);
const COLORREF Amber = RGB(210, 153, 34);
const COLORREF ErrorRed = RGB(220, 92, 92);

const std::vector<int> StockCapSignature = {
    0x48, 0x83, 0xEC, 0x28,
    0x48, 0x8B, 0x0D, -1, -1, -1, -1,
    0xC7, 0x05, -1, -1, -1, -1, 0xCD, 0xCC, 0xCC, 0x3F,
    0xC7, 0x05, -1, -1, -1, -1, 0x55, 0x55, 0x15, 0x40,
};

const std::vector<int> UncappedSignature = {
    0x48, 0x83, 0xEC, 0x28,
    0x48, 0x8B, 0x0D, -1, -1, -1, -1,
    0xC7, 0x05, -1, -1, -1, -1, 0xCD, 0xCC, 0xCC, 0x3F,
    0xC7, 0x05, -1, -1, -1, -1, 0x39, 0x8E, 0x63, 0x40,
};

const std::vector<int> StockGameplayFovSignature = {
    0xF3, 0x0F, 0x10, 0x8B, 0x54, 0x03, 0x00, 0x00,
    0x49, 0x8B, 0xCF, 0xE8, -1, -1, -1, -1,
    0xF3, 0x0F, 0x10, 0x8B, 0x5C, 0x03, 0x00, 0x00,
    0x49, 0x8B, 0xCF, 0xE8, -1, -1, -1, -1,
};

const std::vector<int> PatchedGameplayFovSignature = {
    0xF3, 0x0F, 0x10, 0x8B, 0x54, 0x03, 0x00, 0x00,
    0x49, 0x8B, 0xCF, 0xE8, -1, -1, -1, -1,
    0xF3, 0x0F, 0x10, 0x0D, -1, -1, -1, -1,
    0x49, 0x8B, 0xCF, 0xE8, -1, -1, -1, -1,
};

const std::vector<int> FovValueCaveSignature = {
    0xC3,
    0xCC, 0xCC, 0xCC, 0xCC, 0xCC, 0xCC, 0xCC, 0xCC,
    0xCC, 0xCC, 0xCC, 0xCC, 0xCC, 0xCC, 0xCC, 0xCC,
    0x48, 0x89, 0x5C, 0x24, 0x10, 0x57, 0x48, 0x83, 0xEC, 0x60,
    0xF3, 0x0F, 0x59, 0x0D,
};

const BYTE StockGameplayFovLoad[] = {0xF3, 0x0F, 0x10, 0x8B, 0x5C, 0x03, 0x00, 0x00};

struct PatchError
{
    std::wstring message;
};

struct ModuleInfo
{
    uintptr_t base = 0;
    DWORD size = 0;
};

struct Session
{
    HANDLE process = nullptr;
    DWORD processId = 0;
    uintptr_t aspectAddress = 0;
    uintptr_t managerSlot = 0;
    uintptr_t fovLoadAddress = 0;
    uintptr_t fovValueAddress = 0;
    bool fovPatched = false;
};

HINSTANCE g_instance = nullptr;
HWND g_window = nullptr;
HWND g_fovEnabled = nullptr;
HWND g_fovMode = nullptr;
HWND g_fovValue = nullptr;
HWND g_fovSlider = nullptr;
HWND g_applyButton = nullptr;
HWND g_resetButton = nullptr;
HFONT g_titleFont = nullptr;
HFONT g_headingFont = nullptr;
HFONT g_bodyFont = nullptr;
HFONT g_smallFont = nullptr;
HBRUSH g_canvasBrush = nullptr;
HBRUSH g_cardBrush = nullptr;
Session g_session;
std::wstring g_status = L"Looking for Resonance.exe ...";
std::wstring g_conversion;
COLORREF g_statusColor = Amber;
COLORREF g_statusTextColor = TextMuted;
float g_activeVerticalFov = std::numeric_limits<float>::quiet_NaN();
int g_lastMode = 0;
bool g_syncingControls = false;
bool g_busy = false;

std::wstring WindowsError(const wchar_t* action)
{
    return std::wstring(action) + L" (Windows error " + std::to_wstring(GetLastError()) + L")";
}

void ThrowWindowsError(const wchar_t* action)
{
    throw PatchError{WindowsError(action)};
}

bool Approximately(float left, float right)
{
    return std::fabs(left - right) < 0.0001f;
}

float HorizontalToVerticalFov(float horizontalFov)
{
    constexpr float Pi = 3.14159265358979323846f;
    const float horizontalRadians = horizontalFov * Pi / 180.0f;
    const float verticalRadians = 2.0f * std::atan(std::tan(horizontalRadians / 2.0f) / SuperUltrawideAspectCap);
    return verticalRadians * 180.0f / Pi;
}

float VerticalToHorizontalFov(float verticalFov)
{
    constexpr float Pi = 3.14159265358979323846f;
    const float verticalRadians = verticalFov * Pi / 180.0f;
    const float horizontalRadians = 2.0f * std::atan(std::tan(verticalRadians / 2.0f) * SuperUltrawideAspectCap);
    return horizontalRadians * 180.0f / Pi;
}

DWORD FindGameProcess()
{
    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snapshot == INVALID_HANDLE_VALUE)
    {
        return 0;
    }

    PROCESSENTRY32W entry{};
    entry.dwSize = sizeof(entry);
    DWORD processId = 0;
    if (Process32FirstW(snapshot, &entry))
    {
        do
        {
            if (_wcsicmp(entry.szExeFile, ProcessFileName) == 0)
            {
                processId = entry.th32ProcessID;
                break;
            }
        } while (Process32NextW(snapshot, &entry));
    }
    CloseHandle(snapshot);
    return processId;
}

ModuleInfo FindMainModule(DWORD processId)
{
    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE | TH32CS_SNAPMODULE32, processId);
    if (snapshot == INVALID_HANDLE_VALUE)
    {
        ThrowWindowsError(L"The game module could not be inspected");
    }

    MODULEENTRY32W entry{};
    entry.dwSize = sizeof(entry);
    ModuleInfo result;
    if (Module32FirstW(snapshot, &entry))
    {
        do
        {
            if (_wcsicmp(entry.szModule, ProcessFileName) == 0)
            {
                result.base = reinterpret_cast<uintptr_t>(entry.modBaseAddr);
                result.size = entry.modBaseSize;
                break;
            }
        } while (Module32NextW(snapshot, &entry));
    }
    CloseHandle(snapshot);
    if (result.base == 0 || result.size == 0)
    {
        throw PatchError{L"The Resonance.exe main module has not loaded yet."};
    }
    return result;
}

std::vector<BYTE> ReadMemory(HANDLE process, uintptr_t address, SIZE_T size)
{
    std::vector<BYTE> data(size);
    SIZE_T read = 0;
    if (!ReadProcessMemory(process, reinterpret_cast<LPCVOID>(address), data.data(), size, &read) || read != size)
    {
        ThrowWindowsError(L"Process memory could not be read");
    }
    return data;
}

template <typename T>
T ReadValue(HANDLE process, uintptr_t address)
{
    const auto bytes = ReadMemory(process, address, sizeof(T));
    T value{};
    std::memcpy(&value, bytes.data(), sizeof(T));
    return value;
}

void WriteMemoryBytes(HANDLE process, uintptr_t address, const void* data, SIZE_T size)
{
    SIZE_T written = 0;
    if (!WriteProcessMemory(process, reinterpret_cast<LPVOID>(address), data, size, &written) || written != size)
    {
        ThrowWindowsError(L"Process memory could not be written");
    }
}

void WriteProtected(HANDLE process, uintptr_t address, const void* data, SIZE_T size)
{
    DWORD oldProtection = 0;
    if (!VirtualProtectEx(process, reinterpret_cast<LPVOID>(address), size, PAGE_EXECUTE_READWRITE, &oldProtection))
    {
        ThrowWindowsError(L"Memory protection could not be changed");
    }

    try
    {
        WriteMemoryBytes(process, address, data, size);
        FlushInstructionCache(process, reinterpret_cast<LPCVOID>(address), size);
    }
    catch (...)
    {
        DWORD ignored = 0;
        VirtualProtectEx(process, reinterpret_cast<LPVOID>(address), size, oldProtection, &ignored);
        throw;
    }
    DWORD ignored = 0;
    VirtualProtectEx(process, reinterpret_cast<LPVOID>(address), size, oldProtection, &ignored);
}

std::vector<size_t> FindMatches(const std::vector<BYTE>& data, const std::vector<int>& pattern)
{
    std::vector<size_t> matches;
    if (pattern.empty() || data.size() < pattern.size())
    {
        return matches;
    }

    for (size_t offset = 0; offset <= data.size() - pattern.size(); ++offset)
    {
        if (pattern[0] >= 0 && data[offset] != static_cast<BYTE>(pattern[0]))
        {
            continue;
        }
        bool matched = true;
        for (size_t index = 1; index < pattern.size(); ++index)
        {
            if (pattern[index] >= 0 && data[offset + index] != static_cast<BYTE>(pattern[index]))
            {
                matched = false;
                break;
            }
        }
        if (matched)
        {
            matches.push_back(offset);
        }
    }
    return matches;
}

int32_t ReadInt32(const std::vector<BYTE>& data, size_t offset)
{
    int32_t value = 0;
    std::memcpy(&value, data.data() + offset, sizeof(value));
    return value;
}

void RequestViewportRefresh(const Session& session)
{
    const uintptr_t manager = ReadValue<uintptr_t>(session.process, session.managerSlot);
    if (manager != 0)
    {
        const BYTE refresh = 1;
        WriteMemoryBytes(session.process, manager + 0x10D0, &refresh, sizeof(refresh));
    }
}

void ApplyFov(Session& session, float verticalFov)
{
    if (verticalFov < 20.0f || verticalFov > 120.0f)
    {
        throw PatchError{L"VFOV must be between 20 and 120 degrees."};
    }

    constexpr float Pi = 3.14159265358979323846f;
    const float radians = verticalFov * Pi / 180.0f;
    WriteProtected(session.process, session.fovValueAddress, &radians, sizeof(radians));
    if (!session.fovPatched)
    {
        const intptr_t displacement64 = static_cast<intptr_t>(session.fovValueAddress) -
                                        static_cast<intptr_t>(session.fovLoadAddress + 8);
        if (displacement64 < INT32_MIN || displacement64 > INT32_MAX)
        {
            throw PatchError{L"The FOV storage location is out of range."};
        }

        BYTE instruction[] = {0xF3, 0x0F, 0x10, 0x0D, 0, 0, 0, 0};
        const int32_t displacement = static_cast<int32_t>(displacement64);
        std::memcpy(instruction + 4, &displacement, sizeof(displacement));
        WriteProtected(session.process, session.fovLoadAddress, instruction, sizeof(instruction));
        session.fovPatched = true;
    }
}

void ResetFov(Session& session)
{
    if (!session.fovPatched)
    {
        return;
    }
    WriteProtected(session.process, session.fovLoadAddress, StockGameplayFovLoad, sizeof(StockGameplayFovLoad));
    const BYTE padding[] = {0xCC, 0xCC, 0xCC, 0xCC};
    WriteProtected(session.process, session.fovValueAddress, padding, sizeof(padding));
    session.fovPatched = false;
}

Session AttachGame(DWORD processId, float initialFov, bool resetFov)
{
    Session session;
    session.processId = processId;
    session.process = OpenProcess(
        PROCESS_QUERY_INFORMATION | PROCESS_VM_OPERATION | PROCESS_VM_READ | PROCESS_VM_WRITE,
        FALSE,
        processId);
    if (!session.process)
    {
        ThrowWindowsError(L"The game process could not be opened");
    }

    try
    {
        const ModuleInfo module = FindMainModule(processId);
        auto image = ReadMemory(session.process, module.base, module.size);

        const auto stockAspectMatches = FindMatches(image, StockCapSignature);
        size_t callbackOffset = 0;
        if (stockAspectMatches.size() == 1)
        {
            callbackOffset = stockAspectMatches[0];
            WriteProtected(session.process, module.base + callbackOffset + 27,
                           &SuperUltrawideAspectCap, sizeof(SuperUltrawideAspectCap));
        }
        else if (stockAspectMatches.empty())
        {
            const auto activeMatches = FindMatches(image, UncappedSignature);
            if (activeMatches.size() < 2)
            {
                throw PatchError{L"The aspect-ratio signature does not match this game version."};
            }
            callbackOffset = *std::min_element(activeMatches.begin(), activeMatches.end());
        }
        else
        {
            throw PatchError{L"The aspect-ratio signature is not unique."};
        }

        const int32_t aspectDisplacement = ReadInt32(image, callbackOffset + 23);
        session.aspectAddress = module.base + callbackOffset + 31 + aspectDisplacement;
        const float currentAspect = ReadValue<float>(session.process, session.aspectAddress);
        // Game updates may change the stock aspect cap (1.2.x stored 21/9, 1.3.x stores 2.4).
        // Any sane aspect value proves the global is live; the sentinel/uninitialised marker
        // (~1e37) and NaN are rejected. The value is forced to 32/9 immediately below.
        if (!(currentAspect > 1.0f && currentAspect < 5.0f))
        {
            throw PatchError{L"An unexpected aspect-ratio value was found."};
        }
        WriteMemoryBytes(session.process, session.aspectAddress,
                         &SuperUltrawideAspectCap, sizeof(SuperUltrawideAspectCap));

        const int32_t managerDisplacement = ReadInt32(image, callbackOffset + 7);
        session.managerSlot = module.base + callbackOffset + 11 + managerDisplacement;
        RequestViewportRefresh(session);

        const auto stockFovMatches = FindMatches(image, StockGameplayFovSignature);
        const auto activeFovMatches = FindMatches(image, PatchedGameplayFovSignature);
        if (stockFovMatches.size() + activeFovMatches.size() != 1)
        {
            throw PatchError{
                L"The gameplay FOV signature does not match this game version or is not unique."};
        }

        const size_t fovSignatureOffset = stockFovMatches.size() == 1
            ? stockFovMatches[0]
            : activeFovMatches[0];
        session.fovLoadAddress = module.base + fovSignatureOffset + 16;
        session.fovPatched = activeFovMatches.size() == 1;
        if (session.fovPatched)
        {
            const int32_t displacement = ReadInt32(image, fovSignatureOffset + 20);
            session.fovValueAddress = session.fovLoadAddress + 8 + displacement;
        }
        else
        {
            const auto caves = FindMatches(image, FovValueCaveSignature);
            if (caves.size() != 1)
            {
                throw PatchError{L"The FOV value storage location is not unique."};
            }
            session.fovValueAddress = module.base + caves[0] + 1;
        }

        if (resetFov)
        {
            ResetFov(session);
        }
        else if (std::isfinite(initialFov))
        {
            ApplyFov(session, initialFov);
        }
        return session;
    }
    catch (...)
    {
        CloseHandle(session.process);
        session.process = nullptr;
        throw;
    }
}

bool IsSessionAlive(const Session& session)
{
    if (!session.process)
    {
        return false;
    }
    DWORD exitCode = 0;
    return GetExitCodeProcess(session.process, &exitCode) && exitCode == STILL_ACTIVE;
}

bool Maintain(Session& session, float verticalFov)
{
    if (!IsSessionAlive(session))
    {
        return false;
    }

    const float currentAspect = ReadValue<float>(session.process, session.aspectAddress);
    if (!Approximately(currentAspect, SuperUltrawideAspectCap))
    {
        WriteMemoryBytes(session.process, session.aspectAddress,
                         &SuperUltrawideAspectCap, sizeof(SuperUltrawideAspectCap));
        RequestViewportRefresh(session);
    }

    if (std::isfinite(verticalFov) && session.fovPatched)
    {
        constexpr float Pi = 3.14159265358979323846f;
        const float radians = verticalFov * Pi / 180.0f;
        const float current = ReadValue<float>(session.process, session.fovValueAddress);
        if (!Approximately(current, radians))
        {
            WriteProtected(session.process, session.fovValueAddress, &radians, sizeof(radians));
        }
    }
    return true;
}

void CloseSession()
{
    if (g_session.process)
    {
        CloseHandle(g_session.process);
    }
    g_session = {};
    g_activeVerticalFov = std::numeric_limits<float>::quiet_NaN();
}

HFONT CreateUiFont(int pointSize, int weight = FW_NORMAL)
{
    HDC dc = GetDC(nullptr);
    const int dpi = GetDeviceCaps(dc, LOGPIXELSY);
    ReleaseDC(nullptr, dc);
    return CreateFontW(
        -MulDiv(pointSize, dpi, 72), 0, 0, 0, weight, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
        CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
}

void SetControlFont(HWND control, HFONT font)
{
    SendMessageW(control, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
}

void SetStatus(const std::wstring& text, COLORREF dotColor, COLORREF textColor)
{
    g_status = text;
    g_statusColor = dotColor;
    g_statusTextColor = textColor;
    if (g_window)
    {
        InvalidateRect(g_window, nullptr, FALSE);
    }
}

void DrawTextAt(HDC dc, const wchar_t* text, RECT rect, HFONT font, COLORREF color, UINT format)
{
    const HFONT oldFont = static_cast<HFONT>(SelectObject(dc, font));
    SetTextColor(dc, color);
    SetBkMode(dc, TRANSPARENT);
    DrawTextW(dc, text, -1, &rect, format);
    SelectObject(dc, oldFont);
}

void DrawCard(HDC dc, RECT rect, COLORREF fill)
{
    HBRUSH brush = CreateSolidBrush(fill);
    HPEN pen = CreatePen(PS_SOLID, 1, CardBorder);
    HGDIOBJ oldBrush = SelectObject(dc, brush);
    HGDIOBJ oldPen = SelectObject(dc, pen);
    RoundRect(dc, rect.left, rect.top, rect.right, rect.bottom, 16, 16);
    SelectObject(dc, oldPen);
    SelectObject(dc, oldBrush);
    DeleteObject(pen);
    DeleteObject(brush);
}

void PaintInterface(HWND window)
{
    PAINTSTRUCT paint{};
    HDC dc = BeginPaint(window, &paint);
    RECT client{};
    GetClientRect(window, &client);
    FillRect(dc, &client, g_canvasBrush);

    DrawTextAt(dc, L"RESONANCE", {32, 22, 588, 58}, g_titleFont, TextPrimary, DT_LEFT | DT_SINGLELINE);
    DrawTextAt(dc, L"32:9 viewport & direct gameplay FOV patch",
               {34, 60, 584, 84}, g_bodyFont, TextMuted, DT_LEFT | DT_SINGLELINE);

    DrawCard(dc, {32, 96, 588, 150}, RGB(18, 23, 30));
    HBRUSH dot = CreateSolidBrush(g_statusColor);
    HGDIOBJ oldBrush = SelectObject(dc, dot);
    HPEN nullPen = static_cast<HPEN>(GetStockObject(NULL_PEN));
    HGDIOBJ oldPen = SelectObject(dc, nullPen);
    Ellipse(dc, 50, 116, 62, 128);
    SelectObject(dc, oldPen);
    SelectObject(dc, oldBrush);
    DeleteObject(dot);
    DrawTextAt(dc, g_status.c_str(), {74, 110, 565, 138}, g_bodyFont, g_statusTextColor,
               DT_LEFT | DT_VCENTER | DT_SINGLELINE);

    DrawCard(dc, {32, 166, 588, 258}, Card);
    DrawTextAt(dc, L"32:9 VIEWPORT", {52, 180, 280, 204}, g_headingFont, TextPrimary,
               DT_LEFT | DT_SINGLELINE);
    DrawTextAt(dc, L"Raises the internal 21:9 limit to 5120×1440.",
               {52, 211, 470, 237}, g_bodyFont, TextMuted, DT_LEFT | DT_SINGLELINE);
    DrawTextAt(dc, L"ALWAYS ACTIVE", {455, 182, 565, 208}, g_smallFont, Green,
               DT_CENTER | DT_VCENTER | DT_SINGLELINE);

    DrawCard(dc, {32, 274, 588, 488}, Card);
    DrawTextAt(dc, L"MODE", {52, 330, 202, 350}, g_smallFont, TextMuted, DT_LEFT | DT_SINGLELINE);
    DrawTextAt(dc, L"VALUE", {222, 330, 342, 350}, g_smallFont, TextMuted, DT_LEFT | DT_SINGLELINE);
    DrawTextAt(dc, g_conversion.c_str(), {360, 352, 560, 380}, g_headingFont, Accent,
               DT_LEFT | DT_VCENTER | DT_SINGLELINE);
    DrawTextAt(dc, L"Changes apply live after connecting. 105° VFOV was our clearly visible test.",
               {52, 449, 560, 474}, g_smallFont, TextMuted, DT_LEFT | DT_SINGLELINE);

    DrawTextAt(dc, L"Steam & Game Pass  •  native runtime patch  •  no game files are modified",
               {32, 572, 588, 594}, g_smallFont, TextMuted, DT_CENTER | DT_SINGLELINE);
    EndPaint(window, &paint);
}

int CurrentValue()
{
    wchar_t text[32]{};
    GetWindowTextW(g_fovValue, text, static_cast<int>(std::size(text)));
    int value = _wtoi(text);
    const int mode = static_cast<int>(SendMessageW(g_fovMode, CB_GETCURSEL, 0, 0));
    return std::clamp(value, mode == 0 ? 20 : 60, mode == 0 ? 120 : 175);
}

float CurrentVerticalFov()
{
    const float value = static_cast<float>(CurrentValue());
    return SendMessageW(g_fovMode, CB_GETCURSEL, 0, 0) == 0
        ? value
        : HorizontalToVerticalFov(value);
}

void UpdateConversion()
{
    const float value = static_cast<float>(CurrentValue());
    const bool verticalMode = SendMessageW(g_fovMode, CB_GETCURSEL, 0, 0) == 0;
    const float converted = verticalMode ? VerticalToHorizontalFov(value) : HorizontalToVerticalFov(value);
    wchar_t buffer[64]{};
    swprintf_s(buffer, verticalMode ? L"≈ %.1f° HFOV" : L"≈ %.1f° VFOV", converted);
    g_conversion = buffer;
    InvalidateRect(g_window, nullptr, FALSE);
}

void SetValue(int value)
{
    const int mode = static_cast<int>(SendMessageW(g_fovMode, CB_GETCURSEL, 0, 0));
    value = std::clamp(value, mode == 0 ? 20 : 60, mode == 0 ? 120 : 175);
    g_syncingControls = true;
    SetWindowTextW(g_fovValue, std::to_wstring(value).c_str());
    SendMessageW(g_fovSlider, TBM_SETPOS, TRUE, value);
    g_syncingControls = false;
    UpdateConversion();
}

void ConfigureMode(int newMode)
{
    const float oldValue = static_cast<float>(CurrentValue());
    float converted = oldValue;
    if (newMode == 0 && g_lastMode == 1)
    {
        converted = HorizontalToVerticalFov(oldValue);
    }
    else if (newMode == 1 && g_lastMode == 0)
    {
        converted = VerticalToHorizontalFov(oldValue);
    }
    SendMessageW(g_fovSlider, TBM_SETRANGE, TRUE, MAKELONG(newMode == 0 ? 20 : 60, newMode == 0 ? 120 : 175));
    SendMessageW(g_fovSlider, TBM_SETTICFREQ, newMode == 0 ? 10 : 15, 0);
    g_lastMode = newMode;
    SetValue(static_cast<int>(std::lround(converted)));
}

bool FovOverrideEnabled()
{
    return SendMessageW(g_fovEnabled, BM_GETCHECK, 0, 0) == BST_CHECKED;
}

std::wstring ActiveStatus()
{
    if (std::isfinite(g_activeVerticalFov))
    {
        wchar_t buffer[96]{};
        swprintf_s(buffer, L"32:9 active  •  gameplay FOV %.1f° VFOV", g_activeVerticalFov);
        return buffer;
    }
    return L"32:9 active  •  original dynamic FOV";
}

void ShowPatchError(const PatchError& error)
{
    SetStatus(L"Runtime patch error", ErrorRed, TextPrimary);
    MessageBoxW(g_window, error.message.c_str(), L"Patch error", MB_OK | MB_ICONERROR);
}

void SetBusy(bool busy)
{
    g_busy = busy;
    EnableWindow(g_applyButton, !busy);
    EnableWindow(g_resetButton, !busy && g_session.process != nullptr);
    SetCursor(LoadCursorW(nullptr, busy ? IDC_WAIT : IDC_ARROW));
}

void ApplyLiveFov()
{
    if (!g_session.process || !FovOverrideEnabled())
    {
        return;
    }
    try
    {
        g_activeVerticalFov = CurrentVerticalFov();
        ApplyFov(g_session, g_activeVerticalFov);
        SetStatus(ActiveStatus(), Green, TextPrimary);
    }
    catch (const PatchError& error)
    {
        ShowPatchError(error);
    }
}

void ScheduleLiveApply()
{
    if (!g_syncingControls && g_session.process && FovOverrideEnabled())
    {
        KillTimer(g_window, LiveApplyTimer);
        SetTimer(g_window, LiveApplyTimer, 180, nullptr);
    }
}

void ConnectOrApply()
{
    if (g_busy)
    {
        return;
    }
    if (g_session.process)
    {
        try
        {
            if (FovOverrideEnabled())
            {
                g_activeVerticalFov = CurrentVerticalFov();
                ApplyFov(g_session, g_activeVerticalFov);
            }
            else
            {
                ResetFov(g_session);
                g_activeVerticalFov = std::numeric_limits<float>::quiet_NaN();
            }
            SetStatus(ActiveStatus(), Green, TextPrimary);
        }
        catch (const PatchError& error)
        {
            ShowPatchError(error);
        }
        return;
    }

    const DWORD processId = FindGameProcess();
    if (!processId)
    {
        SetStatus(L"Start Resonance through Steam or Game Pass first.", Amber, TextMuted);
        MessageBoxW(
            g_window,
            L"Start Resonance through Steam, the Xbox app, or the Microsoft Store.\n\n"
            L"The patcher will detect the game automatically as soon as Resonance.exe is running.",
            L"Resonance.exe is not running",
            MB_OK | MB_ICONINFORMATION);
        return;
    }

    SetBusy(true);
    SetStatus(L"Connecting and validating game signatures ...", Accent, TextPrimary);
    UpdateWindow(g_window);
    try
    {
        const float initialFov = FovOverrideEnabled()
            ? CurrentVerticalFov()
            : std::numeric_limits<float>::quiet_NaN();
        g_session = AttachGame(processId, initialFov, false);
        g_activeVerticalFov = initialFov;
        SetWindowTextW(g_applyButton, L"APPLY CHANGES");
        EnableWindow(g_resetButton, TRUE);
        SetStatus(ActiveStatus(), Green, TextPrimary);
    }
    catch (const PatchError& error)
    {
        SetStatus(L"The patch could not be applied.", ErrorRed, TextPrimary);
        MessageBoxW(g_window, error.message.c_str(), L"Patch error", MB_OK | MB_ICONERROR);
    }
    SetBusy(false);
}

void RestoreFov()
{
    if (!g_session.process)
    {
        return;
    }
    try
    {
        ResetFov(g_session);
        g_activeVerticalFov = std::numeric_limits<float>::quiet_NaN();
        SendMessageW(g_fovEnabled, BM_SETCHECK, BST_UNCHECKED, 0);
        EnableWindow(g_fovMode, FALSE);
        EnableWindow(g_fovValue, FALSE);
        EnableWindow(g_fovSlider, FALSE);
        SetStatus(L"32:9 active  •  original dynamic FOV restored", Green, TextPrimary);
    }
    catch (const PatchError& error)
    {
        ShowPatchError(error);
    }
}

void GameExited()
{
    CloseSession();
    EnableWindow(g_resetButton, FALSE);
    SetWindowTextW(g_applyButton, L"CONNECT TO RESONANCE.EXE");
    SetStatus(L"Game closed  •  ready for the next launch", Amber, TextMuted);
}

void DetectGame()
{
    const DWORD processId = FindGameProcess();
    if (!processId)
    {
        SetWindowTextW(g_applyButton, L"CONNECT TO RESONANCE.EXE");
        SetStatus(L"Start the game through Steam or Game Pass", Amber, TextMuted);
    }
    else
    {
        SetWindowTextW(g_applyButton, L"APPLY 32:9 & FOV");
        SetStatus(L"Resonance.exe found  •  PID " + std::to_wstring(processId), Accent, TextPrimary);
    }
}

void HandleMonitorTimer()
{
    if (g_busy)
    {
        return;
    }
    if (!g_session.process)
    {
        DetectGame();
        return;
    }
    try
    {
        if (!Maintain(g_session, g_activeVerticalFov))
        {
            GameExited();
        }
    }
    catch (const PatchError& error)
    {
        ShowPatchError(error);
    }
}

void DrawOwnerButton(const DRAWITEMSTRUCT* item)
{
    const bool apply = item->CtlID == IdApply;
    const bool disabled = (item->itemState & ODS_DISABLED) != 0;
    const bool pressed = (item->itemState & ODS_SELECTED) != 0;
    COLORREF background = apply ? Accent : CardBorder;
    if (pressed)
    {
        background = apply ? RGB(56, 139, 230) : RGB(65, 72, 82);
    }
    if (disabled)
    {
        background = RGB(45, 49, 56);
    }

    HBRUSH brush = CreateSolidBrush(background);
    HPEN pen = CreatePen(PS_SOLID, 1, background);
    HGDIOBJ oldBrush = SelectObject(item->hDC, brush);
    HGDIOBJ oldPen = SelectObject(item->hDC, pen);
    RoundRect(item->hDC, item->rcItem.left, item->rcItem.top,
              item->rcItem.right, item->rcItem.bottom, 10, 10);
    SelectObject(item->hDC, oldPen);
    SelectObject(item->hDC, oldBrush);
    DeleteObject(pen);
    DeleteObject(brush);

    wchar_t text[96]{};
    GetWindowTextW(item->hwndItem, text, static_cast<int>(std::size(text)));
    RECT textRect = item->rcItem;
    DrawTextAt(item->hDC, text, textRect, g_headingFont,
               disabled ? TextMuted : (apply ? Canvas : TextPrimary),
               DT_CENTER | DT_VCENTER | DT_SINGLELINE);
}

void CreateControls(HWND window)
{
    g_fovEnabled = CreateWindowExW(
        0, L"BUTTON", L"Override gameplay FOV",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_AUTOCHECKBOX,
        52, 290, 280, 30, window, reinterpret_cast<HMENU>(static_cast<INT_PTR>(IdFovEnabled)), g_instance, nullptr);
    SendMessageW(g_fovEnabled, BM_SETCHECK, BST_CHECKED, 0);

    g_fovMode = CreateWindowExW(
        0, WC_COMBOBOXW, nullptr,
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | CBS_DROPDOWNLIST | WS_VSCROLL,
        52, 352, 150, 140, window, reinterpret_cast<HMENU>(static_cast<INT_PTR>(IdFovMode)), g_instance, nullptr);
    SendMessageW(g_fovMode, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"VFOV (vertical)"));
    SendMessageW(g_fovMode, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"HFOV (horizontal)"));
    SendMessageW(g_fovMode, CB_SETCURSEL, 0, 0);

    g_fovValue = CreateWindowExW(
        WS_EX_CLIENTEDGE, L"EDIT", L"80",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_NUMBER | ES_CENTER | ES_AUTOHSCROLL,
        222, 352, 120, 29, window, reinterpret_cast<HMENU>(static_cast<INT_PTR>(IdFovValue)), g_instance, nullptr);
    SendMessageW(g_fovValue, EM_SETLIMITTEXT, 3, 0);

    g_fovSlider = CreateWindowExW(
        0, TRACKBAR_CLASSW, nullptr,
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | TBS_AUTOTICKS,
        48, 394, 516, 48, window, reinterpret_cast<HMENU>(static_cast<INT_PTR>(IdFovSlider)), g_instance, nullptr);
    SendMessageW(g_fovSlider, TBM_SETRANGE, TRUE, MAKELONG(20, 120));
    SendMessageW(g_fovSlider, TBM_SETTICFREQ, 10, 0);
    SendMessageW(g_fovSlider, TBM_SETPOS, TRUE, 80);

    g_applyButton = CreateWindowExW(
        0, L"BUTTON", L"CONNECT TO RESONANCE.EXE",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW,
        32, 510, 362, 48, window, reinterpret_cast<HMENU>(static_cast<INT_PTR>(IdApply)), g_instance, nullptr);
    g_resetButton = CreateWindowExW(
        0, L"BUTTON", L"RESTORE FOV",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW,
        410, 510, 178, 48, window, reinterpret_cast<HMENU>(static_cast<INT_PTR>(IdReset)), g_instance, nullptr);
    EnableWindow(g_resetButton, FALSE);

    SetControlFont(g_fovEnabled, g_headingFont);
    SetControlFont(g_fovMode, g_bodyFont);
    SetControlFont(g_fovValue, g_bodyFont);
    SetWindowTheme(g_fovEnabled, L"DarkMode_Explorer", nullptr);
    SetWindowTheme(g_fovMode, L"DarkMode_CFD", nullptr);
    SetWindowTheme(g_fovValue, L"DarkMode_CFD", nullptr);
    SetWindowTheme(g_fovSlider, L"DarkMode_Explorer", nullptr);
    UpdateConversion();
}

LRESULT CALLBACK WindowProcedure(HWND window, UINT message, WPARAM wParam, LPARAM lParam)
{
    switch (message)
    {
    case WM_CREATE:
        g_window = window;
        CreateControls(window);
        SetTimer(window, MonitorTimer, 750, nullptr);
        DetectGame();
        return 0;

    case WM_COMMAND:
    {
        const int id = LOWORD(wParam);
        const int notification = HIWORD(wParam);
        if (id == IdApply && notification == BN_CLICKED)
        {
            ConnectOrApply();
            return 0;
        }
        if (id == IdReset && notification == BN_CLICKED)
        {
            RestoreFov();
            return 0;
        }
        if (id == IdFovEnabled && notification == BN_CLICKED)
        {
            const bool enabled = FovOverrideEnabled();
            EnableWindow(g_fovMode, enabled);
            EnableWindow(g_fovValue, enabled);
            EnableWindow(g_fovSlider, enabled);
            if (g_session.process)
            {
                if (enabled)
                {
                    ScheduleLiveApply();
                }
                else
                {
                    try
                    {
                        ResetFov(g_session);
                        g_activeVerticalFov = std::numeric_limits<float>::quiet_NaN();
                        SetStatus(L"32:9 active  •  original dynamic FOV", Green, TextPrimary);
                    }
                    catch (const PatchError& error)
                    {
                        ShowPatchError(error);
                    }
                }
            }
            return 0;
        }
        if (id == IdFovMode && notification == CBN_SELCHANGE)
        {
            ConfigureMode(static_cast<int>(SendMessageW(g_fovMode, CB_GETCURSEL, 0, 0)));
            ScheduleLiveApply();
            return 0;
        }
        if (id == IdFovValue && notification == EN_CHANGE && !g_syncingControls)
        {
            const int value = CurrentValue();
            SendMessageW(g_fovSlider, TBM_SETPOS, TRUE, value);
            UpdateConversion();
            ScheduleLiveApply();
            return 0;
        }
        break;
    }

    case WM_HSCROLL:
        if (reinterpret_cast<HWND>(lParam) == g_fovSlider)
        {
            SetValue(static_cast<int>(SendMessageW(g_fovSlider, TBM_GETPOS, 0, 0)));
            ScheduleLiveApply();
            return 0;
        }
        break;

    case WM_TIMER:
        if (wParam == MonitorTimer)
        {
            HandleMonitorTimer();
            return 0;
        }
        if (wParam == LiveApplyTimer)
        {
            KillTimer(window, LiveApplyTimer);
            ApplyLiveFov();
            return 0;
        }
        break;

    case WM_DRAWITEM:
        DrawOwnerButton(reinterpret_cast<const DRAWITEMSTRUCT*>(lParam));
        return TRUE;

    case WM_CTLCOLORBTN:
    case WM_CTLCOLORSTATIC:
    {
        HDC dc = reinterpret_cast<HDC>(wParam);
        SetTextColor(dc, TextPrimary);
        SetBkColor(dc, Card);
        return reinterpret_cast<LRESULT>(g_cardBrush);
    }

    case WM_CTLCOLOREDIT:
    case WM_CTLCOLORLISTBOX:
    {
        HDC dc = reinterpret_cast<HDC>(wParam);
        SetTextColor(dc, TextPrimary);
        SetBkColor(dc, Canvas);
        return reinterpret_cast<LRESULT>(g_canvasBrush);
    }

    case WM_PAINT:
        PaintInterface(window);
        return 0;

    case WM_ERASEBKGND:
        return 1;

    case WM_DESTROY:
        KillTimer(window, MonitorTimer);
        KillTimer(window, LiveApplyTimer);
        CloseSession();
        DeleteObject(g_titleFont);
        DeleteObject(g_headingFont);
        DeleteObject(g_bodyFont);
        DeleteObject(g_smallFont);
        DeleteObject(g_canvasBrush);
        DeleteObject(g_cardBrush);
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(window, message, wParam, lParam);
}

int RunCommandLine(int argc, wchar_t** argv)
{
    float fov = std::numeric_limits<float>::quiet_NaN();
    bool reset = false;
    bool once = false;
    for (int index = 1; index < argc; ++index)
    {
        if (_wcsicmp(argv[index], L"--once") == 0)
        {
            once = true;
        }
        else if (_wcsicmp(argv[index], L"--reset-fov") == 0)
        {
            reset = true;
        }
        else if ((_wcsicmp(argv[index], L"--vfov") == 0 || _wcsicmp(argv[index], L"--hfov") == 0) &&
                 index + 1 < argc)
        {
            const bool horizontal = _wcsicmp(argv[index], L"--hfov") == 0;
            const float value = static_cast<float>(_wtof(argv[++index]));
            fov = horizontal ? HorizontalToVerticalFov(value) : value;
        }
        else if (_wcsicmp(argv[index], L"--help") == 0 || _wcsicmp(argv[index], L"-h") == 0)
        {
            MessageBoxW(nullptr,
                L"Resonance 32:9 & Gameplay FOV Patcher\n\n"
                L"--vfov 80      Set vertical gameplay FOV\n"
                L"--hfov 150     Convert a 32:9 horizontal FOV\n"
                L"--reset-fov    Restore the original dynamic FOV\n"
                L"--once         Apply once and exit",
                L"Command-line options", MB_OK | MB_ICONINFORMATION);
            return 0;
        }
        else
        {
            MessageBoxW(nullptr, L"Invalid command-line options.", L"Patcher error", MB_OK | MB_ICONERROR);
            return 2;
        }
    }

    if (reset && std::isfinite(fov))
    {
        MessageBoxW(nullptr, L"FOV options cannot be combined with --reset-fov.",
                    L"Patcher error", MB_OK | MB_ICONERROR);
        return 2;
    }

    const DWORD processId = FindGameProcess();
    if (!processId)
    {
        MessageBoxW(nullptr, L"Resonance.exe is not running.", L"Patcher error", MB_OK | MB_ICONERROR);
        return 3;
    }

    try
    {
        Session session = AttachGame(processId, fov, reset);
        if (!once)
        {
            while (Maintain(session, fov))
            {
                Sleep(500);
            }
        }
        CloseHandle(session.process);
        return 0;
    }
    catch (const PatchError& error)
    {
        MessageBoxW(nullptr, error.message.c_str(), L"Patcher error", MB_OK | MB_ICONERROR);
        return 1;
    }
}
} // namespace

int APIENTRY wWinMain(HINSTANCE instance, HINSTANCE, LPWSTR, int showCommand)
{
    int argc = 0;
    wchar_t** argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    if (argc > 1)
    {
        const int result = RunCommandLine(argc, argv);
        LocalFree(argv);
        return result;
    }
    LocalFree(argv);

    g_instance = instance;
    INITCOMMONCONTROLSEX controls{sizeof(controls), ICC_STANDARD_CLASSES | ICC_BAR_CLASSES};
    InitCommonControlsEx(&controls);

    g_titleFont = CreateUiFont(22, FW_BOLD);
    g_headingFont = CreateUiFont(10, FW_BOLD);
    g_bodyFont = CreateUiFont(10, FW_NORMAL);
    g_smallFont = CreateUiFont(9, FW_NORMAL);
    g_canvasBrush = CreateSolidBrush(Canvas);
    g_cardBrush = CreateSolidBrush(Card);

    WNDCLASSEXW windowClass{};
    windowClass.cbSize = sizeof(windowClass);
    windowClass.style = CS_HREDRAW | CS_VREDRAW;
    windowClass.lpfnWndProc = WindowProcedure;
    windowClass.hInstance = instance;
    windowClass.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    windowClass.hIcon = LoadIconW(nullptr, IDI_APPLICATION);
    windowClass.hIconSm = windowClass.hIcon;
    windowClass.hbrBackground = g_canvasBrush;
    windowClass.lpszClassName = WindowClassName;
    if (!RegisterClassExW(&windowClass))
    {
        return 1;
    }

    RECT bounds{0, 0, 620, 610};
    AdjustWindowRectEx(&bounds, WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX,
                       FALSE, 0);
    HWND window = CreateWindowExW(
        0, WindowClassName, L"Resonance 32:9 & FOV Patcher",
        WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX,
        CW_USEDEFAULT, CW_USEDEFAULT, bounds.right - bounds.left, bounds.bottom - bounds.top,
        nullptr, nullptr, instance, nullptr);
    if (!window)
    {
        return 1;
    }

    const BOOL darkMode = TRUE;
    if (HMODULE dwm = LoadLibraryW(L"dwmapi.dll"))
    {
        using DwmSetWindowAttributeFn = HRESULT(WINAPI*)(HWND, DWORD, LPCVOID, DWORD);
        auto setAttribute = reinterpret_cast<DwmSetWindowAttributeFn>(
            GetProcAddress(dwm, "DwmSetWindowAttribute"));
        if (setAttribute)
        {
            setAttribute(window, 20, &darkMode, sizeof(darkMode));
        }
        FreeLibrary(dwm);
    }

    ShowWindow(window, showCommand);
    UpdateWindow(window);

    MSG message{};
    while (GetMessageW(&message, nullptr, 0, 0) > 0)
    {
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }
    return static_cast<int>(message.wParam);
}
