#include "core/images.h"

#include <d2d1_1.h>
#include <shlwapi.h>
#include <winhttp.h>

#include <algorithm>
#include <atomic>
#include <cstring>
#include <list>
#include <mutex>
#include <unordered_map>

#include "core/flags.h"
#include "core/icons.h"
#include "core/svg.h"
#include "core/theme.h"

extern "C" IMAGE_DOS_HEADER __ImageBase;

namespace mdv {
namespace {

constexpr uint64_t kMaxImageBytes = 40ull << 20;     // per image file / download
constexpr size_t kRemoteCacheBytes = 48u << 20;      // process-wide download cache
constexpr UINT kMaxDecodeWidth = 2560;
constexpr uint64_t kMaxDecodePixels = 16ull << 20;
constexpr uint64_t kMaxAnimationPixels = 4ull << 20;  // larger GIFs stay static
// Decoded pixels and bitmaps kept per document; beyond it the images drawn
// least recently are dropped (and decoded again when they come back).
#ifdef _WIN64
constexpr uint64_t kImageMemoryBudget = 192ull << 20;
#else
constexpr uint64_t kImageMemoryBudget = 96ull << 20;
#endif

struct Pixels {
    UINT w = 0, h = 0;
    std::vector<uint8_t> data;  // 32bpp premultiplied BGRA, stride w*4
};

bool IsSvgBytes(const std::string& b) {
    const size_t probe = std::min<size_t>(b.size(), 1024);
    const std::string_view head(b.data(), probe);
    return head.find("<svg") != std::string_view::npos ||
           (head.find("<?xml") != std::string_view::npos && b.find("<svg") != std::string::npos);
}

bool EndsWithNoCase(std::wstring_view s, std::wstring_view suffix) {
    return s.size() >= suffix.size() && EqualsNoCase(s.substr(s.size() - suffix.size()), suffix);
}

// ---- WIC decoding -----------------------------------------------------------
ComPtr<IWICImagingFactory> CreateWic() {
    ComPtr<IWICImagingFactory> f;
    CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&f));
    return f;
}

bool DecodeFrame(IWICImagingFactory* wic, IWICBitmapDecoder* decoder, Pixels& px, UINT& nat_w, UINT& nat_h) {
    ComPtr<IWICBitmapFrameDecode> frame;
    if (FAILED(decoder->GetFrame(0, &frame))) return false;
    UINT w = 0, h = 0;
    if (FAILED(frame->GetSize(&w, &h)) || w == 0 || h == 0) return false;
    nat_w = w;
    nat_h = h;
    ComPtr<IWICBitmapSource> src = frame;
    UINT tw = w, th = h;
    if (tw > kMaxDecodeWidth) {
        th = std::max<UINT>(1, static_cast<UINT>(static_cast<uint64_t>(th) * kMaxDecodeWidth / tw));
        tw = kMaxDecodeWidth;
    }
    while (static_cast<uint64_t>(tw) * th > kMaxDecodePixels) {
        tw = std::max<UINT>(1, tw * 3 / 4);
        th = std::max<UINT>(1, th * 3 / 4);
    }
    if (tw != w || th != h) {
        ComPtr<IWICBitmapScaler> scaler;
        if (SUCCEEDED(wic->CreateBitmapScaler(&scaler)) &&
            SUCCEEDED(scaler->Initialize(src.Get(), tw, th, WICBitmapInterpolationModeFant))) {
            src = scaler;
        } else {
            tw = w;
            th = h;
        }
    }
    ComPtr<IWICFormatConverter> conv;
    if (FAILED(wic->CreateFormatConverter(&conv)) ||
        FAILED(conv->Initialize(src.Get(), GUID_WICPixelFormat32bppPBGRA, WICBitmapDitherTypeNone, nullptr, 0,
                                WICBitmapPaletteTypeMedianCut))) {
        return false;
    }
    px.w = tw;
    px.h = th;
    px.data.resize(static_cast<size_t>(tw) * th * 4);
    return SUCCEEDED(conv->CopyPixels(nullptr, tw * 4, static_cast<UINT>(px.data.size()), px.data.data()));
}

bool DecodeBytes(IWICImagingFactory* wic, const std::string& bytes, Pixels& px, UINT& w, UINT& h) {
    ComPtr<IStream> stream;
    stream.Attach(SHCreateMemStream(reinterpret_cast<const BYTE*>(bytes.data()), static_cast<UINT>(bytes.size())));
    if (!stream) return false;
    ComPtr<IWICBitmapDecoder> decoder;
    if (FAILED(wic->CreateDecoderFromStream(stream.Get(), nullptr, WICDecodeMetadataCacheOnDemand, &decoder))) {
        return false;
    }
    return DecodeFrame(wic, decoder.Get(), px, w, h);
}

bool ReadImageSize(IWICImagingFactory* wic, const std::wstring& path, UINT& w, UINT& h) {
    ComPtr<IWICBitmapDecoder> decoder;
    if (FAILED(wic->CreateDecoderFromFilename(path.c_str(), nullptr, GENERIC_READ, WICDecodeMetadataCacheOnDemand,
                                              &decoder))) {
        return false;
    }
    ComPtr<IWICBitmapFrameDecode> frame;
    return SUCCEEDED(decoder->GetFrame(0, &frame)) && SUCCEEDED(frame->GetSize(&w, &h)) && w > 0 && h > 0;
}

bool IsGifBytes(const std::string& b) {
    return b.size() > 6 && (b.compare(0, 6, "GIF89a") == 0 || b.compare(0, 6, "GIF87a") == 0);
}

UINT MetadataUInt(IWICMetadataQueryReader* q, const wchar_t* name, UINT fallback) {
    if (!q) return fallback;
    PROPVARIANT v;
    PropVariantInit(&v);
    UINT r = fallback;
    if (SUCCEEDED(q->GetMetadataByName(name, &v))) {
        if (v.vt == VT_UI1) r = v.bVal;
        else if (v.vt == VT_UI2) r = v.uiVal;
        else if (v.vt == VT_UI4) r = v.ulVal;
    }
    PropVariantClear(&v);
    return r;
}

}  // namespace

