#include "core/icons.h"

#include <cmath>
#include <cwctype>

namespace mdv {
namespace {

// Octicons 16px path data (https://github.com/primer/octicons, MIT license).
constexpr std::wstring_view kInfo =
    L"M0 8a8 8 0 1 1 16 0A8 8 0 0 1 0 8Zm8-6.5a6.5 6.5 0 1 0 0 13 6.5 6.5 0 0 0 0-13ZM6.5 7.75A.75.75 0 0 1 "
    L"7.25 7h1a.75.75 0 0 1 .75.75v2.75h.25a.75.75 0 0 1 0 1.5h-2a.75.75 0 0 1 0-1.5h.25v-2h-.25a.75.75 0 0 "
    L"1-.75-.75ZM8 6a1 1 0 1 1 0-2 1 1 0 0 1 0 2Z";
constexpr std::wstring_view kLightBulb =
    L"M8 1.5c-2.363 0-4 1.69-4 3.75 0 .984.424 1.625.984 2.304l.214.253c.223.264.47.556.673.848.284.411.537"
    L".896.621 1.49a.75.75 0 0 1-1.484.211c-.04-.282-.163-.547-.37-.847a8.456 8.456 0 0 0-.542-.68c-.084-.1"
    L"-.173-.205-.268-.32C3.201 7.75 2.5 6.766 2.5 5.25 2.5 2.31 4.863 0 8 0s5.5 2.31 5.5 5.25c0 1.516-.701 "
    L"2.5-1.328 3.259-.095.115-.184.22-.268.319-.207.245-.383.453-.541.681-.208.3-.33.565-.37.847a.751.751 0 "
    L"0 1-1.485-.212c.084-.593.337-1.078.621-1.489.203-.292.45-.584.673-.848.075-.088.147-.173.213-.253.561-"
    L".679.985-1.32.985-2.304 0-2.06-1.637-3.75-4-3.75ZM5.75 12h4.5a.75.75 0 0 1 0 1.5h-4.5a.75.75 0 0 1 0-"
    L"1.5ZM6 15.25a.75.75 0 0 1 .75-.75h2.5a.75.75 0 0 1 0 1.5h-2.5a.75.75 0 0 1-.75-.75Z";
constexpr std::wstring_view kReport =
    L"M0 1.75C0 .784.784 0 1.75 0h12.5C15.216 0 16 .784 16 1.75v9.5A1.75 1.75 0 0 1 14.25 13H8.06l-2.573 "
    L"2.573A1.458 1.458 0 0 1 3 14.543V13H1.75A1.75 1.75 0 0 1 0 11.25Zm1.75-.25a.25.25 0 0 0-.25.25v9.5c0 "
    L".138.112.25.25.25h2a.75.75 0 0 1 .75.75v2.19l2.72-2.72a.749.749 0 0 1 .53-.22h6.5a.25.25 0 0 0 .25-.25"
    L"v-9.5a.25.25 0 0 0-.25-.25Zm7 2.25v2.5a.75.75 0 0 1-1.5 0v-2.5a.75.75 0 0 1 1.5 0ZM9 9a1 1 0 1 1-2 0 1 "
    L"1 0 0 1 2 0Z";
constexpr std::wstring_view kAlert =
    L"M6.457 1.047c.659-1.234 2.427-1.234 3.086 0l6.082 11.378A1.75 1.75 0 0 1 14.082 15H1.918a1.75 1.75 0 0 "
    L"1-1.543-2.575Zm1.763.707a.25.25 0 0 0-.44 0L1.698 13.132a.25.25 0 0 0 .22.368h12.164a.25.25 0 0 0 .22-"
    L".368Zm.53 3.996v2.5a.75.75 0 0 1-1.5 0v-2.5a.75.75 0 0 1 1.5 0ZM9 11a1 1 0 1 1-2 0 1 1 0 0 1 2 0Z";
constexpr std::wstring_view kStop =
    L"M4.47.22A.749.749 0 0 1 5 0h6c.199 0 .389.079.53.22l4.25 4.25c.141.14.22.331.22.53v6a.749.749 0 0 1-"
    L".22.53l-4.25 4.25A.749.749 0 0 1 11 16H5a.749.749 0 0 1-.53-.22L.22 11.53A.749.749 0 0 1 0 11V5c0-.199"
    L".079-.389.22-.53Zm.84 1.28L1.5 5.31v5.38l3.81 3.81h5.38l3.81-3.81V5.31L10.69 1.5ZM8 4a.75.75 0 0 1 "
    L".75.75v3.5a.75.75 0 0 1-1.5 0v-3.5A.75.75 0 0 1 8 4Zm0 8a1 1 0 1 1 0-2 1 1 0 0 1 0 2Z";
constexpr std::wstring_view kCopy =
    L"M0 6.75C0 5.784.784 5 1.75 5h1.5a.75.75 0 0 1 0 1.5h-1.5a.25.25 0 0 0-.25.25v7.5c0 .138.112.25.25.25h"
    L"7.5a.25.25 0 0 0 .25-.25v-1.5a.75.75 0 0 1 1.5 0v1.5A1.75 1.75 0 0 1 9.25 16h-7.5A1.75 1.75 0 0 1 0 "
    L"14.25ZM5 1.75C5 .784 5.784 0 6.75 0h7.5C15.216 0 16 .784 16 1.75v7.5A1.75 1.75 0 0 1 14.25 11h-7.5A1.75 "
    L"1.75 0 0 1 5 9.25Zm1.75-.25a.25.25 0 0 0-.25.25v7.5c0 .138.112.25.25.25h7.5a.25.25 0 0 0 .25-.25v-7.5a"
    L".25.25 0 0 0-.25-.25Z";
constexpr std::wstring_view kCheck =
    L"M13.78 4.22a.75.75 0 0 1 0 1.06l-7.25 7.25a.75.75 0 0 1-1.06 0L2.22 9.28a.751.751 0 0 1 .018-1.042.751"
    L".751 0 0 1 1.042-.018L6 10.94l6.72-6.72a.75.75 0 0 1 1.06 0Z";
constexpr std::wstring_view kImage =
    L"M16 13.25A1.75 1.75 0 0 1 14.25 15H1.75A1.75 1.75 0 0 1 0 13.25V2.75C0 1.784.784 1 1.75 1h12.5c.966 0 "
    L"1.75.784 1.75 1.75ZM1.75 2.5a.25.25 0 0 0-.25.25v10.5c0 .138.112.25.25.25h.94l.03-.03 6.077-6.078a1.75 "
    L"1.75 0 0 1 2.412-.06L14.5 10.31V2.75a.25.25 0 0 0-.25-.25Zm12.5 11a.25.25 0 0 0 .25-.25v-.917l-4.298-"
    L"3.889a.25.25 0 0 0-.344.009L4.81 13.5ZM7 6a2 2 0 1 1-3.999.001A2 2 0 0 1 7 6ZM5.5 6a.5.5 0 1 0-1 0 .5"
    L".5 0 0 0 1 0Z";
constexpr std::wstring_view kLink =
    L"m7.775 3.275 1.25-1.25a3.5 3.5 0 1 1 4.95 4.95l-2.5 2.5a3.5 3.5 0 0 1-4.95 0 .751.751 0 0 1 .018-1.042"
    L".751.751 0 0 1 1.042-.018 1.998 1.998 0 0 0 2.83 0l2.5-2.5a2.002 2.002 0 0 0-2.83-2.83l-1.25 1.25a.751"
    L".751 0 0 1-1.042-.018.751.751 0 0 1-.018-1.042Zm-4.69 9.64a1.998 1.998 0 0 0 2.83 0l1.25-1.25a.751.751 "
    L"0 0 1 1.042.018.751.751 0 0 1 .018 1.042l-1.25 1.25a3.5 3.5 0 1 1-4.95-4.95l2.5-2.5a3.5 3.5 0 0 1 4.95 "
    L"0 .751.751 0 0 1-.018 1.042.751.751 0 0 1-1.042.018 1.998 1.998 0 0 0-2.83 0l-2.5 2.5a1.998 1.998 0 0 "
    L"0 0 2.83Z";
constexpr std::wstring_view kZoomIn =
    L"M3.75 7.5a.75.75 0 0 1 .75-.75h2.25V4.5a.75.75 0 0 1 1.5 0v2.25h2.25a.75.75 0 0 1 0 1.5H8.25v2.25a.75.75 0 "
    L"0 1-1.5 0V8.25H4.5a.75.75 0 0 1-.75-.75Z"
    L"M7.5 0a7.5 7.5 0 0 1 5.807 12.247l2.473 2.473a.749.749 0 1 1-1.06 1.06l-2.473-2.473A7.5 7.5 0 1 1 7.5 0Zm-6 "
    L"7.5a6 6 0 1 0 12 0 6 6 0 0 0-12 0Z";
constexpr std::wstring_view kZoomOut =
    L"M4.5 6.75h6a.75.75 0 0 1 0 1.5h-6a.75.75 0 0 1 0-1.5Z"
    L"M0 7.5a7.5 7.5 0 1 1 13.307 4.747l2.473 2.473a.749.749 0 1 1-1.06 1.06l-2.473-2.473A7.5 7.5 0 0 1 0 7.5Zm7.5"
    L"-6a6 6 0 1 0 0 12 6 6 0 0 0 0-12Z";
constexpr std::wstring_view kScreenFull =
    L"M1.75 10a.75.75 0 0 1 .75.75v2.5c0 .138.112.25.25.25h2.5a.75.75 0 0 1 0 1.5h-2.5A1.75 1.75 0 0 1 1 13.25v-2.5"
    L"a.75.75 0 0 1 .75-.75Zm12.5 0a.75.75 0 0 1 .75.75v2.5A1.75 1.75 0 0 1 13.25 15h-2.5a.75.75 0 0 1 0-1.5h2.5a"
    L".25.25 0 0 0 .25-.25v-2.5a.75.75 0 0 1 .75-.75ZM2.75 2.5a.25.25 0 0 0-.25.25v2.5a.75.75 0 0 1-1.5 0v-2.5C1 "
    L"1.784 1.784 1 2.75 1h2.5a.75.75 0 0 1 0 1.5ZM10 1.75a.75.75 0 0 1 .75-.75h2.5c.966 0 1.75.784 1.75 1.75v2.5"
    L"a.75.75 0 0 1-1.5 0v-2.5a.25.25 0 0 0-.25-.25h-2.5a.75.75 0 0 1-.75-.75Z";
constexpr std::wstring_view kSync =
    L"M1.705 8.005a.75.75 0 0 1 .834.656 5.5 5.5 0 0 0 9.592 2.97l-1.204-1.204a.25.25 0 0 1 .177-.427h3.646a.25.25"
    L" 0 0 1 .25.25v3.646a.25.25 0 0 1-.427.177l-1.38-1.38A7.002 7.002 0 0 1 1.05 8.84a.75.75 0 0 1 .656-.834ZM8 "
    L"2.5a5.487 5.487 0 0 0-4.131 1.869l1.204 1.204A.25.25 0 0 1 4.896 6H1.25A.25.25 0 0 1 1 5.75V2.104a.25.25 0 "
    L"0 1 .427-.177l1.38 1.38A7.002 7.002 0 0 1 14.95 7.16a.75.75 0 0 1-1.49.178A5.5 5.5 0 0 0 8 2.5Z";
constexpr std::wstring_view kX =
    L"M3.72 3.72a.75.75 0 0 1 1.06 0L8 6.94l3.22-3.22a.749.749 0 0 1 1.275.326.749.749 0 0 1-.215.734L9.06 8l3.22 "
    L"3.22a.749.749 0 0 1-.326 1.275.749.749 0 0 1-.734-.215L8 9.06l-3.22 3.22a.751.751 0 0 1-1.042-.018.751.751 "
    L"0 0 1-.018-1.042L6.94 8 3.72 4.78a.75.75 0 0 1 0-1.06Z";

class PathParser {
public:
    explicit PathParser(std::wstring_view d) : d_(d) {}

