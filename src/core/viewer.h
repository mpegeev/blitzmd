// The Lister plugin window: a single child window that renders the document
// with Direct2D and handles scrolling, selection, links, search, zoom and
// the context menu.
#pragma once

#include <windows.h>

#include <memory>
#include <string>
#include <vector>

namespace mdv {

// Total Commander Lister plugin constants (listplug.h).
constexpr int lc_copy = 1;
constexpr int lc_newparams = 2;
constexpr int lc_selectall = 3;
constexpr int lc_setpercent = 4;
constexpr int lcp_wraptext = 1;
constexpr int lcp_fittowindow = 2;
constexpr int lcp_forceshow = 16;
constexpr int lcp_darkmode = 128;
constexpr int lcp_darkmodenative = 256;
constexpr int lcs_findfirst = 1;
constexpr int lcs_matchcase = 2;
constexpr int lcs_wholewords = 4;
constexpr int lcs_backwards = 8;
constexpr int itm_percent = 0xFFFE;
constexpr int itm_focus = 0xFFF8;

class Viewer;

// Plugin-level information from Total Commander.
struct HostInfo {
    DWORD interface_hi = 0;
    DWORD interface_lo = 0;
    bool tc_reports_dark_mode() const { return interface_hi > 2 || (interface_hi == 2 && interface_lo >= 12); }
};
HostInfo& Host();

// Creates the viewer window as a child of `parent` and loads `path`.
// Returns nullptr if the window could not be created.
HWND CreateViewer(HWND parent, const wchar_t* path, int show_flags);
Viewer* ViewerFromHwnd(HWND hwnd);

bool ViewerLoadFile(HWND hwnd, const wchar_t* path, int show_flags);
bool ViewerSearch(HWND hwnd, const wchar_t* text, int flags);
bool ViewerCommand(HWND hwnd, int command, int parameter);
bool ViewerPrint(HWND hwnd, const wchar_t* printer, const RECT* margins);

// Whether Windows apps use the dark theme.
bool SystemUsesDarkTheme();

}  // namespace mdv