// Plays an animated GIF by compositing frames on demand into one canvas
// (frames are never all decoded at once).
class GifAnimation {
public:
    // Fails for single-frame or oversized GIFs (those are shown statically).
    bool Open(IWICImagingFactory* wic, const std::string& bytes) {
        wic_ = wic;
        stream_.Attach(SHCreateMemStream(reinterpret_cast<const BYTE*>(bytes.data()), static_cast<UINT>(bytes.size())));
        if (!stream_ ||
            FAILED(wic->CreateDecoderFromStream(stream_.Get(), nullptr, WICDecodeMetadataCacheOnDemand, &decoder_))) {
            return false;
        }
        if (FAILED(decoder_->GetFrameCount(&frames_)) || frames_ < 2) return false;
        ComPtr<IWICMetadataQueryReader> q;
        decoder_->GetMetadataQueryReader(&q);
        width_ = MetadataUInt(q.Get(), L"/logscrdesc/Width", 0);
        height_ = MetadataUInt(q.Get(), L"/logscrdesc/Height", 0);
        if (width_ == 0 || height_ == 0) {
            ComPtr<IWICBitmapFrameDecode> f;
            if (FAILED(decoder_->GetFrame(0, &f)) || FAILED(f->GetSize(&width_, &height_))) return false;
        }
        if (width_ == 0 || height_ == 0 || width_ > kMaxDecodeWidth ||
            static_cast<uint64_t>(width_) * height_ > kMaxAnimationPixels) {
            return false;
        }
        // Like browsers: without the NETSCAPE2.0 extension a GIF plays once;
        // loop count 0 means forever, N means N repetitions.
        plays_left_ = 1;
        if (q) {
            PROPVARIANT v;
            PropVariantInit(&v);
            if (SUCCEEDED(q->GetMetadataByName(L"/appext/application", &v)) && v.vt == (VT_UI1 | VT_VECTOR) &&
                v.caub.cElems == 11 &&
                (!memcmp(v.caub.pElems, "NETSCAPE2.0", 11) || !memcmp(v.caub.pElems, "ANIMEXTS1.0", 11))) {
                PropVariantClear(&v);
                if (SUCCEEDED(q->GetMetadataByName(L"/appext/data", &v)) && v.vt == (VT_UI1 | VT_VECTOR) &&
                    v.caub.cElems >= 4 && v.caub.pElems[0] > 0 && v.caub.pElems[1] == 1) {
                    const UINT loops = MAKEWORD(v.caub.pElems[2], v.caub.pElems[3]);
                    plays_left_ = loops == 0 ? 0 : loops + 1;
                }
            }
            PropVariantClear(&v);
        }
        canvas_.assign(static_cast<size_t>(width_) * height_ * 4, 0);
        return true;
    }

    // Renders frame `index` (0, or the frame after the current one).
    bool RenderFrame(UINT index) {
        if (index == 0) {
            std::fill(canvas_.begin(), canvas_.end(), uint8_t{0});
            prev_disposal_ = 0;
        } else if (prev_disposal_ == 2) {
            ClearRect(prev_);
        } else if (prev_disposal_ == 3 && saved_.size() == canvas_.size()) {
            canvas_ = saved_;
        }
        ComPtr<IWICBitmapFrameDecode> frame;
        if (FAILED(decoder_->GetFrame(index, &frame))) return false;
        ComPtr<IWICMetadataQueryReader> q;
        frame->GetMetadataQueryReader(&q);
        const UINT left = MetadataUInt(q.Get(), L"/imgdesc/Left", 0);
        const UINT top = MetadataUInt(q.Get(), L"/imgdesc/Top", 0);
        const UINT delay_cs = MetadataUInt(q.Get(), L"/grctlext/Delay", 10);
        const UINT disposal = MetadataUInt(q.Get(), L"/grctlext/Disposal", 0);
        delay_ms_ = delay_cs < 2 ? 100.0 : delay_cs * 10.0;  // like browsers
        if (disposal == 3) saved_ = canvas_;
        UINT fw = 0, fh = 0;
        frame->GetSize(&fw, &fh);
        ComPtr<IWICFormatConverter> conv;
        if (fw == 0 || fh == 0 || FAILED(wic_->CreateFormatConverter(&conv)) ||
            FAILED(conv->Initialize(frame.Get(), GUID_WICPixelFormat32bppPBGRA, WICBitmapDitherTypeNone, nullptr, 0,
                                    WICBitmapPaletteTypeCustom))) {
            return false;
        }
        // Only the part of the frame on the canvas: a frame may claim to be
        // far bigger than the canvas (up to 65535 x 65535).
        const UINT cw = left < width_ ? std::min(fw, width_ - left) : 0;
        const UINT ch = top < height_ ? std::min(fh, height_ - top) : 0;
        std::vector<uint8_t> px(static_cast<size_t>(cw) * ch * 4);
        if (cw && ch) {
            const WICRect rc{0, 0, static_cast<INT>(cw), static_cast<INT>(ch)};
            if (FAILED(conv->CopyPixels(&rc, cw * 4, static_cast<UINT>(px.size()), px.data()))) return false;
        }
        for (UINT y = 0; y < ch; ++y) {
            for (UINT x = 0; x < cw; ++x) {
                const uint8_t* s = &px[(static_cast<size_t>(y) * cw + x) * 4];
                if (s[3] == 0) continue;  // transparent pixel keeps the canvas
                uint8_t* d = &canvas_[(static_cast<size_t>(top + y) * width_ + left + x) * 4];
                d[0] = s[0];
                d[1] = s[1];
                d[2] = s[2];
                d[3] = s[3];
            }
        }
        prev_ = RECT{static_cast<LONG>(left), static_cast<LONG>(top), static_cast<LONG>(left + fw),
                     static_cast<LONG>(top + fh)};
        prev_disposal_ = disposal;
        current_ = index;
        return true;
    }

    // Steps to the next frame; false when the animation has finished.
    bool Next() {
        if (finished_) return false;
        if (current_ + 1 >= frames_ && plays_left_ != 0 && --plays_left_ == 0) {
            finished_ = true;
            return false;
        }
        if (!RenderFrame((current_ + 1) % frames_)) {
            finished_ = true;
            return false;
        }
        return true;
    }

    UINT width() const { return width_; }
    UINT height() const { return height_; }
    const std::vector<uint8_t>& canvas() const { return canvas_; }
    double delay_ms() const { return delay_ms_; }
    bool finished() const { return finished_; }