    bool Build(ID2D1GeometrySink* sink) {
        wchar_t cmd = 0;
        float cx = 0, cy = 0;       // current point
        float sx = 0, sy = 0;       // subpath start
        float lcx = 0, lcy = 0;     // last control point (for S/T)
        wchar_t last = 0;
        bool open = false;
        auto begin_if_needed = [&] {
            if (!open) {
                sink->BeginFigure(D2D1::Point2F(cx, cy), D2D1_FIGURE_BEGIN_FILLED);
                open = true;
                sx = cx;
                sy = cy;
            }
        };
        while (SkipSeparators(), i_ < d_.size()) {
            const wchar_t c = d_[i_];
            if (std::iswalpha(c)) {
                cmd = c;
                ++i_;
            } else if (!cmd) {
                return false;
            }
            const bool rel = std::iswlower(cmd) != 0;
            const wchar_t up = static_cast<wchar_t>(std::towupper(cmd));
            float a[7];
            switch (up) {
            case L'M':
                if (!Numbers(a, 2)) return false;
                if (open) sink->EndFigure(D2D1_FIGURE_END_OPEN);
                open = false;
                cx = rel ? cx + a[0] : a[0];
                cy = rel ? cy + a[1] : a[1];
                begin_if_needed();
                cmd = rel ? L'l' : L'L';  // subsequent pairs are line-to
                break;
            case L'L':
                if (!Numbers(a, 2)) return false;
                begin_if_needed();
                cx = rel ? cx + a[0] : a[0];
                cy = rel ? cy + a[1] : a[1];
                sink->AddLine(D2D1::Point2F(cx, cy));
                break;
            case L'H':
                if (!Numbers(a, 1)) return false;
                begin_if_needed();
                cx = rel ? cx + a[0] : a[0];
                sink->AddLine(D2D1::Point2F(cx, cy));
                break;
            case L'V':
                if (!Numbers(a, 1)) return false;
                begin_if_needed();
                cy = rel ? cy + a[0] : a[0];
                sink->AddLine(D2D1::Point2F(cx, cy));
                break;
            case L'C': {
                if (!Numbers(a, 6)) return false;
                begin_if_needed();
                const float ox = rel ? cx : 0, oy = rel ? cy : 0;
                D2D1_BEZIER_SEGMENT s{{a[0] + ox, a[1] + oy}, {a[2] + ox, a[3] + oy}, {a[4] + ox, a[5] + oy}};
                sink->AddBezier(s);
                lcx = s.point2.x;
                lcy = s.point2.y;
                cx = s.point3.x;
                cy = s.point3.y;
                break;
            }
            case L'S': {
                if (!Numbers(a, 4)) return false;
                begin_if_needed();
                const float ox = rel ? cx : 0, oy = rel ? cy : 0;
                const wchar_t pu = static_cast<wchar_t>(std::towupper(last));
                const float x1 = (pu == L'C' || pu == L'S') ? 2 * cx - lcx : cx;
                const float y1 = (pu == L'C' || pu == L'S') ? 2 * cy - lcy : cy;
                D2D1_BEZIER_SEGMENT s{{x1, y1}, {a[0] + ox, a[1] + oy}, {a[2] + ox, a[3] + oy}};
                sink->AddBezier(s);
                lcx = s.point2.x;
                lcy = s.point2.y;
                cx = s.point3.x;
                cy = s.point3.y;
                break;
            }
            case L'Q': {
                if (!Numbers(a, 4)) return false;
                begin_if_needed();
                const float ox = rel ? cx : 0, oy = rel ? cy : 0;
                D2D1_QUADRATIC_BEZIER_SEGMENT s{{a[0] + ox, a[1] + oy}, {a[2] + ox, a[3] + oy}};
                sink->AddQuadraticBezier(s);
                lcx = s.point1.x;
                lcy = s.point1.y;
                cx = s.point2.x;
                cy = s.point2.y;
                break;
            }
            case L'T': {
                if (!Numbers(a, 2)) return false;
                begin_if_needed();
                const float ox = rel ? cx : 0, oy = rel ? cy : 0;
                const wchar_t pu = static_cast<wchar_t>(std::towupper(last));
                const float x1 = (pu == L'Q' || pu == L'T') ? 2 * cx - lcx : cx;
                const float y1 = (pu == L'Q' || pu == L'T') ? 2 * cy - lcy : cy;
                D2D1_QUADRATIC_BEZIER_SEGMENT s{{x1, y1}, {a[0] + ox, a[1] + oy}};
                sink->AddQuadraticBezier(s);
                lcx = x1;
                lcy = y1;
                cx = s.point2.x;
                cy = s.point2.y;
                break;
            }
            case L'A': {
                if (!Number(a[0]) || !Number(a[1]) || !Number(a[2]) || !Flag(a[3]) || !Flag(a[4]) ||
                    !Number(a[5]) || !Number(a[6])) {
                    return false;
                }
                begin_if_needed();
                const float ex = rel ? cx + a[5] : a[5];
                const float ey = rel ? cy + a[6] : a[6];
                D2D1_ARC_SEGMENT s{};
                s.point = D2D1::Point2F(ex, ey);
                s.size = D2D1::SizeF(std::fabs(a[0]), std::fabs(a[1]));
                s.rotationAngle = a[2];
                s.arcSize = a[3] != 0 ? D2D1_ARC_SIZE_LARGE : D2D1_ARC_SIZE_SMALL;
                s.sweepDirection = a[4] != 0 ? D2D1_SWEEP_DIRECTION_CLOCKWISE
                                             : D2D1_SWEEP_DIRECTION_COUNTER_CLOCKWISE;
                sink->AddArc(s);
                cx = ex;
                cy = ey;
                break;
            }
            case L'Z':
                if (open) sink->EndFigure(D2D1_FIGURE_END_CLOSED);
                open = false;
                cx = sx;
                cy = sy;
                cmd = 0;  // takes no numbers: one after it is an error (it looped forever)
                break;
            default:
                return false;
            }
            last = cmd;
        }
        if (open) sink->EndFigure(D2D1_FIGURE_END_OPEN);
        return true;
    }

private:
    void SkipSeparators() {
        while (i_ < d_.size() && (std::iswspace(d_[i_]) || d_[i_] == L',')) ++i_;
    }
    bool Number(float& out) {
        SkipSeparators();
        const size_t start = i_;
        if (i_ < d_.size() && (d_[i_] == L'-' || d_[i_] == L'+')) ++i_;
        bool digits = false, dot = false;
        while (i_ < d_.size()) {
            const wchar_t c = d_[i_];
            if (c >= L'0' && c <= L'9') {
                digits = true;
                ++i_;
            } else if (c == L'.' && !dot) {
                dot = true;
                ++i_;
            } else {
                break;
            }
        }
        if (digits && i_ < d_.size() && (d_[i_] == L'e' || d_[i_] == L'E')) {
            size_t j = i_ + 1;
            if (j < d_.size() && (d_[j] == L'-' || d_[j] == L'+')) ++j;
            if (j < d_.size() && d_[j] >= L'0' && d_[j] <= L'9') {
                i_ = j;
                while (i_ < d_.size() && d_[i_] >= L'0' && d_[i_] <= L'9') ++i_;
            }
        }
        if (!digits) {
            i_ = start;
            return false;
        }
        out = std::wcstof(std::wstring(d_.substr(start, i_ - start)).c_str(), nullptr);
        return true;
    }
    bool Flag(float& out) {
        SkipSeparators();
        if (i_ < d_.size() && (d_[i_] == L'0' || d_[i_] == L'1')) {
            out = d_[i_] == L'1' ? 1.f : 0.f;
            ++i_;
            return true;
        }
        return false;
    }
    bool Numbers(float* out, int n) {
        for (int k = 0; k < n; ++k) {
            if (!Number(out[k])) return false;
        }
        return true;
    }

