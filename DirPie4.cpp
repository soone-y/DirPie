#ifndef NOMINMAX
#define NOMINMAX
#endif

#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif

#include <windows.h>
#include <windowsx.h>
#include <commctrl.h>
#include <shellapi.h>
#include <shobjidl.h>
#include <uxtheme.h>
#include <gdiplus.h>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

using std::wstring;

static const wchar_t* kAppClass = L"DirPie4Main";
static const wchar_t* kPieClass = L"DirPie4Pie";
static const UINT WM_APP_REFRESH = WM_APP + 1;

static HINSTANCE g_hInst = nullptr;
static HWND g_hwndMain = nullptr;
static HWND g_hwndList = nullptr;
static HWND g_hwndPie = nullptr;
static HWND g_hwndEdit = nullptr;
static HWND g_hwndUp = nullptr;
static HWND g_hwndStatus = nullptr;

static ULONG_PTR g_gdiplusToken = 0;

static std::atomic<uint64_t> g_generation{1};
static std::atomic<bool> g_quit{false};

static const uint64_t CAP_BYTES = 5ULL * 1024 * 1024 * 1024;
static const uint64_t REFRESH_INTERVAL_MS = 30ULL * 1000;
static const int WORKER_COUNT = 2;

struct WalkStats {
  uint32_t skipped_access = 0;
  uint32_t skipped_path = 0;
  uint32_t skipped_other = 0;
  uint32_t skipped_reparse = 0;
  bool incomplete = false;
  bool reached_cap = false;
};

struct SizeInfo {
  uint64_t bytes = 0;
  bool exact = false;
  bool incomplete = false;
  WalkStats stats{};
  uint64_t tick = 0;
};

struct Entry {
  wstring name;
  wstring path;
  uint64_t bytes = 0;
  bool has_value = false;
  bool exact = false;
  bool incomplete = false;
  WalkStats stats{};
};

static std::mutex g_mu;
static std::unordered_map<wstring, SizeInfo> g_cache;
static std::vector<Entry> g_entries;

static wstring g_currentDir = L"C:\\";
static int g_hoverIndex = -1;

static uint64_t NowTick() { return GetTickCount64(); }

static bool IsDots(const wchar_t* n) {
  return (n[0] == L'.' && n[1] == 0) || (n[0] == L'.' && n[1] == L'.' && n[2] == 0);
}

static wstring TrimTrailingSlash(wstring p) {
  while (p.size() > 3 && (p.back() == L'\\' || p.back() == L'/')) p.pop_back();
  return p;
}

static wstring EnsureBackslash(wstring p) {
  if (!p.empty() && p.back() != L'\\' && p.back() != L'/') p.push_back(L'\\');
  return p;
}

static bool StartsWithNoCase(const wstring& s, const wchar_t* pref) {
  const size_t n = wcslen(pref);
  return s.size() >= n && _wcsnicmp(s.c_str(), pref, n) == 0;
}

static wstring ToLongPath(const wstring& p_in) {

  wstring p = p_in;
  if (p.empty()) return p;
  if (StartsWithNoCase(p, L"\\\\?\\")) return p;
  if (StartsWithNoCase(p, L"\\\\.\\")) return p;
  if (StartsWithNoCase(p, L"\\\\")) {

    return L"\\\\?\\UNC\\" + p.substr(2);
  }
  return L"\\\\?\\" + p;
}

static wstring JoinPath(const wstring& a, const wstring& b) {
  if (a.empty()) return b;
  if (a.back() == L'\\' || a.back() == L'/') return a + b;
  return a + L"\\" + b;
}

static wstring ParentDir(const wstring& p) {
  wstring s = TrimTrailingSlash(p);
  const size_t pos = s.find_last_of(L"\\/");
  if (pos == wstring::npos) return s;
  if (pos <= 2) {
    if (s.size() >= 3 && s[1] == L':') return s.substr(0, 3);
  }
  return s.substr(0, pos);
}

static wstring FormatBytes(uint64_t b) {
  const wchar_t* units[] = {L"B", L"KB", L"MB", L"GB", L"TB", L"PB"};
  double v = (double)b;
  int u = 0;
  while (v >= 1024.0 && u < 5) { v /= 1024.0; ++u; }

  wchar_t buf[64];
  if (u == 0) swprintf(buf, 64, L"%llu %s", (unsigned long long)b, units[u]);
  else swprintf(buf, 64, L"%.2f %s", v, units[u]);
  return buf;
}