    double due = 0;  // NowMs() when the next frame is due

private:
    void ClearRect(const RECT& r) {
        for (LONG y = std::max<LONG>(r.top, 0); y < r.bottom && y < static_cast<LONG>(height_); ++y) {
            for (LONG x = std::max<LONG>(r.left, 0); x < r.right && x < static_cast<LONG>(width_); ++x) {
                std::memset(&canvas_[(static_cast<size_t>(y) * width_ + x) * 4], 0, 4);
            }
        }
    }

    ComPtr<IWICImagingFactory> wic_;
    ComPtr<IStream> stream_;
    ComPtr<IWICBitmapDecoder> decoder_;
    UINT frames_ = 0, width_ = 0, height_ = 0, current_ = 0;
    UINT plays_left_ = 0;
    bool finished_ = false;
    UINT prev_disposal_ = 0;
    RECT prev_{};
    double delay_ms_ = 100;
    std::vector<uint8_t> canvas_, saved_;
};

namespace {

// ---- HTTP ---------------------------------------------------------------------
HINTERNET HttpSession() {
    static std::once_flag once;
    static HINTERNET session = nullptr;
    std::call_once(once, [] {
        session = WinHttpOpen(L"mdview/" MDVIEW_VERSION " (Total Commander Lister plugin)",
                              WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY, WINHTTP_NO_PROXY_NAME,
                              WINHTTP_NO_PROXY_BYPASS, 0);
        if (!session) {
            session = WinHttpOpen(L"mdview/" MDVIEW_VERSION, WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
                                  WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
        }
        if (session) {
            WinHttpSetTimeouts(session, 4000, 5000, 8000, 15000);
            DWORD protocols = WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_2;
#ifdef WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_3
            protocols |= WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_3;
#endif
            if (!WinHttpSetOption(session, WINHTTP_OPTION_SECURE_PROTOCOLS, &protocols, sizeof protocols)) {
                protocols = WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_2;
                WinHttpSetOption(session, WINHTTP_OPTION_SECURE_PROTOCOLS, &protocols, sizeof protocols);
            }
        }
    });
    return session;
}

// `cancelled`: the viewer is gone. A download also gives up after a minute
// in all (a server may trickle a byte at a time, each within the timeouts).
bool HttpGet(const std::wstring& url, std::string& out, const std::atomic<bool>& cancelled) {
    constexpr double kDeadlineMs = 60000;
    const double deadline = NowMs() + kDeadlineMs;
    HINTERNET session = HttpSession();
    if (!session) return false;
    URL_COMPONENTS uc{};
    uc.dwStructSize = sizeof uc;
    wchar_t host[256] = {};
    uc.lpszHostName = host;
    uc.dwHostNameLength = static_cast<DWORD>(std::size(host));
    uc.dwUrlPathLength = static_cast<DWORD>(-1);
    uc.dwExtraInfoLength = static_cast<DWORD>(-1);
    if (!WinHttpCrackUrl(url.c_str(), static_cast<DWORD>(url.size()), 0, &uc)) return false;
    std::wstring path(uc.lpszUrlPath ? std::wstring(uc.lpszUrlPath, uc.dwUrlPathLength) : L"/");
    if (uc.lpszExtraInfo) path.append(uc.lpszExtraInfo, uc.dwExtraInfoLength);
    const bool secure = uc.nScheme == INTERNET_SCHEME_HTTPS;
    HINTERNET conn = WinHttpConnect(session, host, uc.nPort, 0);
    if (!conn) return false;
    bool ok = false;
    HINTERNET req = WinHttpOpenRequest(conn, L"GET", path.c_str(), nullptr, WINHTTP_NO_REFERER,
                                       WINHTTP_DEFAULT_ACCEPT_TYPES, secure ? WINHTTP_FLAG_SECURE : 0);
    if (req) {
        const wchar_t* headers = L"Accept: image/avif,image/webp,image/png,image/svg+xml,image/*;q=0.8,*/*;q=0.5\r\n";
        if (WinHttpSendRequest(req, headers, static_cast<DWORD>(-1L), WINHTTP_NO_REQUEST_DATA, 0, 0, 0) &&
            WinHttpReceiveResponse(req, nullptr)) {
            DWORD status = 0, len = sizeof status;
            WinHttpQueryHeaders(req, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, WINHTTP_HEADER_NAME_BY_INDEX,
                                &status, &len, WINHTTP_NO_HEADER_INDEX);
            if (status == 200) {
                ok = true;
                char buf[16384];
                for (;;) {
                    DWORD got = 0;
                    if (cancelled || NowMs() > deadline || !WinHttpReadData(req, buf, sizeof buf, &got)) {
                        ok = false;
                        break;
                    }
                    if (got == 0) break;
                    out.append(buf, got);
                    if (out.size() > kMaxImageBytes) {
                        ok = false;
                        break;
                    }
                }
            }
        }
        WinHttpCloseHandle(req);
    }
    WinHttpCloseHandle(conn);
    return ok && !out.empty();
}

// Process-wide cache of downloaded image bytes (LRU, bounded).
class RemoteCache {
public:
    static RemoteCache& Get() {
        static RemoteCache cache;
        return cache;
    }
    bool Lookup(const std::wstring& url, std::shared_ptr<const std::string>& out) {
        std::lock_guard<std::mutex> lock(mu_);
        auto it = map_.find(url);
        if (it == map_.end()) return false;
        if (!it->second->bytes && NowMs() - it->second->time > kRetryFailedMs) {
            // A failure may have been transient: try again.
            lru_.erase(it->second);
            map_.erase(it);
            return false;
        }
        lru_.splice(lru_.begin(), lru_, it->second);
        out = it->second->bytes;
        return true;
    }
    void Store(const std::wstring& url, std::shared_ptr<const std::string> bytes) {
        std::lock_guard<std::mutex> lock(mu_);
        if (auto it = map_.find(url); it != map_.end()) {
            if (it->second->bytes || !bytes) return;
            lru_.erase(it->second);  // a retry succeeded
            map_.erase(it);
        }
        total_ += bytes ? bytes->size() : 0;
        lru_.push_front(Item{url, std::move(bytes), NowMs()});
        map_[url] = lru_.begin();
        while (total_ > kRemoteCacheBytes && lru_.size() > 1) {
            Item& last = lru_.back();
            total_ -= last.bytes ? last.bytes->size() : 0;
            map_.erase(last.url);
            lru_.pop_back();
        }
    }

private:
    static constexpr double kRetryFailedMs = 30000;
    struct Item {
        std::wstring url;
        std::shared_ptr<const std::string> bytes;  // null = failed download
        double time = 0;
    };
    std::mutex mu_;
    std::list<Item> lru_;
    std::unordered_map<std::wstring, std::list<Item>::iterator> map_;
    size_t total_ = 0;
};

// The image jobs' own thread pool: a document with hundreds of pictures
// queues its jobs here instead of making the process pool start a thread
// for each (they mostly wait for a download slot).
PTP_POOL ImagePool() {
    static const PTP_POOL pool = [] {
        const PTP_POOL p = CreateThreadpool(nullptr);
        if (p) {
            SetThreadpoolThreadMaximum(p, 8);
            if (!SetThreadpoolThreadMinimum(p, 1)) {
                CloseThreadpool(p);
                return PTP_POOL{};
            }
        }
        return p;
    }();
    return pool;
}

// Limits concurrent downloads.
class DownloadGate {
public:
    static DownloadGate& Get() {
        static DownloadGate gate;
        return gate;
    }
    void Enter() { WaitForSingleObject(sem_, INFINITE); }
    void Leave() { ReleaseSemaphore(sem_, 1, nullptr); }

private:
    DownloadGate() : sem_(CreateSemaphoreW(nullptr, 6, 6, nullptr)) {}
    HANDLE sem_;
};

}  // namespace

// ============================================================================

UINT ImageLoadedMessage() {
    static const UINT msg = RegisterWindowMessageW(L"mdview.ImageLoaded.v1");
    return msg;
}

std::wstring ResolveResourceUrl(const std::wstring& base_dir, std::wstring_view url, bool* remote) {
    if (remote) *remote = false;
    std::wstring_view u = Trim(url);
    if (u.empty()) return {};
    if (StartsWithNoCase(u, L"http://") || StartsWithNoCase(u, L"https://")) {
        if (remote) *remote = true;
        return std::wstring(u);
    }
    if (StartsWithNoCase(u, L"//")) {
        if (remote) *remote = true;
        return L"https:" + std::wstring(u);
    }
    if (StartsWithNoCase(u, L"data:")) return std::wstring(u);
    std::wstring path;
    if (StartsWithNoCase(u, L"file:")) {
        std::wstring_view rest = u.substr(5);
        while (!rest.empty() && rest.front() == L'/') rest.remove_prefix(1);
        // file:///C:/x -> C:/x ; file://server/share -> \\server\share
        if (rest.size() >= 2 && rest[1] == L':') path = PercentDecode(rest);
        else path = L"\\\\" + PercentDecode(rest);
    } else {
        // Reject other URL schemes (javascript:, mailto:, ...) but keep "C:\..."
        const size_t colon = u.find(L':');
        if (colon != std::wstring_view::npos && colon > 1) {
            bool scheme = true;
            for (size_t i = 0; i < colon; ++i) {
                const wchar_t c = u[i];
                if (!((c >= L'a' && c <= L'z') || (c >= L'A' && c <= L'Z') || (i > 0 && (c == L'+' || c == L'-' || c == L'.')))) {
                    scheme = false;
                    break;
                }
            }
            if (scheme) return {};
        }
        std::wstring_view p = u;
        const size_t cut = p.find_first_of(L"?#");
        if (cut != std::wstring_view::npos) p = p.substr(0, cut);
        path = PercentDecode(p);
    }
    for (wchar_t& c : path) {
        if (c == L'/') c = L'\\';
    }
    if (path.empty()) return {};
    const bool absolute = (path.size() >= 2 && path[1] == L':') || (path.size() >= 2 && path[0] == L'\\' && path[1] == L'\\');
    std::wstring full = absolute ? path : base_dir + (path[0] == L'\\' ? path.substr(1) : path);
    wchar_t buf[2048];
    const DWORD n = GetFullPathNameW(full.c_str(), static_cast<DWORD>(std::size(buf)), buf, nullptr);
    if (n == 0 || n >= std::size(buf)) return full;
    return std::wstring(buf, n);
}

// ============================================================================

struct ImageStore::Impl {
    struct Job {
        int index = -1;
        uint32_t generation = 0;
        std::wstring url;
        bool remote = false;
        bool allow_remote = false;
        // results
        bool ok = false;
        UINT w = 0, h = 0;
        Pixels pixels;
        std::shared_ptr<SvgImage> svg;
        std::shared_ptr<const std::string> animated;  // GIF with several frames
    };
    struct Queue {
        std::mutex mu;
        HWND hwnd = nullptr;
        std::vector<std::unique_ptr<Job>> done;
        std::atomic<bool> cancelled{false};  // the document is gone: skip what hasn't run
    };