    std::wstring_view d_;
    size_t i_ = 0;
};

std::wstring_view IconPath(Icon icon) {
    switch (icon) {
    case Icon::Info: return kInfo;
    case Icon::LightBulb: return kLightBulb;
    case Icon::Report: return kReport;
    case Icon::Alert: return kAlert;
    case Icon::Stop: return kStop;
    case Icon::Copy: return kCopy;
    case Icon::Check: return kCheck;
    case Icon::Image: return kImage;
    case Icon::Link: return kLink;
    case Icon::ZoomIn: return kZoomIn;
    case Icon::ZoomOut: return kZoomOut;
    case Icon::ScreenFull: return kScreenFull;
    case Icon::Sync: return kSync;
    case Icon::X: return kX;
    }
    return {};
}

}  // namespace

ComPtr<ID2D1PathGeometry> ParseSvgPath(ID2D1Factory* factory, std::wstring_view d, D2D1_FILL_MODE fill) {
    ComPtr<ID2D1PathGeometry> geo;
    if (FAILED(factory->CreatePathGeometry(&geo))) return nullptr;
    ComPtr<ID2D1GeometrySink> sink;
    if (FAILED(geo->Open(&sink))) return nullptr;
    sink->SetFillMode(fill);
    PathParser parser(d);
    parser.Build(sink.Get());
    if (FAILED(sink->Close())) return nullptr;
    return geo;
}

ID2D1PathGeometry* IconCache::Get(ID2D1Factory* factory, Icon icon) {
    if (factory_ != factory) {
        for (auto& g : geo_) g.Reset();
        factory_ = factory;
        factory_ref_ = factory;
    }
    ComPtr<ID2D1PathGeometry>& geo = geo_[static_cast<int>(icon)];
    if (!geo) geo = ParseSvgPath(factory, IconPath(icon));
    return geo.Get();
}

void DrawIcon(ID2D1RenderTarget* rt, IconCache& cache, Icon icon, const D2D1_RECT_F& rect, ID2D1Brush* brush) {
    ComPtr<ID2D1Factory> factory;
    rt->GetFactory(&factory);
    ID2D1PathGeometry* geo = cache.Get(factory.Get(), icon);
    if (!geo) return;
    D2D1_MATRIX_3X2_F old;
    rt->GetTransform(&old);
    const float sx = (rect.right - rect.left) / 16.f;
    const float sy = (rect.bottom - rect.top) / 16.f;
    rt->SetTransform(D2D1::Matrix3x2F::Scale(sx, sy) * D2D1::Matrix3x2F::Translation(rect.left, rect.top) * old);
    rt->FillGeometry(geo, brush);
    rt->SetTransform(old);
}

void DrawDisclosure(ID2D1RenderTarget* rt, const D2D1_RECT_F& r, bool open, ID2D1Brush* brush) {
    ComPtr<ID2D1Factory> factory;
    rt->GetFactory(&factory);
    ComPtr<ID2D1PathGeometry> geo;
    if (FAILED(factory->CreatePathGeometry(&geo))) return;
    ComPtr<ID2D1GeometrySink> sink;
    if (FAILED(geo->Open(&sink))) return;
    const float w = r.right - r.left, h = r.bottom - r.top;
    if (open) {
        sink->BeginFigure(D2D1::Point2F(r.left, r.top + h * 0.2f), D2D1_FIGURE_BEGIN_FILLED);
        sink->AddLine(D2D1::Point2F(r.right, r.top + h * 0.2f));
        sink->AddLine(D2D1::Point2F(r.left + w * 0.5f, r.top + h * 0.85f));
    } else {
        sink->BeginFigure(D2D1::Point2F(r.left + w * 0.2f, r.top), D2D1_FIGURE_BEGIN_FILLED);
        sink->AddLine(D2D1::Point2F(r.left + w * 0.85f, r.top + h * 0.5f));
        sink->AddLine(D2D1::Point2F(r.left + w * 0.2f, r.bottom));
    }
    sink->EndFigure(D2D1_FIGURE_END_CLOSED);
    sink->Close();
    rt->FillGeometry(geo.Get(), brush);
}

void DrawCheckbox(ID2D1RenderTarget* rt, IconCache& cache, const D2D1_RECT_F& r, bool checked,
                  ID2D1Brush* border, ID2D1Brush* fill, ID2D1Brush* check_fill, ID2D1Brush* checkmark) {
    const float w = r.right - r.left;
    const float radius = std::max(2.f, w * 0.2f);
    const D2D1_ROUNDED_RECT rr{r, radius, radius};
    if (checked) {
        rt->FillRoundedRectangle(rr, check_fill);
        const float inset = w * 0.12f;
        DrawIcon(rt, cache, Icon::Check, D2D1::RectF(r.left + inset, r.top + inset, r.right - inset, r.bottom - inset),
                 checkmark);
    } else {
        rt->FillRoundedRectangle(rr, fill);
        const D2D1_ROUNDED_RECT inner{D2D1::RectF(r.left + 0.5f, r.top + 0.5f, r.right - 0.5f, r.bottom - 0.5f),
                                      radius, radius};
        rt->DrawRoundedRectangle(inner, border, 1.f);
    }
}

}  // namespace mdv