static void SetStatusText(const wstring& s) {
  if (!g_hwndStatus) return;
  SendMessageW(g_hwndStatus, SB_SETTEXTW, 0, (LPARAM)s.c_str());
}

static void AddSkipFromError(DWORD err, WalkStats& st) {
  st.incomplete = true;
  if (err == ERROR_ACCESS_DENIED) st.skipped_access++;
  else if (err == ERROR_FILENAME_EXCED_RANGE || err == ERROR_BUFFER_OVERFLOW ||
           err == ERROR_PATH_NOT_FOUND || err == ERROR_BAD_PATHNAME) st.skipped_path++;
  else st.skipped_other++;
}

static uint64_t WalkDirLogicalSize(const wstring& rootAbs,
                                  uint64_t capBytes,
                                  uint64_t gen,
                                  WalkStats& st) {
  uint64_t total = 0;

  std::vector<wstring> stack;
  stack.reserve(256);
  stack.push_back(TrimTrailingSlash(rootAbs));

  while (!stack.empty()) {
    if (g_quit.load() || g_generation.load() != gen) return total;

    const wstring dir = stack.back();
    stack.pop_back();

    const wstring pattern = EnsureBackslash(dir) + L"*";
    const wstring patternLong = ToLongPath(pattern);

    WIN32_FIND_DATAW fdat{};
    HANDLE h = FindFirstFileExW(patternLong.c_str(),
                               FindExInfoBasic,
                               &fdat,
                               FindExSearchNameMatch,
                               nullptr,
                               FIND_FIRST_EX_LARGE_FETCH);
    if (h == INVALID_HANDLE_VALUE) {
      AddSkipFromError(GetLastError(), st);
      continue;
    }

    for (;;) {
      if (g_quit.load() || g_generation.load() != gen) { FindClose(h); return total; }

      const wchar_t* name = fdat.cFileName;
      if (!IsDots(name)) {
        const bool isDir = (fdat.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
        const bool isReparse = (fdat.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0;

        if (isDir) {
          if (isReparse) {
            st.incomplete = true;
            st.skipped_reparse++;
          } else {
            stack.push_back(JoinPath(dir, name));
          }
        } else {
          const uint64_t sz = ((uint64_t)fdat.nFileSizeHigh << 32) | (uint64_t)fdat.nFileSizeLow;
          total += sz;

          if (capBytes > 0 && total >= capBytes) {
            st.reached_cap = true;
            FindClose(h);
            return total;
          }
        }
      }

      if (!FindNextFileW(h, &fdat)) {
        const DWORD ec = GetLastError();
        if (ec != ERROR_NO_MORE_FILES) AddSkipFromError(ec, st);
        break;
      }
    }
    FindClose(h);
  }

  return total;
}

enum class JobKind { Capped, Exact };

struct Job {
  uint64_t gen = 0;
  wstring path;
  JobKind kind = JobKind::Capped;
};

static std::mutex g_jobMu;
static std::condition_variable g_jobCv;
static std::deque<Job> g_jobs;

static bool IsFresh(const SizeInfo& si) {
  return (NowTick() - si.tick) < REFRESH_INTERVAL_MS;
}

static void EnqueueJob(const Job& j) {
  { std::lock_guard<std::mutex> lk(g_jobMu); g_jobs.push_back(j); }
  g_jobCv.notify_one();
}

static void ClearJobs() {
  std::lock_guard<std::mutex> lk(g_jobMu);
  g_jobs.clear();
}

static void WorkerThreadMain() {
  while (!g_quit.load()) {
    Job job{};
    {
      std::unique_lock<std::mutex> lk(g_jobMu);
      g_jobCv.wait(lk, [] { return g_quit.load() || !g_jobs.empty(); });
      if (g_quit.load()) break;
      job = g_jobs.front();
      g_jobs.pop_front();
    }

    if (g_generation.load() != job.gen) continue;

    WalkStats st{};
    const uint64_t cap = (job.kind == JobKind::Capped) ? CAP_BYTES : 0;
    const uint64_t bytes = WalkDirLogicalSize(job.path, cap, job.gen, st);
    if (g_generation.load() != job.gen) continue;

    SizeInfo si{};
    si.bytes = bytes;
    si.exact = (job.kind == JobKind::Exact) && !st.reached_cap;
    si.incomplete = st.incomplete;
    si.stats = st;
    si.tick = NowTick();

    {
      std::lock_guard<std::mutex> lk(g_mu);
      auto it = g_cache.find(job.path);
      if (it != g_cache.end()) {
        const SizeInfo& old = it->second;

        if (old.exact && !old.incomplete) {
          if (si.incomplete && !si.exact) {

          } else {
            g_cache[job.path] = si;
          }
        } else {
          g_cache[job.path] = si;
        }
      } else {
        g_cache[job.path] = si;
      }
    }

    PostMessageW(g_hwndMain, WM_APP_REFRESH, (WPARAM)job.gen, 0);

    if (job.kind == JobKind::Capped) {
      if (st.reached_cap || st.incomplete) EnqueueJob(Job{job.gen, job.path, JobKind::Exact});
    }
  }
}

static void EnsureListColumns(HWND lv) {
  if (ListView_GetColumnWidth(lv, 0) > 0) return;
  while (ListView_DeleteColumn(lv, 0)) {}

  LVCOLUMNW col{};
  col.mask = LVCF_TEXT | LVCF_WIDTH | LVCF_SUBITEM;

  col.pszText = (LPWSTR)L"Name"; col.cx = 260; col.iSubItem = 0; ListView_InsertColumn(lv, 0, &col);
  col.pszText = (LPWSTR)L"Size"; col.cx = 140; col.iSubItem = 1; ListView_InsertColumn(lv, 1, &col);
  col.pszText = (LPWSTR)L"%";    col.cx =  70; col.iSubItem = 2; ListView_InsertColumn(lv, 2, &col);
}

static void RefreshUIFromCache(uint64_t gen) {
  if (g_generation.load() != gen) return;

  uint64_t sum = 0;
  WalkStats totals{};

  {
    std::lock_guard<std::mutex> lk(g_mu);
    for (auto& e : g_entries) {
      auto it = g_cache.find(e.path);
      if (it != g_cache.end()) {
        const SizeInfo& si = it->second;
        e.bytes = si.bytes;
        e.has_value = true;
        e.exact = si.exact;
        e.incomplete = si.incomplete;
        e.stats = si.stats;
      }

      if (e.has_value) sum += e.bytes;

      totals.skipped_access += e.stats.skipped_access;
      totals.skipped_path   += e.stats.skipped_path;
      totals.skipped_other  += e.stats.skipped_other;
      totals.skipped_reparse+= e.stats.skipped_reparse;
      totals.incomplete = totals.incomplete || e.stats.incomplete;
    }
  }

  std::stable_sort(g_entries.begin(), g_entries.end(),
                   [](const Entry& a, const Entry& b) {
                     const uint64_t ax = a.has_value ? a.bytes : 0;
                     const uint64_t bx = b.has_value ? b.bytes : 0;
                     return ax > bx;
                   });

  SendMessageW(g_hwndList, WM_SETREDRAW, FALSE, 0);
  ListView_DeleteAllItems(g_hwndList);

  LVITEMW it{};
  it.mask = LVIF_TEXT | LVIF_PARAM;

  for (int i = 0; i < (int)g_entries.size(); ++i) {
    it.iItem = i;
    it.iSubItem = 0;
    it.pszText = (LPWSTR)g_entries[i].name.c_str();
    it.lParam = (LPARAM)i;
    ListView_InsertItem(g_hwndList, &it);
  }

  for (int i = 0; i < (int)g_entries.size(); ++i) {
    const Entry& e = g_entries[i];
    wstring sSize, sPct;

    if (e.has_value) {
      sSize = (e.incomplete ? L"~ " : L"") + FormatBytes(e.bytes);
      if (sum > 0) {
        const double pct = (double)e.bytes * 100.0 / (double)sum;
        wchar_t buf[32];
        swprintf(buf, 32, L"%.1f", pct);
        sPct = buf;
      }
    } else {
      sSize = L"...";
    }

    ListView_SetItemText(g_hwndList, i, 1, (LPWSTR)sSize.c_str());
    ListView_SetItemText(g_hwndList, i, 2, (LPWSTR)sPct.c_str());
  }

  SendMessageW(g_hwndList, WM_SETREDRAW, TRUE, 0);

  wchar_t sbuf[256];
  swprintf(sbuf, 256,
           L"%s  |  skipped: access=%u path=%u other=%u reparse=%u%s",
           g_currentDir.c_str(),
           totals.skipped_access, totals.skipped_path, totals.skipped_other, totals.skipped_reparse,
           totals.incomplete ? L"  (incomplete)" : L"");
  SetStatusText(sbuf);

  InvalidateRect(g_hwndPie, nullptr, FALSE);
}

struct SliceGeom { float startDeg; float sweepDeg; };

static std::vector<SliceGeom> ComputeSlices(float baseStartDeg,
                                            const std::vector<uint64_t>& bytes,
                                            uint64_t sum) {
  std::vector<SliceGeom> out(bytes.size());
  float angle = baseStartDeg;

  for (size_t i = 0; i < bytes.size(); ++i) {
    float sweep = 0.0f;
    if (sum > 0) sweep = (float)(360.0 * ((double)bytes[i] / (double)sum));
    out[i] = { angle, sweep };
    angle += sweep;
  }
  return out;
}

static Gdiplus::Color SliceColor(int i) {
  static const uint32_t palette[] = {
    0xFF4E79A7, 0xFFF28E2B, 0xFFE15759, 0xFF76B7B2, 0xFF59A14F,
    0xFFEDC948, 0xFFB07AA1, 0xFFFF9DA7, 0xFF9C755F, 0xFFBAB0AC,
    0xFF2E5EAA, 0xFF8CD17D, 0xFFFFBE7D, 0xFFB6992D, 0xFF86BCB6
  };
  return Gdiplus::Color(palette[i % (int)(sizeof(palette) / sizeof(palette[0]))]);
}

static int HitTestPie(POINT ptClient) {
  RECT rc{};
  GetClientRect(g_hwndPie, &rc);

  const int w = rc.right - rc.left;
  const int h = rc.bottom - rc.top;
  const int cx = w / 2;
  const int cy = h / 2;

  const int r = (w < h ? w : h) / 2 - 8;
  if (r <= 10) return -1;

  const int dx = ptClient.x - cx;
  const int dy = ptClient.y - cy;

  const double dist2 = (double)dx * dx + (double)dy * dy;
  if (dist2 > (double)r * r) return -1;

  const int hole = (int)(r * 0.55);
  if (dist2 < (double)hole * hole) return -1;

  std::vector<uint64_t> bs;
  uint64_t sum = 0;
  for (auto& e : g_entries) {
    if (!e.has_value) return -1;
    bs.push_back(e.bytes);
    sum += e.bytes;
  }
  if (sum == 0) return -1;

  double ang = atan2((double)dy, (double)dx) * 180.0 / 3.141592653589793;
  if (ang < 0) ang += 360.0;

  const auto slices = ComputeSlices(0.0f, bs, sum);
  for (int i = 0; i < (int)slices.size(); ++i) {
    const float a0 = slices[i].startDeg;
    const float a1 = a0 + slices[i].sweepDeg;
    if ((float)ang >= a0 && (float)ang < a1) return i;
  }
  return -1;
}

static void PiePaint(HWND hwnd, HDC hdc) {
  RECT rc{};
  GetClientRect(hwnd, &rc);

  const int w = rc.right - rc.left;
  const int h = rc.bottom - rc.top;

  Gdiplus::Bitmap back(w, h, PixelFormat32bppPARGB);
  Gdiplus::Graphics g(&back);
  g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
  g.Clear(Gdiplus::Color(255, 250, 250, 250));

  const int cx = w / 2;
  const int cy = h / 2;
  const int r = (w < h ? w : h) / 2 - 10;

  if (r < 10) {
    Gdiplus::Graphics out(hdc);
    out.DrawImage(&back, 0, 0);
    return;
  }

  const int hole = (int)(r * 0.55);
  Gdiplus::Rect pieRect(cx - r, cy - r, 2 * r, 2 * r);

  std::vector<uint64_t> bs;
  uint64_t sum = 0;
  bool allKnown = true;
  bool anyIncomplete = false;

  for (auto& e : g_entries) {
    if (!e.has_value) { allKnown = false; break; }
    bs.push_back(e.bytes);
    sum += e.bytes;
    anyIncomplete = anyIncomplete || e.incomplete;
  }

  if (!allKnown || sum == 0) {
    Gdiplus::SolidBrush br(Gdiplus::Color(255, 220, 220, 220));
    g.FillPie(&br, pieRect, 180.0f, 180.0f);
  } else {
    const auto slices = ComputeSlices(0.0f, bs, sum);
    for (int i = 0; i < (int)slices.size(); ++i) {
      Gdiplus::SolidBrush br(SliceColor(i));
      g.FillPie(&br, pieRect, slices[i].startDeg, slices[i].sweepDeg);
    }
  }

  Gdiplus::SolidBrush holeBr(Gdiplus::Color(255, 250, 250, 250));
  g.FillEllipse(&holeBr, cx - hole, cy - hole, 2 * hole, 2 * hole);

  Gdiplus::FontFamily ff(L"Segoe UI");
  Gdiplus::Font f(&ff, 12.0f, Gdiplus::FontStyleRegular, Gdiplus::UnitPixel);
  Gdiplus::SolidBrush txt(Gdiplus::Color(255, 20, 20, 20));

  Gdiplus::RectF rcf((Gdiplus::REAL)(cx - hole),
                     (Gdiplus::REAL)(cy - 12),
                     (Gdiplus::REAL)(2 * hole),
                     24.0f);

  Gdiplus::StringFormat sf;
  sf.SetAlignment(Gdiplus::StringAlignmentCenter);
  sf.SetLineAlignment(Gdiplus::StringAlignmentCenter);

  const wstring center =
      (allKnown && sum > 0)
          ? ((anyIncomplete ? L"~ " : L"") + FormatBytes(sum))
          : L"Scanning...";

  g.DrawString(center.c_str(), -1, &f, rcf, &sf, &txt);

  Gdiplus::Graphics out(hdc);
  out.DrawImage(&back, 0, 0);
}

static void SetListHover(int idx) {
  if (!g_hwndList) return;

  if (g_hoverIndex >= 0) {
    LVITEMW it{};
    it.mask = LVIF_STATE;
    it.iItem = g_hoverIndex;
    it.stateMask = LVIS_DROPHILITED;
    it.state = 0;
    ListView_SetItem(g_hwndList, &it);
  }

  g_hoverIndex = idx;

  if (g_hoverIndex >= 0) {
    LVITEMW it{};
    it.mask = LVIF_STATE;
    it.iItem = g_hoverIndex;
    it.stateMask = LVIS_DROPHILITED;
    it.state = LVIS_DROPHILITED;
    ListView_SetItem(g_hwndList, &it);
  }
}

static void EnumerateChildrenAndSchedule(uint64_t gen, const wstring& dirAbs) {
  std::vector<Entry> found;

  const wstring dir = TrimTrailingSlash(dirAbs);
  const wstring pattern = EnsureBackslash(dir) + L"*";
  const wstring patternLong = ToLongPath(pattern);

  WIN32_FIND_DATAW fdat{};
  HANDLE h = FindFirstFileExW(patternLong.c_str(),
                             FindExInfoBasic,
                             &fdat,
                             FindExSearchNameMatch,
                             nullptr,
                             FIND_FIRST_EX_LARGE_FETCH);
  if (h == INVALID_HANDLE_VALUE) {
    wchar_t buf[256];
    swprintf(buf, 256, L"%s  |  enumerate failed: %lu", dirAbs.c_str(), GetLastError());
    SetStatusText(buf);
    return;
  }

  for (;;) {
    if (g_generation.load() != gen) { FindClose(h); return; }

if (!IsDots(fdat.cFileName)) {
  const bool isDir = (fdat.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;

  Entry e{};
  e.name = fdat.cFileName;
  e.path = JoinPath(dir, e.name);

  if (isDir) {
    found.push_back(std::move(e));
  } else {
    const uint64_t sz = ((uint64_t)fdat.nFileSizeHigh << 32) | (uint64_t)fdat.nFileSizeLow;
    e.bytes = sz;
    e.has_value = true;
    e.exact = true;
    e.incomplete = false;
    found.push_back(std::move(e));
  }
}


    if (!FindNextFileW(h, &fdat)) break;
  }
  FindClose(h);

  { std::lock_guard<std::mutex> lk(g_mu); g_entries = std::move(found); }

for (auto& e : g_entries) {
  DWORD attr = GetFileAttributesW(ToLongPath(e.path).c_str());
  if (attr != INVALID_FILE_ATTRIBUTES && !(attr & FILE_ATTRIBUTE_DIRECTORY)) {
    continue; // file: already has exact size
  }

  bool need = true;
  {
    std::lock_guard<std::mutex> lk(g_mu);
    auto it = g_cache.find(e.path);
    if (it != g_cache.end() && IsFresh(it->second)) need = false;
  }
  if (need) EnqueueJob(Job{gen, e.path, JobKind::Capped});
}


  PostMessageW(g_hwndMain, WM_APP_REFRESH, (WPARAM)gen, 0);
}

static void StartAnalyze(const wstring& dirAbs) {
  const uint64_t gen = g_generation.fetch_add(1) + 1;

  ClearJobs();
  SetListHover(-1);

  g_currentDir = TrimTrailingSlash(dirAbs);
  SetWindowTextW(g_hwndEdit, g_currentDir.c_str());

  { std::lock_guard<std::mutex> lk(g_mu); g_entries.clear(); }

  EnsureListColumns(g_hwndList);
  ListView_DeleteAllItems(g_hwndList);
  InvalidateRect(g_hwndPie, nullptr, TRUE);

  EnumerateChildrenAndSchedule(gen, g_currentDir);
}

static void Layout(HWND hwnd) {
  RECT rc{};
  GetClientRect(hwnd, &rc);

  const int W = rc.right - rc.left;
  const int H = rc.bottom - rc.top;

  const int topH = 32;
  const int statusH = 22;

  int contentH = H - topH - statusH;
  if (contentH < 50) contentH = 50;

  MoveWindow(g_hwndUp,   8, 6, 60, 22, TRUE);
  MoveWindow(g_hwndEdit, 76, 6, W - 88, 22, TRUE);

  int split = (int)(W * 0.45);
  if (split < 220) split = 220;
  if (split > W - 220) split = W - 220;

  MoveWindow(g_hwndList,   0,      topH, split,     contentH, TRUE);
  MoveWindow(g_hwndPie,    split,  topH, W - split, contentH, TRUE);
  MoveWindow(g_hwndStatus, 0, topH + contentH, W,   statusH,  TRUE);
}

static LRESULT CALLBACK PieWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
  switch (msg) {
    case WM_ERASEBKGND:
      return 1;

    case WM_MOUSEMOVE: {
      TRACKMOUSEEVENT tme{ sizeof(TRACKMOUSEEVENT), TME_LEAVE, hwnd, 0 };
      TrackMouseEvent(&tme);

      POINT pt{ GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
      SetListHover(HitTestPie(pt));
      return 0;
    }

    case WM_MOUSELEAVE:
      SetListHover(-1);
      return 0;

    case WM_LBUTTONDOWN: {
      POINT pt{ GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
      const int idx = HitTestPie(pt);
      if (idx >= 0 && idx < (int)g_entries.size()) StartAnalyze(g_entries[idx].path);
      return 0;
    }

    case WM_PAINT: {
      PAINTSTRUCT ps{};
      HDC hdc = BeginPaint(hwnd, &ps);
      PiePaint(hwnd, hdc);
      EndPaint(hwnd, &ps);
      return 0;
    }
  }

  return DefWindowProcW(hwnd, msg, wParam, lParam);
}

static LRESULT CALLBACK MainWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
  switch (msg) {
    case WM_CREATE: {
      InitCommonControls();

      g_hwndUp = CreateWindowExW(0, L"BUTTON", L"Up",
                                WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                                0, 0, 0, 0,
                                hwnd, (HMENU)1001, g_hInst, nullptr);

      g_hwndEdit = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"",
                                  WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL,
                                  0, 0, 0, 0,
                                  hwnd, (HMENU)1002, g_hInst, nullptr);

      g_hwndList = CreateWindowExW(WS_EX_CLIENTEDGE, WC_LISTVIEWW, L"",
                                  WS_CHILD | WS_VISIBLE | LVS_REPORT |
                                  LVS_SINGLESEL | LVS_SHOWSELALWAYS,
                                  0, 0, 0, 0,
                                  hwnd, (HMENU)1003, g_hInst, nullptr);

      ListView_SetExtendedListViewStyle(
          g_hwndList,
          LVS_EX_FULLROWSELECT | LVS_EX_DOUBLEBUFFER | LVS_EX_INFOTIP | LVS_EX_TRACKSELECT);

      EnsureListColumns(g_hwndList);

      g_hwndPie = CreateWindowExW(0, kPieClass, L"",
                                 WS_CHILD | WS_VISIBLE,
                                 0, 0, 0, 0,
                                 hwnd, (HMENU)1004, g_hInst, nullptr);

      g_hwndStatus = CreateWindowExW(0, STATUSCLASSNAMEW, L"",
                                    WS_CHILD | WS_VISIBLE,
                                    0, 0, 0, 0,
                                    hwnd, (HMENU)1005, g_hInst, nullptr);

      SetWindowTextW(g_hwndEdit, g_currentDir.c_str());
      Layout(hwnd);

      StartAnalyze(g_currentDir);
      return 0;
    }

    case WM_SIZE:
      Layout(hwnd);
      return 0;

    case WM_COMMAND: {
      const int id = LOWORD(wParam);
      if (id == 1001) { StartAnalyze(ParentDir(g_currentDir)); return 0; }
      return 0;
    }

    case WM_NOTIFY: {
      LPNMHDR hdr = (LPNMHDR)lParam;
      if (hdr->hwndFrom == g_hwndList && hdr->code == NM_DBLCLK) {
        const int sel = ListView_GetNextItem(g_hwndList, -1, LVNI_SELECTED);
        if (sel >= 0 && sel < (int)g_entries.size()) StartAnalyze(g_entries[sel].path);
        return 0;
      }
      return 0;
    }

    case WM_APP_REFRESH:
      RefreshUIFromCache((uint64_t)wParam);
      return 0;

    case WM_DESTROY:
      PostQuitMessage(0);
      return 0;
  }

  return DefWindowProcW(hwnd, msg, wParam, lParam);
}

int APIENTRY wWinMain(HINSTANCE hInst, HINSTANCE, LPWSTR, int) {
  g_hInst = hInst;

  CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);

  Gdiplus::GdiplusStartupInput gdiSI;
  Gdiplus::GdiplusStartup(&g_gdiplusToken, &gdiSI, nullptr);

  WNDCLASSEXW wc{};
  wc.cbSize = sizeof(wc);
  wc.hInstance = hInst;
  wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
  wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
  wc.lpfnWndProc = PieWndProc;
  wc.lpszClassName = kPieClass;
  RegisterClassExW(&wc);

  wc = {};
  wc.cbSize = sizeof(wc);
  wc.hInstance = hInst;
  wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
  wc.hIcon = LoadIcon(nullptr, IDI_APPLICATION);
  wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
  wc.lpfnWndProc = MainWndProc;
  wc.lpszClassName = kAppClass;
  RegisterClassExW(&wc);

  g_hwndMain = CreateWindowExW(0, kAppClass, L"DirPie4",
                              WS_OVERLAPPEDWINDOW | WS_VISIBLE,
                              CW_USEDEFAULT, CW_USEDEFAULT, 980, 620,
                              nullptr, nullptr, hInst, nullptr);

  std::vector<std::thread> workers;
  workers.reserve(WORKER_COUNT);
  for (int i = 0; i < WORKER_COUNT; ++i) workers.emplace_back(WorkerThreadMain);

  MSG msg{};
  while (GetMessageW(&msg, nullptr, 0, 0)) {
    TranslateMessage(&msg);
    DispatchMessageW(&msg);
  }

  g_quit.store(true);
  g_jobCv.notify_all();
  for (auto& t : workers) if (t.joinable()) t.join();

  Gdiplus::GdiplusShutdown(g_gdiplusToken);
  CoUninitialize();
  return 0;
}