    const Document* doc = nullptr;
    HWND hwnd = nullptr;
    bool allow_remote = true;
    bool sync = false;              // offscreen rendering: load everything inline
    uint32_t generation = 1;
    std::vector<ImageEntry> entries;
    std::vector<Pixels> pending_pixels;
    std::vector<ID2D1RenderTarget*> bitmap_rt;
    std::shared_ptr<Queue> queue = std::make_shared<Queue>();
    ComPtr<IWICImagingFactory> wic;
    ComPtr<IDWriteFactory> dw;

    static void RunJob(Job& job, const std::atomic<bool>& cancelled) {
        if (cancelled) return;
        const bool com = SUCCEEDED(CoInitializeEx(nullptr, COINIT_MULTITHREADED));
        std::shared_ptr<const std::string> bytes;
        if (job.remote) {
            if (!RemoteCache::Get().Lookup(job.url, bytes) && job.allow_remote) {
                auto data = std::make_shared<std::string>();
                DownloadGate::Get().Enter();
                const bool got = !cancelled && HttpGet(job.url, *data, cancelled);
                DownloadGate::Get().Leave();
                bytes = got ? std::shared_ptr<const std::string>(std::move(data)) : nullptr;
                if (!cancelled) RemoteCache::Get().Store(job.url, bytes);  // (not a failure to remember)
            }
        } else if (StartsWithNoCase(job.url, L"data:")) {
            auto data = std::make_shared<std::string>();
            if (DecodeDataUri(job.url, *data)) bytes = std::move(data);
        } else {
            auto data = std::make_shared<std::string>();
            if (ReadFileBytes(job.url.c_str(), *data, kMaxImageBytes)) bytes = std::move(data);
        }
        if (bytes && !bytes->empty()) {
            if (IsSvgBytes(*bytes)) {
                job.svg = SvgImage::Parse(*bytes);
                if (job.svg) {
                    job.w = static_cast<UINT>(job.svg->width() + 0.5f);
                    job.h = static_cast<UINT>(job.svg->height() + 0.5f);
                    job.ok = true;
                }
            } else if (auto wic = CreateWic()) {
                GifAnimation anim;
                if (IsGifBytes(*bytes) && anim.Open(wic.Get(), *bytes) && anim.RenderFrame(0)) {
                    job.w = job.pixels.w = anim.width();
                    job.h = job.pixels.h = anim.height();
                    job.pixels.data = anim.canvas();
                    job.animated = bytes;
                    job.ok = true;
                } else {
                    job.ok = DecodeBytes(wic.Get(), *bytes, job.pixels, job.w, job.h);
                }
            }
        }
        if (com) CoUninitialize();
    }

