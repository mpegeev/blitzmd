// Image loading for a document: local files (WIC: PNG, JPEG, GIF, BMP, ICO,
// TIFF, WebP...), SVG (Direct2D SVG + our own <text> support) and remote
// http(s) images downloaded asynchronously and cached per process.
#pragma once

#include <d2d1.h>
#include <d2d1_3.h>
#include <wincodec.h>

#include <memory>
#include <string>
#include <vector>

#include "core/document.h"
#include "core/util.h"

namespace mdv {

class Palette;
class SvgImage;
class GifAnimation;

enum class ImageState : uint8_t { Pending, Loading, Ready, Failed };

struct ImageEntry {
    std::wstring url;            // resolved absolute path or http(s) URL
    bool remote = false;
    bool svg = false;
    ImageState state = ImageState::Pending;
    UINT px_w = 0, px_h = 0;     // natural size (image pixels == DIPs)
    ComPtr<IWICBitmapSource> pixels;     // decoded, 32bpp PBGRA (device independent)
    std::shared_ptr<SvgImage> svg_image; // parsed SVG (device independent)
    ComPtr<ID2D1Bitmap> bitmap;          // device resource, created on demand
    uint32_t bitmap_gen = 0;
    std::shared_ptr<const std::string> encoded;  // animated GIF source
    std::shared_ptr<GifAnimation> animation;     // created when first drawn
    uint32_t drawn_stamp = 0;                    // paint in which it was last drawn
};

class ImageStore {
public:
    ImageStore();
    ~ImageStore();
    ImageStore(const ImageStore&) = delete;
    ImageStore& operator=(const ImageStore&) = delete;

    // `notify` receives WM_MDVIEW_IMAGE when an asynchronous load finishes.
    void Attach(const Document* doc, HWND notify, bool dark, bool allow_remote);
    void Detach();
    void SetDark(bool dark);     // switches <picture> sources / gh-*-mode-only images
    bool dark() const { return dark_; }

    // Starts loading every image (local synchronously for their sizes,
    // pixel decoding and remote downloads asynchronously).
    void LoadAll();

    // Layout size in DIPs of image `index` constrained to `max_w`, with CSS
    // sizes multiplied by `zoom`. `line_h` sizes placeholders.
    D2D1_SIZE_F LayoutSize(int index, float max_w, float line_h, float zoom);
    bool Hidden(int index) const;
    // Draws image `index` into `r` (or a placeholder if not available).
    void Draw(ID2D1RenderTarget* rt, int index, const D2D1_RECT_F& r, Palette* palette,
              IDWriteFactory* dw, float em);
    // Called by the window when WM_MDVIEW_IMAGE arrives. Returns true if any
    // image changed size (layout must be redone).
    bool CompletePending();
    void ReleaseDeviceResources();
    const ImageEntry* Entry(int index) const;
    std::wstring ResolvedUrl(int index) const;

    // Animated GIFs: call BeginPaint() before drawing a frame; afterwards
    // NextAnimationDue() tells when (NowMs() time) a visible animation needs
    // its next frame (0 = none), and AdvanceAnimations() steps those that are
    // due. Returns true if anything changed.
    void BeginPaint() { ++paint_stamp_; }
    double NextAnimationDue() const;
    bool AdvanceAnimations(double now_ms);
    // Keeps decoded images within the memory budget; call after painting.
    void TrimMemory();

private:
    uint64_t EntryBytes(size_t i) const;

    struct Impl;
    std::unique_ptr<Impl> impl_;
    bool dark_ = false;
    uint32_t paint_stamp_ = 1;
};

// Registered window message sent when asynchronous image work completes.
UINT ImageLoadedMessage();

// Resolves a (possibly relative) URL against the document directory.
// Returns an absolute file path, an http(s) URL, or empty if unsupported.
std::wstring ResolveResourceUrl(const std::wstring& base_dir, std::wstring_view url, bool* remote);

}  // namespace mdv
