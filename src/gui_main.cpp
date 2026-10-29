// gui_main.cpp
// File Organizer - Windows desktop GUI built with the plain Win32 API.
// No external libraries: everything used here ships with MinGW-w64.
// All file logic lives in organizer_core.h; this file only draws the window
// and talks to that logic.

#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif

#include <windows.h>
#include <commctrl.h>
#include <shlobj.h>
#include <uxtheme.h>
#include <dwmapi.h>

#include <memory>
#include <string>
#include <vector>

#include "organizer_core.h"

#ifndef DWMWA_USE_IMMERSIVE_DARK_MODE
#define DWMWA_USE_IMMERSIVE_DARK_MODE 20
#endif

using organizer::PlanItem;
using organizer::Status;

namespace {

// ---------- colours (strict monochrome: black, white, neutral greys) ----------
const COLORREF kBg      = RGB(8, 8, 8);        // window background  #080808
const COLORREF kCard    = RGB(10, 10, 10);     // panels             #0A0A0A
const COLORREF kInput   = RGB(0, 0, 0);        // table + path field #000000
const COLORREF kBorder  = RGB(41, 41, 41);     // borders            #292929
const COLORREF kRowLine = RGB(26, 26, 26);     // table grid lines   #1A1A1A
const COLORREF kRowSel  = RGB(38, 38, 38);     // selected row       #262626
const COLORREF kTrack   = RGB(32, 32, 32);     // progress track     #202020
const COLORREF kText    = RGB(255, 255, 255);  // primary text       #FFFFFF
const COLORREF kMuted   = RGB(160, 160, 160);  // secondary text     #A0A0A0
const COLORREF kDisabledText = RGB(102, 102, 102);
const COLORREF kPressed = RGB(204, 204, 204);  // pressed white button

// ---------- control ids and custom messages ----------
enum ControlId {
    IDC_TITLE = 100, IDC_SUBTITLE, IDC_FOLDER_LABEL, IDC_PATH, IDC_BROWSE, IDC_RESCAN,
    IDC_FOUND, IDC_CATS, IDC_PREVIEW_LABEL, IDC_LIST, IDC_EMPTY,
    IDC_STATUS, IDC_PROGRESS, IDC_ORGANIZE
};
const UINT WM_SCAN_DONE     = WM_APP + 1;  // worker -> UI: scan finished
const UINT WM_MOVE_PROGRESS = WM_APP + 2;  // worker -> UI: one file processed
const UINT WM_MOVE_DONE     = WM_APP + 3;  // worker -> UI: all files processed

// ---------- data passed between the worker thread and the UI ----------
struct ScanJob { HWND hwnd; fs::path folder; fs::path exe; };
struct MoveJob { HWND hwnd; std::vector<PlanItem> items; };
struct ScanDoneMsg { organizer::ScanResult result; };
struct MoveProgressMsg { size_t index; Status status; std::string message; };

// ---------- application state (UI thread only) ----------
HWND g_hwnd = nullptr;
HWND g_title, g_subtitle, g_folderLabel, g_path, g_browse, g_rescan, g_found, g_cats,
     g_previewLabel, g_list, g_empty, g_status, g_progress, g_organize;
HFONT g_fTitle, g_fNormal, g_fBold, g_fBig, g_fLabel;
HBRUSH g_brBg, g_brCard, g_brInput, g_brSel, g_brLine;
RECT g_cardA, g_cardB, g_cardC, g_cardD, g_pathBox, g_listRect, g_resultsRect;
int g_dpi = 96;
int g_listW = 0;

std::wstring g_folder;
fs::path g_exePath;
std::vector<PlanItem> g_plan;
std::wstring g_emptyText;
COLORREF g_statusColor = kMuted;
int g_ignored = 0;
bool g_busy = false, g_canOrganize = false, g_showResults = false;
int g_moved = 0, g_skipped = 0, g_failed = 0, g_done = 0;
HANDLE g_worker = nullptr;

int S(int v) { return MulDiv(v, g_dpi, 96); }   // scale a size for the screen DPI

std::wstring Widen(const std::string &s) {
    if (s.empty()) return L"";
    int n = MultiByteToWideChar(CP_ACP, 0, s.c_str(), static_cast<int>(s.size()), nullptr, 0);
    std::wstring w(static_cast<size_t>(n), L'\0');
    MultiByteToWideChar(CP_ACP, 0, s.c_str(), static_cast<int>(s.size()), &w[0], n);
    return w;
}

HFONT MakeFont(int pt, int weight) {
    return CreateFontW(-MulDiv(pt, g_dpi, 72), 0, 0, 0, weight, FALSE, FALSE, FALSE,
                       DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                       CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
}

COLORREF CategoryColor(const std::string &c) {   // greyscale ramp by category position
    const std::vector<std::string> names = organizer::categoryNames();
    size_t idx = 0;
    for (size_t i = 0; i < names.size(); i++) if (names[i] == c) idx = i;
    int level = 255 - static_cast<int>(idx * 160 / (names.size() > 1 ? names.size() - 1 : 1));
    return RGB(level, level, level);
}

// ---------- small UI helpers ----------
void SetStatus(const std::wstring &text, COLORREF color) {
    g_statusColor = color;
    SetWindowTextW(g_status, text.c_str());
    InvalidateRect(g_status, nullptr, TRUE);
}

void UpdateListView() {
    SendMessageW(g_list, LB_SETCOUNT, g_plan.size(), 0);
    InvalidateRect(g_list, nullptr, FALSE);
    SetWindowTextW(g_empty, g_emptyText.c_str());
    ShowWindow(g_empty, g_plan.empty() ? SW_SHOW : SW_HIDE);
}

void UpdateSummary() {
    if (g_plan.empty()) {
        SetWindowTextW(g_found, L"No files detected");
        SetWindowTextW(g_cats, L"Categories will appear here after you choose a folder.");
        return;
    }
    SetWindowTextW(g_found, (std::to_wstring(g_plan.size()) +
                             (g_plan.size() == 1 ? L" file detected" : L" files detected")).c_str());
    std::wstring line;
    for (const std::string &cat : organizer::categoryNames()) {
        int n = 0;
        for (const PlanItem &p : g_plan) if (p.category == cat) n++;
        if (n == 0) continue;
        if (!line.empty()) line += L"   |   ";
        line += Widen(cat) + L" " + std::to_wstring(n);
    }
    SetWindowTextW(g_cats, line.c_str());
}

void RefreshControls() {
    EnableWindow(g_browse, !g_busy);
    EnableWindow(g_rescan, !g_busy && !g_folder.empty());
    EnableWindow(g_organize, !g_busy && g_canOrganize);
}

void JoinWorker() {
    if (g_worker) {
        WaitForSingleObject(g_worker, INFINITE);
        CloseHandle(g_worker);
        g_worker = nullptr;
    }
}

// ---------- layout and painting ----------
void Layout(int w, int h) {
    int m = S(24), gap = S(12), pad = S(18), x = m, cw = w - 2 * m;

    MoveWindow(g_title, x, S(14), cw, S(42), TRUE);
    MoveWindow(g_subtitle, x, S(56), cw, S(22), TRUE);

    int y = S(92);
    g_cardA = {x, y, x + cw, y + S(92)};           y = g_cardA.bottom + gap;
    g_cardB = {x, y, x + cw, y + S(74)};           y = g_cardB.bottom + gap;
    g_cardD = {x, h - m - S(108), x + cw, h - m};
    g_cardC = {x, y, x + cw, g_cardD.top - gap};

    // Card A: folder selection
    MoveWindow(g_folderLabel, x + pad, g_cardA.top + S(14), S(200), S(16), TRUE);
    int rowTop = g_cardA.top + S(40), btnH = S(36), browseW = S(110), rescanW = S(96), g2 = S(10);
    int right = g_cardA.right - pad;
    MoveWindow(g_rescan, right - rescanW, rowTop, rescanW, btnH, TRUE);
    MoveWindow(g_browse, right - rescanW - g2 - browseW, rowTop, browseW, btnH, TRUE);
    g_pathBox = {g_cardA.left + pad, rowTop, right - rescanW - g2 - browseW - g2, rowTop + btnH};
    MoveWindow(g_path, g_pathBox.left + S(12), rowTop + (btnH - S(20)) / 2,
               g_pathBox.right - g_pathBox.left - S(24), S(20), TRUE);

    // Card B: organization summary
    MoveWindow(g_found, x + pad, g_cardB.top + S(12), cw - 2 * pad, S(26), TRUE);
    MoveWindow(g_cats, x + pad, g_cardB.top + S(42), cw - 2 * pad, S(22), TRUE);

    // Card C: preview
    MoveWindow(g_previewLabel, x + pad, g_cardC.top + S(14), S(200), S(16), TRUE);
    g_listRect = {g_cardC.left + S(10), g_cardC.top + S(60), g_cardC.right - S(10), g_cardC.bottom - S(10)};
    if (g_listRect.bottom < g_listRect.top + S(40)) g_listRect.bottom = g_listRect.top + S(40);
    int lw = g_listRect.right - g_listRect.left, lh = g_listRect.bottom - g_listRect.top;
    g_listW = lw;
    MoveWindow(g_list, g_listRect.left, g_listRect.top, lw, lh, TRUE);
    MoveWindow(g_empty, g_listRect.left + S(20), g_listRect.top + lh / 2 - S(30), lw - S(40), S(60), TRUE);

    // Card D: action + progress
    int textW = cw - 3 * pad - S(190);
    MoveWindow(g_status, x + pad, g_cardD.top + S(18), textW, S(20), TRUE);
    MoveWindow(g_progress, x + pad, g_cardD.top + S(46), textW, S(8), TRUE);
    g_resultsRect = {x + pad, g_cardD.top + S(68), x + pad + textW, g_cardD.top + S(94)};
    MoveWindow(g_organize, g_cardD.right - pad - S(190), g_cardD.top + S(26), S(190), S(56), TRUE);
}

struct Cols { int nameX, nameW, destX, destW, statX, statW; };
Cols MakeCols(int w) {
    Cols c;
    int pad = S(14), usable = w - 2 * pad;
    c.nameX = pad;               c.nameW = usable * 38 / 100;
    c.destX = c.nameX + c.nameW; c.destW = usable * 20 / 100;
    c.statX = c.destX + c.destW; c.statW = usable - c.nameW - c.destW;
    return c;
}

void FillCard(HDC dc, const RECT &r, COLORREF fill, COLORREF border, int radius) {
    HPEN pen = CreatePen(PS_SOLID, 1, border);
    HBRUSH br = CreateSolidBrush(fill);
    HGDIOBJ oldPen = SelectObject(dc, pen), oldBr = SelectObject(dc, br);
    RoundRect(dc, r.left, r.top, r.right, r.bottom, radius, radius);
    SelectObject(dc, oldPen); SelectObject(dc, oldBr);
    DeleteObject(pen); DeleteObject(br);
}

void OnPaint(HWND hwnd) {
    PAINTSTRUCT ps;
    HDC dc = BeginPaint(hwnd, &ps);
    SetBkMode(dc, TRANSPARENT);

    FillCard(dc, g_cardA, kCard, kBorder, S(14));
    FillCard(dc, g_cardB, kCard, kBorder, S(14));
    FillCard(dc, g_cardC, kCard, kBorder, S(14));
    FillCard(dc, g_cardD, kCard, kBorder, S(14));
    FillCard(dc, g_pathBox, kInput, kBorder, S(10));

    // column headings above the list
    SelectObject(dc, g_fLabel);
    SetTextColor(dc, kMuted);
    Cols c = MakeCols(g_listW - GetSystemMetrics(SM_CXVSCROLL));
    int hy = g_listRect.top - S(22);
    const wchar_t *names[3] = {L"FILE", L"DESTINATION", L"STATUS"};
    int xs[3] = {c.nameX, c.destX, c.statX};
    for (int i = 0; i < 3; i++) {
        RECT r = {g_listRect.left + xs[i], hy, g_listRect.left + xs[i] + S(200), hy + S(18)};
        DrawTextW(dc, names[i], -1, &r, DT_LEFT | DT_SINGLELINE | DT_NOPREFIX);
    }

    // results: Moved / Skipped / Failed
    if (g_showResults) {
        SelectObject(dc, g_fBold);
        struct Chip { std::wstring text; COLORREF color; };
        Chip chips[3] = {{L"Moved " + std::to_wstring(g_moved), kText},
                         {L"Skipped " + std::to_wstring(g_skipped), kMuted},
                         {L"Failed " + std::to_wstring(g_failed), kText}};
        int px = g_resultsRect.left;
        for (const Chip &chip : chips) {
            SIZE sz;
            GetTextExtentPoint32W(dc, chip.text.c_str(), static_cast<int>(chip.text.size()), &sz);
            SetTextColor(dc, chip.color);
            TextOutW(dc, px, g_resultsRect.top, chip.text.c_str(), static_cast<int>(chip.text.size()));
            px += sz.cx + S(22);
        }
    }
    EndPaint(hwnd, &ps);
}

void DrawButton(const DRAWITEMSTRUCT *d) {
    bool primary = d->CtlID == IDC_ORGANIZE;          // white button, black text
    bool disabled = (d->itemState & ODS_DISABLED) != 0;
    bool pressed = (d->itemState & ODS_SELECTED) != 0;

    COLORREF fill, edge, txt;
    if (primary) {
        fill = disabled ? RGB(34, 34, 34) : pressed ? kPressed : kText;
        edge = disabled ? kBorder : fill;
        txt  = disabled ? kDisabledText : RGB(0, 0, 0);
    } else {                                           // black button, grey border, white text
        fill = pressed ? RGB(38, 38, 38) : RGB(0, 0, 0);
        edge = disabled ? RGB(30, 30, 30) : pressed ? kMuted : RGB(90, 90, 90);
        txt  = disabled ? kDisabledText : kText;
    }

    FillRect(d->hDC, &d->rcItem, g_brCard);            // panel colour behind the rounded corners
    FillCard(d->hDC, d->rcItem, fill, edge, S(12));

    wchar_t label[64] = L"";
    GetWindowTextW(d->hwndItem, label, 64);
    SetBkMode(d->hDC, TRANSPARENT);
    SetTextColor(d->hDC, txt);
    SelectObject(d->hDC, g_fBold);
    RECT r = d->rcItem;
    DrawTextW(d->hDC, label, -1, &r, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);

    // keyboard focus: a clear outline (inverted colour on the white button)
    if ((d->itemState & ODS_FOCUS) && !(d->itemState & ODS_NOFOCUSRECT)) {
        RECT f = d->rcItem;
        InflateRect(&f, -S(5), -S(5));
        HPEN pen = CreatePen(PS_DOT, 1, primary ? RGB(0, 0, 0) : kText);
        HGDIOBJ op = SelectObject(d->hDC, pen), ob = SelectObject(d->hDC, GetStockObject(NULL_BRUSH));
        Rectangle(d->hDC, f.left, f.top, f.right, f.bottom);
        SelectObject(d->hDC, op); SelectObject(d->hDC, ob); DeleteObject(pen);
    }
}

std::wstring StatusText(const PlanItem &it, COLORREF &color) {
    switch (it.status) {
        case Status::Ready:    color = kMuted; return L"Ready to move";
        case Status::Conflict: color = kMuted; return L"Already exists - will be skipped";
        case Status::Moved:    color = kText; return L"Moved";
        case Status::Skipped:  color = kMuted; return L"Skipped: " + Widen(it.message);
        case Status::Failed:   color = kText;   return L"Failed: " + Widen(it.message);
    }
    color = kMuted;
    return L"";
}

void DrawListRow(const DRAWITEMSTRUCT *d) {
    HDC dc = d->hDC;
    RECT r = d->rcItem;
    if (d->itemID == static_cast<UINT>(-1) || d->itemID >= g_plan.size()) {
        FillRect(dc, &r, g_brInput);
        return;
    }
    const PlanItem &it = g_plan[d->itemID];
    FillRect(dc, &r, (d->itemState & ODS_SELECTED) ? g_brSel : g_brInput);
    RECT line = {r.left, r.bottom - 1, r.right, r.bottom};
    FillRect(dc, &line, g_brLine);

    SetBkMode(dc, TRANSPARENT);
    Cols c = MakeCols(g_listW - GetSystemMetrics(SM_CXVSCROLL));
    UINT fmt = DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX;
    SelectObject(dc, g_fNormal);

    // column 1: file name
    RECT t = {r.left + c.nameX, r.top, r.left + c.nameX + c.nameW - S(8), r.bottom};
    SetTextColor(dc, kText);
    std::wstring name = it.source.filename().wstring();
    DrawTextW(dc, name.c_str(), -1, &t, fmt);

    // column 2: coloured dot + category
    int dot = S(8), cy = (r.top + r.bottom) / 2;
    HBRUSH dotBrush = CreateSolidBrush(CategoryColor(it.category));
    HGDIOBJ oldBr = SelectObject(dc, dotBrush), oldPen = SelectObject(dc, GetStockObject(NULL_PEN));
    Ellipse(dc, r.left + c.destX, cy - dot / 2, r.left + c.destX + dot, cy + dot / 2);
    SelectObject(dc, oldBr); SelectObject(dc, oldPen); DeleteObject(dotBrush);
    t = {r.left + c.destX + dot + S(8), r.top, r.left + c.destX + c.destW - S(8), r.bottom};
    SetTextColor(dc, kText);
    std::wstring cat = Widen(it.category);
    DrawTextW(dc, cat.c_str(), -1, &t, fmt);

    // column 3: status
    COLORREF color;
    std::wstring status = StatusText(it, color);
    t = {r.left + c.statX, r.top, r.left + c.statX + c.statW, r.bottom};
    SetTextColor(dc, color);
    if (it.status == Status::Failed) SelectObject(dc, g_fBold);
    DrawTextW(dc, status.c_str(), -1, &t, fmt);
}

// ---------- worker threads (keep the window responsive) ----------
DWORD WINAPI ScanThread(LPVOID param) {
    std::unique_ptr<ScanJob> job(static_cast<ScanJob *>(param));
    auto *msg = new ScanDoneMsg;
    try {
        msg->result = organizer::scanDirectory(job->folder, job->exe);
    } catch (const std::exception &e) {
        msg->result.error = std::string("Unexpected error: ") + e.what();
    }
    if (!PostMessageW(job->hwnd, WM_SCAN_DONE, 0, reinterpret_cast<LPARAM>(msg))) delete msg;
    return 0;
}

DWORD WINAPI MoveThread(LPVOID param) {
    std::unique_ptr<MoveJob> job(static_cast<MoveJob *>(param));
    for (size_t i = 0; i < job->items.size(); i++) {
        PlanItem &item = job->items[i];
        try {
            organizer::moveOne(item);
        } catch (const std::exception &e) {
            item.status = Status::Failed;
            item.message = e.what();
        }
        auto *msg = new MoveProgressMsg{i, item.status, item.message};
        if (!PostMessageW(job->hwnd, WM_MOVE_PROGRESS, 0, reinterpret_cast<LPARAM>(msg))) delete msg;
    }
    PostMessageW(job->hwnd, WM_MOVE_DONE, 0, 0);
    return 0;
}

// ---------- actions ----------
void StartScan() {
    if (g_busy || g_folder.empty()) return;

    // Validate before scanning
    fs::path p(g_folder);
    std::error_code ec;
    g_plan.clear();
    g_canOrganize = false;
    g_showResults = false;
    g_ignored = 0;
    SendMessageW(g_progress, PBM_SETPOS, 0, 0);
    InvalidateRect(g_hwnd, &g_resultsRect, TRUE);

    if (!fs::exists(p, ec) || !fs::is_directory(p, ec)) {
        g_emptyText = L"That folder could not be found.\nChoose another folder with Browse.";
        UpdateListView(); UpdateSummary();
        SetStatus(L"The selected folder does not exist or is not a directory.", kText);
        RefreshControls();
        return;
    }
    if (p.relative_path().empty()) {
        g_emptyText = L"A whole drive cannot be organized.\nPlease choose a normal folder.";
        UpdateListView(); UpdateSummary();
        SetStatus(L"Please choose a folder, not a drive root.", kText);
        RefreshControls();
        return;
    }

    JoinWorker();
    auto *job = new ScanJob{g_hwnd, p, g_exePath};
    g_worker = CreateThread(nullptr, 0, ScanThread, job, 0, nullptr);
    if (!g_worker) {
        delete job;
        SetStatus(L"Could not start the scan.", kText);
        return;
    }
    g_busy = true;
    g_emptyText = L"Scanning folder...";
    UpdateListView(); UpdateSummary();
    SetStatus(L"Scanning folder...", kMuted);
    RefreshControls();
}

void OnScanDone(ScanDoneMsg *msg) {
    std::unique_ptr<ScanDoneMsg> owner(msg);
    JoinWorker();
    g_busy = false;

    if (!msg->result.error.empty() && msg->result.items.empty()) {
        g_plan.clear();
        g_emptyText = L"This folder could not be read.";
        SetStatus(Widen(msg->result.error), kText);
    } else {
        g_plan = std::move(msg->result.items);
        g_ignored = msg->result.ignored;
        int ready = 0, conflicts = 0;
        for (const PlanItem &p : g_plan) {
            if (p.status == Status::Ready) ready++;
            else if (p.status == Status::Conflict) conflicts++;
        }
        g_canOrganize = ready > 0;
        g_emptyText = L"No files to organize in this folder.\nSubfolders, hidden files and program files are never touched.";

        if (g_plan.empty()) {
            SetStatus(L"Nothing to organize here.", kMuted);
        } else {
            std::wstring s = std::to_wstring(ready) + L" file(s) ready to move";
            if (conflicts) s += L", " + std::to_wstring(conflicts) + L" will be skipped (already exist)";
            if (g_ignored) s += L", " + std::to_wstring(g_ignored) + L" hidden/program file(s) ignored";
            if (!msg->result.error.empty()) s += L". Warning: " + Widen(msg->result.error);
            SetStatus(s, kText);
        }
    }
    UpdateListView(); UpdateSummary();
    RefreshControls();
}

void OnBrowse() {
    BROWSEINFOW bi = {};
    bi.hwndOwner = g_hwnd;
    bi.lpszTitle = L"Choose the folder to organize";
    bi.ulFlags = BIF_RETURNONLYFSDIRS | BIF_USENEWUI;
    PIDLIST_ABSOLUTE pidl = SHBrowseForFolderW(&bi);
    if (!pidl) return;                                  // user pressed Cancel
    wchar_t buf[MAX_PATH] = L"";
    if (SHGetPathFromIDListW(pidl, buf)) {
        g_folder = buf;
        SetWindowTextW(g_path, g_folder.c_str());
        InvalidateRect(g_path, nullptr, TRUE);
        CoTaskMemFree(pidl);
        StartScan();
        return;
    }
    CoTaskMemFree(pidl);
    SetStatus(L"That location is not a normal folder. Please choose another one.", kText);
}

void OnOrganize() {
    if (g_busy || !g_canOrganize) return;
    int ready = 0;
    for (const PlanItem &p : g_plan) if (p.status == Status::Ready) ready++;

    // Nothing is moved until the user says yes.
    std::wstring q = L"Move " + std::to_wstring(ready) + L" file(s) into category folders inside:\n\n" +
                     g_folder + L"\n\nFiles are never deleted or overwritten. Continue?";
    if (MessageBoxW(g_hwnd, q.c_str(), L"File Organizer",
                    MB_YESNO | MB_ICONQUESTION | MB_DEFBUTTON2) != IDYES) {
        SetStatus(L"Cancelled. No files were moved.", kMuted);
        return;
    }

    JoinWorker();
    auto *job = new MoveJob{g_hwnd, g_plan};            // the worker gets its own copy
    g_worker = CreateThread(nullptr, 0, MoveThread, job, 0, nullptr);
    if (!g_worker) {
        delete job;
        SetStatus(L"Could not start the operation. No files were moved.", kText);
        return;
    }
    g_busy = true;
    g_showResults = true;
    g_moved = g_skipped = g_failed = g_done = 0;
    SendMessageW(g_progress, PBM_SETRANGE32, 0, static_cast<LPARAM>(g_plan.size()));
    SendMessageW(g_progress, PBM_SETPOS, 0, 0);
    SetStatus(L"Organizing files...", kText);
    InvalidateRect(g_hwnd, &g_resultsRect, TRUE);
    RefreshControls();
}

void OnMoveProgress(MoveProgressMsg *msg) {
    std::unique_ptr<MoveProgressMsg> owner(msg);
    if (msg->index < g_plan.size()) {
        g_plan[msg->index].status = msg->status;
        g_plan[msg->index].message = msg->message;
    }
    if (msg->status == Status::Moved) g_moved++;
    else if (msg->status == Status::Skipped) g_skipped++;
    else if (msg->status == Status::Failed) g_failed++;
    g_done++;

    SendMessageW(g_progress, PBM_SETPOS, static_cast<WPARAM>(g_done), 0);
    SetStatus(L"Organizing files... " + std::to_wstring(g_done) + L" of " + std::to_wstring(g_plan.size()), kText);
    InvalidateRect(g_list, nullptr, FALSE);
    InvalidateRect(g_hwnd, &g_resultsRect, TRUE);
}

void OnMoveDone() {
    JoinWorker();
    g_busy = false;
    g_canOrganize = false;                               // results are final until the next Rescan
    if (g_failed > 0)
        SetStatus(L"Finished with " + std::to_wstring(g_failed) + L" failure(s). See the Status column.", kText);
    else
        SetStatus(L"Finished. Click Rescan to refresh the list.", kText);
    InvalidateRect(g_hwnd, &g_resultsRect, TRUE);
    RefreshControls();
}

// ---------- window procedure ----------
HWND MakeChild(HWND parent, const wchar_t *cls, const wchar_t *text, DWORD style, int id, HFONT font) {
    HWND h = CreateWindowExW(0, cls, text, WS_CHILD | WS_VISIBLE | style, 0, 0, 10, 10, parent,
                             reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), GetModuleHandleW(nullptr), nullptr);
    SendMessageW(h, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
    return h;
}

void OnCreate(HWND hwnd) {
    g_hwnd = hwnd;
    BOOL dark = TRUE;
    if (FAILED(DwmSetWindowAttribute(hwnd, DWMWA_USE_IMMERSIVE_DARK_MODE, &dark, sizeof(dark))))
        DwmSetWindowAttribute(hwnd, 19, &dark, sizeof(dark));   // older Windows 10 builds
    COLORREF cap = kBg, capText = kText, frame = kBorder;       // Windows 11 only; harmless elsewhere
    DwmSetWindowAttribute(hwnd, 35, &cap, sizeof(cap));         // DWMWA_CAPTION_COLOR
    DwmSetWindowAttribute(hwnd, 36, &capText, sizeof(capText)); // DWMWA_TEXT_COLOR
    DwmSetWindowAttribute(hwnd, 34, &frame, sizeof(frame));     // DWMWA_BORDER_COLOR

    g_title = MakeChild(hwnd, L"STATIC", L"File Organizer", SS_LEFT | SS_NOPREFIX, IDC_TITLE, g_fTitle);
    g_subtitle = MakeChild(hwnd, L"STATIC", L"Sort the files of one folder into category folders. Nothing moves until you confirm.",
                           SS_LEFT | SS_NOPREFIX | SS_ENDELLIPSIS, IDC_SUBTITLE, g_fNormal);
    g_folderLabel = MakeChild(hwnd, L"STATIC", L"FOLDER", SS_LEFT, IDC_FOLDER_LABEL, g_fLabel);
    g_path = MakeChild(hwnd, L"EDIT", L"No folder selected", ES_AUTOHSCROLL | ES_READONLY, IDC_PATH, g_fNormal);
    g_browse = MakeChild(hwnd, L"BUTTON", L"Browse...", WS_TABSTOP | BS_OWNERDRAW, IDC_BROWSE, g_fBold);
    g_rescan = MakeChild(hwnd, L"BUTTON", L"Rescan", WS_TABSTOP | BS_OWNERDRAW, IDC_RESCAN, g_fBold);
    g_found = MakeChild(hwnd, L"STATIC", L"No files detected", SS_LEFT | SS_NOPREFIX, IDC_FOUND, g_fBig);
    g_cats = MakeChild(hwnd, L"STATIC", L"", SS_LEFT | SS_NOPREFIX | SS_ENDELLIPSIS, IDC_CATS, g_fNormal);
    g_previewLabel = MakeChild(hwnd, L"STATIC", L"PREVIEW", SS_LEFT, IDC_PREVIEW_LABEL, g_fLabel);
    g_list = MakeChild(hwnd, L"LISTBOX",  L"", WS_TABSTOP | WS_VSCROLL | LBS_OWNERDRAWFIXED | LBS_NODATA | LBS_NOINTEGRALHEIGHT,
                       IDC_LIST, g_fNormal);
    g_empty = MakeChild(hwnd, L"STATIC", L"", SS_CENTER | SS_NOPREFIX, IDC_EMPTY, g_fNormal);
    SetWindowPos(g_empty, HWND_TOP, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE);   // keep it above the list
    g_status = MakeChild(hwnd, L"STATIC", L"", SS_LEFT | SS_NOPREFIX | SS_ENDELLIPSIS, IDC_STATUS, g_fNormal);
    g_progress = MakeChild(hwnd, PROGRESS_CLASSW, L"", PBS_SMOOTH, IDC_PROGRESS, g_fNormal);
    g_organize = MakeChild(hwnd, L"BUTTON", L"Organize Files", WS_TABSTOP | BS_OWNERDRAW, IDC_ORGANIZE, g_fBold);

    SetWindowTheme(g_list, L"DarkMode_Explorer", nullptr);      // dark scrollbar (needs the manifest)
    SetWindowTheme(g_progress, L"", L"");                       // lets us colour the progress bar
    SendMessageW(g_progress, PBM_SETBARCOLOR, 0, kText);
    SendMessageW(g_progress, PBM_SETBKCOLOR, 0, kTrack);

    g_emptyText = L"No folder selected yet.\nClick Browse to choose a folder to organize.";
    UpdateListView();
    SetStatus(L"Choose a folder to begin.", kMuted);
    RefreshControls();
}

LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
        case WM_CREATE:
            OnCreate(hwnd);
            return 0;

        case WM_SIZE:
            if (g_title) {
                Layout(LOWORD(lp), HIWORD(lp));
                InvalidateRect(hwnd, nullptr, TRUE);
            }
            return 0;

        case WM_GETMINMAXINFO: {
            auto *mmi = reinterpret_cast<MINMAXINFO *>(lp);
            mmi->ptMinTrackSize.x = S(760);
            mmi->ptMinTrackSize.y = S(660);
            return 0;
        }

        case WM_PAINT:
            OnPaint(hwnd);
            return 0;

        case WM_MEASUREITEM: {
            auto *mi = reinterpret_cast<MEASUREITEMSTRUCT *>(lp);
            if (mi->CtlType == ODT_LISTBOX) { mi->itemHeight = S(34); return TRUE; }
            break;
        }

        case WM_DRAWITEM: {
            auto *d = reinterpret_cast<DRAWITEMSTRUCT *>(lp);
            if (d->CtlType == ODT_LISTBOX) DrawListRow(d);
            else if (d->CtlType == ODT_BUTTON) DrawButton(d);
            return TRUE;
        }

        case WM_CTLCOLORSTATIC:
        case WM_CTLCOLOREDIT: {
            HDC dc = reinterpret_cast<HDC>(wp);
            int id = GetDlgCtrlID(reinterpret_cast<HWND>(lp));
            COLORREF bg = kCard, fg = kText;
            HBRUSH brush = g_brCard;
            switch (id) {
                case IDC_TITLE:    bg = kBg; fg = kText;  brush = g_brBg; break;
                case IDC_SUBTITLE: bg = kBg; fg = kMuted; brush = g_brBg; break;
                case IDC_PATH:     bg = kInput; fg = g_folder.empty() ? kMuted : kText; brush = g_brInput; break;
                case IDC_EMPTY:    bg = kInput; fg = kMuted; brush = g_brInput; break;
                case IDC_FOLDER_LABEL:
                case IDC_PREVIEW_LABEL:
                case IDC_CATS:     fg = kMuted; break;
                case IDC_STATUS:   fg = g_statusColor; break;
                default: break;
            }
            SetBkColor(dc, bg);
            SetTextColor(dc, fg);
            return reinterpret_cast<LRESULT>(brush);
        }

        case WM_CTLCOLORLISTBOX: {
            // Root cause of the white table: the LISTBOX fills everything outside its
            // rows (empty area below the last row) with this brush. Default = white.
            HDC dc = reinterpret_cast<HDC>(wp);
            SetBkColor(dc, kInput);
            SetTextColor(dc, kText);
            return reinterpret_cast<LRESULT>(g_brInput);
        }

        case WM_COMMAND:
            if (HIWORD(wp) == BN_CLICKED) {
                switch (LOWORD(wp)) {
                    case IDC_BROWSE:   OnBrowse();   break;
                    case IDC_RESCAN:   StartScan();  break;
                    case IDC_ORGANIZE: OnOrganize(); break;
                }
            }
            return 0;

        case WM_SCAN_DONE:
            OnScanDone(reinterpret_cast<ScanDoneMsg *>(lp));
            return 0;
        case WM_MOVE_PROGRESS:
            OnMoveProgress(reinterpret_cast<MoveProgressMsg *>(lp));
            return 0;
        case WM_MOVE_DONE:
            OnMoveDone();
            return 0;

        case WM_CLOSE:
            if (g_busy) {
                MessageBoxW(hwnd, L"Please wait until the current operation finishes.",
                            L"File Organizer", MB_OK | MB_ICONINFORMATION);
                return 0;
            }
            DestroyWindow(hwnd);
            return 0;

        case WM_DESTROY:
            JoinWorker();
            PostQuitMessage(0);
            return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

}  // namespace

int WINAPI WinMain(HINSTANCE hInst, HINSTANCE, LPSTR, int nShow) {
    SetProcessDPIAware();                                   // sharp text on high-DPI screens
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);      // needed by the folder dialog
    INITCOMMONCONTROLSEX icc = {sizeof(icc), ICC_PROGRESS_CLASS | ICC_STANDARD_CLASSES};
    InitCommonControlsEx(&icc);