    struct Work {
        std::shared_ptr<Queue> queue;
        std::unique_ptr<Job> job;
    };

    static void CALLBACK WorkCallback(PTP_CALLBACK_INSTANCE, void* ctx) {
        std::unique_ptr<Work> work(static_cast<Work*>(ctx));
        try {
            RunJob(*work->job, work->queue->cancelled);
        } catch (...) {
            work->job->ok = false;
        }
        HWND target = nullptr;
        {
            std::lock_guard<std::mutex> lock(work->queue->mu);
            work->queue->done.push_back(std::move(work->job));
            target = work->queue->hwnd;
        }
        if (target) PostMessageW(target, ImageLoadedMessage(), 0, 0);
    }

    void Submit(int index, const std::wstring& url, bool remote) {
        auto job = std::make_unique<Job>();
        job->index = index;
        job->generation = generation;
        job->url = url;
        job->remote = remote;
        job->allow_remote = allow_remote;
        if (sync) {
            RunJob(*job, queue->cancelled);
            std::lock_guard<std::mutex> lock(queue->mu);
            queue->done.push_back(std::move(job));
            return;
        }
        // Keep the DLL loaded while the callback runs.
        TP_CALLBACK_ENVIRON env;
        InitializeThreadpoolEnvironment(&env);
        SetThreadpoolCallbackLibrary(&env, reinterpret_cast<HMODULE>(&__ImageBase));
        SetThreadpoolCallbackRunsLong(&env);
        if (const PTP_POOL pool = ImagePool()) SetThreadpoolCallbackPool(&env, pool);
        auto* work = new Work{queue, std::move(job)};
        if (!TrySubmitThreadpoolCallback(WorkCallback, work, &env)) {
            std::unique_ptr<Work> w(work);
            RunJob(*w->job, queue->cancelled);
            std::lock_guard<std::mutex> lock(queue->mu);
            queue->done.push_back(std::move(w->job));
        }
        DestroyThreadpoolEnvironment(&env);
    }
};

ImageStore::ImageStore() : impl_(std::make_unique<Impl>()) {}

ImageStore::~ImageStore() { Detach(); }

void ImageStore::Attach(const Document* doc, HWND notify, bool dark, bool allow_remote) {
    Detach();
    impl_->doc = doc;
    impl_->hwnd = notify;
    impl_->allow_remote = allow_remote;
    impl_->sync = notify == nullptr;
    dark_ = dark;
    {
        std::lock_guard<std::mutex> lock(impl_->queue->mu);
        impl_->queue->hwnd = notify;
    }
    const size_t n = doc ? doc->images.size() : 0;
    impl_->entries.assign(n, ImageEntry{});
    impl_->pending_pixels.assign(n, Pixels{});
    impl_->bitmap_rt.assign(n, nullptr);
    if (!impl_->dw) DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory),
                                        reinterpret_cast<IUnknown**>(impl_->dw.GetAddressOf()));
}

void ImageStore::Detach() {
    {
        std::lock_guard<std::mutex> lock(impl_->queue->mu);
        impl_->queue->hwnd = nullptr;
        impl_->queue->done.clear();
    }
    impl_->queue->cancelled = true;  // jobs still out stop early
    // A fresh queue: late results of the old document are simply dropped.
    impl_->queue = std::make_shared<Impl::Queue>();
    ++impl_->generation;
    impl_->doc = nullptr;
    impl_->entries.clear();
    impl_->pending_pixels.clear();
    impl_->bitmap_rt.clear();
}

void ImageStore::SetDark(bool dark) {
    if (dark == dark_) return;
    dark_ = dark;
    if (!impl_->doc) return;
    // Reload images whose source depends on the color scheme.
    bool any = false;
    for (size_t i = 0; i < impl_->entries.size(); ++i) {
        const ImageRef& ref = impl_->doc->images[i];
        if (!ref.src_dark.empty() || !ref.src_light.empty()) {
            impl_->entries[i] = ImageEntry{};
            impl_->pending_pixels[i] = Pixels{};
            impl_->bitmap_rt[i] = nullptr;
            any = true;
        }
    }
    if (any) LoadAll();
}

void ImageStore::LoadAll() {
    const Document* doc = impl_->doc;
    if (!doc) return;
    if (!impl_->wic) impl_->wic = CreateWic();
    uint64_t eager_bytes = 0;
    std::vector<size_t> flags;
    for (size_t i = 0; i < impl_->entries.size(); ++i) {
        ImageEntry& e = impl_->entries[i];
        if (e.state != ImageState::Pending) continue;
        const ImageRef& ref = doc->images[i];
        if (ref.flag) {
            flags.push_back(i);
            continue;
        }
        std::wstring src = ref.src;
        if (dark_ && !ref.src_dark.empty()) src = ref.src_dark;
        else if (!dark_ && !ref.src_light.empty()) src = ref.src_light;
        bool remote = false;
        e.url = ResolveResourceUrl(doc->base_dir, src, &remote);
        e.remote = remote;
        // Never touch network shares on our own: loading \\host\share\x.png
        // makes Windows authenticate to that host (NTLM hash leak). Only
        // shares on the same server as the document itself are allowed.
        if (!remote && e.url.size() > 2 && e.url[0] == L'\\' && e.url[1] == L'\\') {
            auto server = [](const std::wstring& p) {
                const size_t end = p.find(L'\\', 2);
                return ToLower(p.substr(0, end == std::wstring::npos ? p.size() : end));
            };
            const bool doc_on_share = doc->base_dir.size() > 2 && doc->base_dir[0] == L'\\' && doc->base_dir[1] == L'\\';
            if (!doc_on_share || server(doc->base_dir) != server(e.url)) {
                e.state = ImageState::Failed;
                continue;
            }
        }
        if (e.url.empty() || (remote && !impl_->allow_remote)) {
            std::shared_ptr<const std::string> cached;
            if (!(remote && RemoteCache::Get().Lookup(e.url, cached) && cached)) {
                e.state = ImageState::Failed;
                continue;
            }
        }
        if (!remote && !StartsWithNoCase(e.url, L"data:")) {
            const DWORD attrs = GetFileAttributesW(e.url.c_str());
            if (attrs == INVALID_FILE_ATTRIBUTES || (attrs & FILE_ATTRIBUTE_DIRECTORY)) {
                e.state = ImageState::Failed;
                continue;
            }
            if (EndsWithNoCase(e.url, L".svg") || EndsWithNoCase(e.url, L".svgz")) {
                // Small text files: parse right away so the size is known.
                std::string bytes;
                if (ReadFileBytes(e.url.c_str(), bytes, kMaxImageBytes)) e.svg_image = SvgImage::Parse(bytes);
                if (e.svg_image) {
                    e.svg = true;
                    e.px_w = static_cast<UINT>(e.svg_image->width() + 0.5f);
                    e.px_h = static_cast<UINT>(e.svg_image->height() + 0.5f);
                    e.state = ImageState::Ready;
                } else {
                    e.state = ImageState::Failed;
                }
                continue;
            }
            if (impl_->wic) ReadImageSize(impl_->wic.Get(), e.url, e.px_w, e.px_h);
            if (e.px_w > 0 && e.px_h > 0) {
                // The size is known: pixels are decoded only once the image
                // is first drawn — offscreen always (a thumbnail shows few of
                // a document's pictures), on screen beyond half the budget.
                const uint64_t est = std::min<uint64_t>(static_cast<uint64_t>(e.px_w) * e.px_h, kMaxDecodePixels) * 4;
                if (impl_->sync || eager_bytes + est > kImageMemoryBudget / 2) {
                    e.state = ImageState::Ready;
                    continue;
                }
                eager_bytes += est;
            }
        }
        e.state = ImageState::Loading;
        impl_->Submit(static_cast<int>(i), e.url, remote);
    }
    if (!flags.empty()) {
        // Flag emoji: small SVGs from the embedded set, parsed right away.
        std::vector<std::string> keys;
        for (size_t i : flags) keys.push_back(WideToUtf8(doc->images[i].src));
        std::vector<std::string> svgs = FlagSvgs(keys);
        for (size_t k = 0; k < flags.size(); ++k) {
            ImageEntry& e = impl_->entries[flags[k]];
            e.url = L"flag:" + doc->images[flags[k]].src;
            e.svg_image = svgs[k].empty() ? nullptr : SvgImage::Parse(std::move(svgs[k]));
            e.svg = e.svg_image != nullptr;
            e.px_w = e.px_h = 36;
            e.state = e.svg ? ImageState::Ready : ImageState::Failed;
        }
    }
    if (impl_->sync) CompletePending();
}

uint64_t ImageStore::EntryBytes(size_t i) const {
    const ImageEntry& e = impl_->entries[i];
    uint64_t b = impl_->pending_pixels[i].data.size();
    if (e.bitmap) {
        const D2D1_SIZE_U s = e.bitmap->GetPixelSize();
        b += static_cast<uint64_t>(s.width) * s.height * 4;
    }
    if (e.animation) b += e.animation->canvas().size() * 2;  // canvas + disposal copy
    return b;
}

void ImageStore::TrimMemory() {
    if (impl_->sync) return;
    std::vector<size_t> victims;
    uint64_t total = 0;
    for (size_t i = 0; i < impl_->entries.size(); ++i) {
        const uint64_t b = EntryBytes(i);
        total += b;
        if (b > 0 && impl_->entries[i].drawn_stamp != paint_stamp_) victims.push_back(i);
    }
    if (total <= kImageMemoryBudget) return;
    // Least recently drawn first; of the never drawn, those further down.
    const std::vector<ImageEntry>& entries = impl_->entries;
    std::sort(victims.begin(), victims.end(), [&entries](size_t a, size_t b) {
        const uint32_t sa = entries[a].drawn_stamp, sb = entries[b].drawn_stamp;
        return sa != sb ? sa < sb : a > b;
    });
    for (size_t i : victims) {
        if (total <= kImageMemoryBudget / 4 * 3) break;
        total -= EntryBytes(i);
        ImageEntry& e = impl_->entries[i];
        e.bitmap.Reset();
        impl_->bitmap_rt[i] = nullptr;
        impl_->pending_pixels[i] = Pixels{};
        e.animation.reset();
        e.encoded.reset();
    }
}

bool ImageStore::CompletePending() {
    std::vector<std::unique_ptr<Impl::Job>> done;
    {
        std::lock_guard<std::mutex> lock(impl_->queue->mu);
        done.swap(impl_->queue->done);
    }
    bool resized = false;
    for (auto& job : done) {
        if (job->generation != impl_->generation || job->index < 0 ||
            static_cast<size_t>(job->index) >= impl_->entries.size()) {
            continue;
        }
        ImageEntry& e = impl_->entries[static_cast<size_t>(job->index)];
        if (e.url != job->url) continue;
        const UINT old_w = e.px_w, old_h = e.px_h;
        if (job->ok) {
            e.state = ImageState::Ready;
            e.px_w = job->w;
            e.px_h = job->h;
            if (job->svg) {
                e.svg = true;
                e.svg_image = std::move(job->svg);
            } else {
                impl_->pending_pixels[static_cast<size_t>(job->index)] = std::move(job->pixels);
                e.bitmap.Reset();
                // Offscreen rendering (thumbnails, printing) shows frame 0.
                if (!impl_->sync && !e.animation) e.encoded = std::move(job->animated);
            }
        } else {
            e.state = ImageState::Failed;
        }
        if (e.px_w != old_w || e.px_h != old_h || !job->ok) resized = true;
    }
    if (!done.empty()) TrimMemory();
    return resized;
}