    HDC screen = GetDC(nullptr);
    g_dpi = GetDeviceCaps(screen, LOGPIXELSX);
    ReleaseDC(nullptr, screen);

    wchar_t exe[MAX_PATH];
    GetModuleFileNameW(nullptr, exe, MAX_PATH);
    g_exePath = exe;                                        // used to never move our own .exe

    g_brBg = CreateSolidBrush(kBg);       g_brCard = CreateSolidBrush(kCard);
    g_brInput = CreateSolidBrush(kInput); g_brSel = CreateSolidBrush(kRowSel);
    g_brLine = CreateSolidBrush(kRowLine);
    g_fTitle = MakeFont(22, FW_BOLD);     g_fNormal = MakeFont(10, FW_NORMAL);
    g_fBold = MakeFont(10, FW_SEMIBOLD);  g_fBig = MakeFont(14, FW_BOLD);
    g_fLabel = MakeFont(8, FW_BOLD);

    WNDCLASSEXW wc = {};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInst;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hIcon = LoadIconW(nullptr, IDI_APPLICATION);
    wc.hbrBackground = g_brBg;
    wc.lpszClassName = L"FileOrganizerWindow";
    RegisterClassExW(&wc);

    RECT work;
    SystemParametersInfoW(SPI_GETWORKAREA, 0, &work, 0);
    int w = std::min(S(940), static_cast<int>(work.right - work.left));
    int h = std::min(S(760), static_cast<int>(work.bottom - work.top));
    HWND hwnd = CreateWindowExW(0, wc.lpszClassName, L"File Organizer",
                                WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN,
                                CW_USEDEFAULT, CW_USEDEFAULT, w, h, nullptr, nullptr, hInst, nullptr);
    if (!hwnd) return 1;
    ShowWindow(hwnd, nShow);
    UpdateWindow(hwnd);

    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
        if (!IsDialogMessageW(hwnd, &msg)) {                // Tab key moves between buttons
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
    }
    return static_cast<int>(msg.wParam);
}