double ImageStore::NextAnimationDue() const {
    double due = 0;
    for (const ImageEntry& e : impl_->entries) {
        if (e.animation && e.drawn_stamp == paint_stamp_ && !e.animation->finished()) {
            if (due == 0 || e.animation->due < due) due = e.animation->due;
        }
    }
    return due;
}

bool ImageStore::AdvanceAnimations(double now_ms) {
    bool changed = false;
    for (size_t i = 0; i < impl_->entries.size(); ++i) {
        ImageEntry& e = impl_->entries[i];
        // Only animations visible in the last paint play; others wait.
        if (!e.animation || e.drawn_stamp != paint_stamp_ || e.animation->due > now_ms) continue;
        GifAnimation& a = *e.animation;
        if (!a.Next()) continue;
        // After a long pause (scrolled away, window hidden) continue from now
        // instead of fast-forwarding.
        a.due = a.due + a.delay_ms() < now_ms - 250 ? now_ms + a.delay_ms() : a.due + a.delay_ms();
        if (e.bitmap && FAILED(e.bitmap->CopyFromMemory(nullptr, a.canvas().data(), a.width() * 4))) {
            e.bitmap.Reset();
            impl_->bitmap_rt[i] = nullptr;
        }
        changed = true;
    }
    return changed;
}

void ImageStore::ReleaseDeviceResources() {
    for (size_t i = 0; i < impl_->entries.size(); ++i) {
        impl_->entries[i].bitmap.Reset();
        impl_->bitmap_rt[i] = nullptr;
        if (impl_->entries[i].svg_image) impl_->entries[i].svg_image->ReleaseDeviceResources();
    }
}

const ImageEntry* ImageStore::Entry(int index) const {
    if (index < 0 || static_cast<size_t>(index) >= impl_->entries.size()) return nullptr;
    return &impl_->entries[static_cast<size_t>(index)];
}

std::wstring ImageStore::ResolvedUrl(int index) const {
    const ImageEntry* e = Entry(index);
    return e ? e->url : std::wstring();
}

bool ImageStore::Hidden(int index) const {
    if (!impl_->doc || index < 0 || static_cast<size_t>(index) >= impl_->doc->images.size()) return false;
    const uint8_t only = impl_->doc->images[static_cast<size_t>(index)].theme_only;
    return (only == 1 && dark_) || (only == 2 && !dark_);
}

D2D1_SIZE_F ImageStore::LayoutSize(int index, float max_w, float line_h, float zoom) {
    if (!impl_->doc || index < 0 || static_cast<size_t>(index) >= impl_->entries.size()) {
        return D2D1::SizeF(line_h, line_h);
    }
    if (Hidden(index)) return D2D1::SizeF(0, 0);
    const ImageRef& ref = impl_->doc->images[static_cast<size_t>(index)];
    const ImageEntry& e = impl_->entries[static_cast<size_t>(index)];
    const float nw = static_cast<float>(e.px_w), nh = static_cast<float>(e.px_h);
    const bool known = nw > 0 && nh > 0 && e.state != ImageState::Failed;
    float w = 0, h = 0;
    // CSS pixels scale with the zoom like text does; `max_w` and `line_h` are
    // zoomed already.
    if (ref.width > 0) w = ref.width_percent ? (max_w > 0 ? max_w * ref.width / 100.f : 0) : ref.width * zoom;
    if (ref.height > 0 && !ref.height_percent) h = ref.height * zoom;
    if (w > 0 && h <= 0) h = known ? w * nh / nw : 0;
    if (h > 0 && w <= 0) w = known ? h * nw / nh : 0;
    if (w <= 0 || h <= 0) {
        if (known) {
            w = nw * zoom;
            h = nh * zoom;
        } else {
            // Placeholder: icon + alt text, one line high.
            const float em = line_h / 1.5f;
            float text_w = 0;
            if (!ref.alt.empty() && impl_->dw) {
                ComPtr<IDWriteTextFormat> fmt;
                ComPtr<IDWriteTextLayout> tl;
                if (SUCCEEDED(impl_->dw->CreateTextFormat(L"Segoe UI", nullptr, DWRITE_FONT_WEIGHT_NORMAL,
                                                          DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL,
                                                          em * 0.875f, L"", &fmt)) &&
                    SUCCEEDED(impl_->dw->CreateTextLayout(ref.alt.c_str(), static_cast<UINT32>(ref.alt.size()),
                                                          fmt.Get(), 100000.f, 100000.f, &tl))) {
                    DWRITE_TEXT_METRICS tm{};
                    tl->GetMetrics(&tm);
                    text_w = std::min(tm.widthIncludingTrailingWhitespace + em * 0.5f, em * 30.f);
                }
            }
            if (w <= 0) w = em * 1.75f + text_w;
            if (h <= 0) h = em * 1.5f;
        }
    }
    if (max_w > 0 && w > max_w) {
        h *= max_w / w;
        w = max_w;
    }
    // Never let a degenerate size (inf, NaN) into the layout.
    auto sane = [](float v) { return v >= 1.f ? std::min(v, 32767.f) : 1.f; };
    return D2D1::SizeF(sane(w), sane(h));
}

void ImageStore::Draw(ID2D1RenderTarget* rt, int index, const D2D1_RECT_F& r, Palette* palette,
                      IDWriteFactory* dw, float em) {
    if (index < 0 || static_cast<size_t>(index) >= impl_->entries.size() || !palette) return;
    ImageEntry& e = impl_->entries[static_cast<size_t>(index)];
    const size_t i = static_cast<size_t>(index);
    e.drawn_stamp = paint_stamp_;
    if (e.state == ImageState::Ready) {
        if (e.svg && e.svg_image) {
            e.svg_image->Draw(rt, r, dw ? dw : impl_->dw.Get());
            return;
        }
        if (impl_->bitmap_rt[i] != rt) {
            e.bitmap.Reset();
            impl_->bitmap_rt[i] = nullptr;
        }
        if (impl_->sync && !e.bitmap && !e.animation && !e.encoded && impl_->pending_pixels[i].data.empty()) {
            // Offscreen, drawn the first time: decoded now (see LoadAll).
            impl_->Submit(index, e.url, e.remote);  // (runs inline)
            CompletePending();
        }
        if (e.encoded) {
            // First time on screen: start playing (frame 0 is already decoded).
            auto anim = std::make_shared<GifAnimation>();
            if (impl_->wic && anim->Open(impl_->wic.Get(), *e.encoded) && anim->RenderFrame(0)) {
                anim->due = NowMs() + anim->delay_ms();
                e.animation = std::move(anim);
                impl_->pending_pixels[i] = Pixels{};
                e.bitmap.Reset();
            }
            e.encoded.reset();
        }
        const D2D1_BITMAP_PROPERTIES props =
            D2D1::BitmapProperties(D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED));
        if (!e.bitmap && e.animation) {
            const GifAnimation& a = *e.animation;
            if (SUCCEEDED(rt->CreateBitmap(D2D1::SizeU(a.width(), a.height()), a.canvas().data(), a.width() * 4, props,
                                           &e.bitmap))) {
                impl_->bitmap_rt[i] = rt;
            }
        }
        if (!e.bitmap) {
            Pixels& px = impl_->pending_pixels[i];
            if (!px.data.empty()) {
                if (SUCCEEDED(rt->CreateBitmap(D2D1::SizeU(px.w, px.h), px.data.data(), px.w * 4, props, &e.bitmap))) {
                    impl_->bitmap_rt[i] = rt;
                    // Keep pixels only while other render targets may need them
                    // (offscreen rendering); a device loss triggers a reload.
                    if (!impl_->sync) px = Pixels{};
                }
            } else if (!impl_->sync && !e.animation) {
                // Device resources were lost: decode again.
                e.state = ImageState::Loading;
                impl_->Submit(index, e.url, e.remote);
            }
        }
        if (e.bitmap) {
            ComPtr<ID2D1DeviceContext> dc;
            const D2D1_SIZE_F bs = e.bitmap->GetSize();
            const float scale_x = (r.right - r.left) / std::max(bs.width, 1.f);
            if (SUCCEEDED(rt->QueryInterface(IID_PPV_ARGS(&dc)))) {
                // Shrunk or zoomed in: smooth; at about its size: fast.
                dc->DrawBitmap(e.bitmap.Get(), &r, 1.f,
                               (scale_x < 0.95f || scale_x > 1.5f) && !e.animation
                                   ? D2D1_INTERPOLATION_MODE_HIGH_QUALITY_CUBIC
                                   : D2D1_INTERPOLATION_MODE_LINEAR,
                               nullptr, nullptr);
            } else {
                rt->DrawBitmap(e.bitmap.Get(), r, 1.f, D2D1_BITMAP_INTERPOLATION_MODE_LINEAR);
            }
            return;
        }
    }

    // Placeholder (loading / failed): subtle box, image icon and alt text.
    const float w = r.right - r.left, h = r.bottom - r.top;
    if (w < 2 || h < 2) return;
    const float radius = std::min({em * 0.375f, w * 0.25f, h * 0.25f});
    rt->FillRoundedRectangle(D2D1_ROUNDED_RECT{r, radius, radius}, palette->Brush(kColorImagePlaceholder));
    const D2D1_RECT_F edge = D2D1::RectF(r.left + 0.5f, r.top + 0.5f, r.right - 0.5f, r.bottom - 0.5f);
    rt->DrawRoundedRectangle(D2D1_ROUNDED_RECT{edge, radius, radius}, palette->Brush(kColorBorderMuted), 1.f);
    if (e.state != ImageState::Failed && e.state != ImageState::Loading) return;
    const float isz = std::min({em, w - 4.f, h - 4.f});
    if (isz < 6) return;
    const float ix = r.left + std::min(em * 0.4f, (w - isz) * 0.5f);
    const float iy = r.top + (h - isz) * 0.5f;
    static thread_local IconCache icons;
    DrawIcon(rt, icons, Icon::Image, D2D1::RectF(ix, iy, ix + isz, iy + isz), palette->Brush(kColorMuted));
    const ImageRef& ref = impl_->doc->images[i];
    const float tx = ix + isz + em * 0.35f;
    if (!ref.alt.empty() && tx < r.right - em && dw) {
        ComPtr<IDWriteTextFormat> fmt;
        ComPtr<IDWriteTextLayout> tl;
        if (SUCCEEDED(dw->CreateTextFormat(L"Segoe UI", nullptr, DWRITE_FONT_WEIGHT_NORMAL, DWRITE_FONT_STYLE_NORMAL,
                                           DWRITE_FONT_STRETCH_NORMAL, em * 0.875f, L"", &fmt)) &&
            SUCCEEDED(dw->CreateTextLayout(ref.alt.c_str(), static_cast<UINT32>(ref.alt.size()), fmt.Get(),
                                           r.right - tx - em * 0.25f, h, &tl))) {
            tl->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
            DWRITE_TRIMMING trim{DWRITE_TRIMMING_GRANULARITY_CHARACTER, 0, 0};
            ComPtr<IDWriteInlineObject> ellipsis;
            dw->CreateEllipsisTrimmingSign(fmt.Get(), &ellipsis);
            tl->SetTrimming(&trim, ellipsis.Get());
            DWRITE_TEXT_METRICS tm{};
            tl->GetMetrics(&tm);
            rt->PushAxisAlignedClip(r, D2D1_ANTIALIAS_MODE_ALIASED);
            rt->DrawTextLayout(D2D1::Point2F(tx, r.top + (h - tm.height) * 0.5f), tl.Get(),
                               palette->Brush(kColorMuted), D2D1_DRAW_TEXT_OPTIONS_ENABLE_COLOR_FONT);
            rt->PopAxisAlignedClip();
        }
    }
}

}  // namespace mdv
