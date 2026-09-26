#include "core/flags.h"

#include <windows.h>
#include <compressapi.h>

#include <algorithm>
#include <cstring>
#include <map>
#include <mutex>
#include <string_view>
#include <unordered_map>

extern "C" IMAGE_DOS_HEADER __ImageBase;

namespace mdv {
namespace {

// Layout of the FLAGS resource: see scripts/gen_flags.py.
struct IndexEntry {
    char key[8];
    uint32_t offset;
    uint32_t length;
};
static_assert(sizeof(IndexEntry) == 16);

struct FlagSet {
    const IndexEntry* entries = nullptr;
    uint32_t count = 0;
    const uint8_t* packed = nullptr;
    uint32_t packed_size = 0;
    uint32_t unpacked_size = 0;
};

const FlagSet& Flags() {
    static const FlagSet set = [] {
        FlagSet s;
        const auto module = reinterpret_cast<HMODULE>(&__ImageBase);
        const HRSRC res = FindResourceW(module, L"FLAGS", MAKEINTRESOURCEW(10));  // RT_RCDATA
        const HGLOBAL mem = res ? LoadResource(module, res) : nullptr;
        const auto* p = mem ? static_cast<const uint8_t*>(LockResource(mem)) : nullptr;
        const uint64_t size = res ? SizeofResource(module, res) : 0;
        if (!p || size < 12 || std::memcmp(p, "MDVF", 4) != 0) return s;
        uint32_t count = 0, packed = 0;
        std::memcpy(&count, p + 4, 4);
        const uint64_t index_end = 8 + static_cast<uint64_t>(count) * sizeof(IndexEntry);
        if (index_end + 4 > size) return s;
        std::memcpy(&packed, p + index_end, 4);
        if (index_end + 4 + packed > size) return s;
        s.entries = reinterpret_cast<const IndexEntry*>(p + 8);
        s.count = count;
        s.packed = p + index_end + 4;
        s.packed_size = packed;
        for (uint32_t i = 0; i < count; ++i) {
            s.unpacked_size = std::max(s.unpacked_size, s.entries[i].offset + s.entries[i].length);
        }
        return s;
    }();
    return set;
}

std::string_view KeyOf(const IndexEntry& e) { return std::string_view(e.key, strnlen(e.key, sizeof e.key)); }

const IndexEntry* Find(std::string_view key) {
    const FlagSet& s = Flags();
    const IndexEntry* end = s.entries + s.count;
    const IndexEntry* it = std::lower_bound(s.entries, end, key,
                                            [](const IndexEntry& e, std::string_view k) { return KeyOf(e) < k; });
    return it != end && KeyOf(*it) == key ? it : nullptr;
}

// The Compression API (cabinet.dll) exists since Windows 8.
struct Cabinet {
    decltype(&CreateDecompressor) create = nullptr;
    decltype(&Decompress) decompress = nullptr;
    decltype(&CloseDecompressor) close = nullptr;
    bool ok() const { return create && decompress && close; }
};

const Cabinet& Api() {
    static const Cabinet api = [] {
        Cabinet c;
        if (const HMODULE dll = LoadLibraryExW(L"cabinet.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32)) {
            auto get = [dll](const char* name) { return reinterpret_cast<void*>(GetProcAddress(dll, name)); };
            c.create = reinterpret_cast<decltype(&CreateDecompressor)>(get("CreateDecompressor"));
            c.decompress = reinterpret_cast<decltype(&Decompress)>(get("Decompress"));
            c.close = reinterpret_cast<decltype(&CloseDecompressor)>(get("CloseDecompressor"));
        }
        return c;
    }();
    return api;
}

std::string Unpack() {
    const FlagSet& s = Flags();
    const Cabinet& api = Api();
    DECOMPRESSOR_HANDLE h = nullptr;
    if (!s.packed || s.unpacked_size == 0 || !api.ok() || !api.create(COMPRESS_ALGORITHM_MSZIP, nullptr, &h)) {
        return {};
    }
    std::string out(s.unpacked_size, '\0');
    SIZE_T n = 0;
    const bool ok = api.decompress(h, s.packed, s.packed_size, out.data(), out.size(), &n) && n == out.size();
    api.close(h);
    return ok ? out : std::string();
}

bool IsRegionalIndicator(const std::wstring& t, size_t i) {
    return i + 1 < t.size() && t[i] == 0xD83C && t[i + 1] >= 0xDDE6 && t[i + 1] <= 0xDDFF;
}

// U+E0020..U+E007E: tag characters spelling the subdivision code.
bool IsTagChar(const std::wstring& t, size_t i) {
    return i + 1 < t.size() && t[i] == 0xDB40 && t[i + 1] >= 0xDC20 && t[i + 1] <= 0xDC7E;
}

}  // namespace

void AddFlagImages(Document& doc) {
    if (Flags().count == 0) return;
    int usable = -1;  // decompressor present? Asked on the first flag only.
    std::map<std::string, int32_t> images;
    for (Block* leaf : doc.leaves) {
        const std::wstring& t = leaf->text;
        if (leaf->base_flags & (kSup | kSub)) continue;
        size_t run = 0;
        bool added = false;
        // The objects already there (images, formulas, diagrams over their
        // source), in text order: nothing inside one becomes a flag.
        const size_t existing = leaf->objects.size();
        size_t next = 0;
        for (size_t i = 0; i + 3 < t.size();) {
            while (next < existing && leaf->objects[next].pos + leaf->objects[next].length <= i) ++next;
            if (next < existing && leaf->objects[next].pos <= i) {
                i = leaf->objects[next].pos + leaf->objects[next].length;
                continue;
            }
            std::string key;
            size_t len = 0;
            if (IsRegionalIndicator(t, i)) {
                // Indicators pair up from the start of a sequence, flags or not.
                if (!IsRegionalIndicator(t, i + 2)) {
                    i += 2;
                    continue;
                }
                key = {static_cast<char>('a' + (t[i + 1] - 0xDDE6)), static_cast<char>('a' + (t[i + 3] - 0xDDE6))};
                len = 4;
            } else if (t[i] == 0xD83C && t[i + 1] == 0xDFF4) {  // U+1F3F4 waving black flag
                size_t j = i + 2;
                while (IsTagChar(t, j) && key.size() < 8) {
                    key.push_back(static_cast<char>(t[j + 1] - 0xDC00));
                    j += 2;
                }
                if (key.empty() || j + 1 >= t.size() || t[j] != 0xDB40 || t[j + 1] != 0xDC7F) {  // cancel tag
                    i += 2;
                    continue;
                }
                len = j + 2 - i;
            } else {
                ++i;
                continue;
            }
            while (run < leaf->runs.size() && leaf->runs[run].start + leaf->runs[run].length <= i) ++run;
            // Superscripts stay letters; formulas are objects of their own.
            const bool raised = run < leaf->runs.size() && leaf->runs[run].start <= i &&
                                (leaf->runs[run].flags & (kSup | kSub | kMath));
            const bool overlaps = next < existing && leaf->objects[next].pos < i + len;
            if (!raised && !overlaps && Find(key)) {
                if (usable < 0) usable = Api().ok();
                if (!usable) return;  // letters, as Windows shows them
                const auto [it, inserted] = images.try_emplace(key, static_cast<int32_t>(doc.images.size()));
                if (inserted) {
                    ImageRef ref;
                    ref.src.assign(key.begin(), key.end());
                    ref.alt = t.substr(i, len);
                    ref.flag = true;
                    doc.images.push_back(std::move(ref));
                }
                InlineObject obj;
                obj.pos = static_cast<uint32_t>(i);
                obj.length = static_cast<uint32_t>(len);
                obj.image = it->second;
                leaf->objects.push_back(obj);
                added = true;
            }
            i += len;
        }
        if (added) {
            std::sort(leaf->objects.begin(), leaf->objects.end(),
                      [](const InlineObject& a, const InlineObject& b) { return a.pos < b.pos; });
        }
    }
}

std::vector<std::string> FlagSvgs(const std::vector<std::string>& keys) {
    // The few flags a document uses stay cached (a couple of KB each); the
    // whole set is only unpacked for flags not seen before.
    static std::mutex mu;
    static std::unordered_map<std::string, std::string> cache;
    std::vector<std::string> out(keys.size());
    std::lock_guard<std::mutex> lock(mu);
    std::string all;
    bool unpacked = false;
    for (size_t i = 0; i < keys.size(); ++i) {
        const IndexEntry* e = Find(keys[i]);
        if (!e) continue;
        auto it = cache.find(keys[i]);
        if (it == cache.end()) {
            if (!unpacked) {
                all = Unpack();
                unpacked = true;
            }
            if (all.size() < static_cast<uint64_t>(e->offset) + e->length) continue;
            it = cache.emplace(keys[i], all.substr(e->offset, e->length)).first;
        }
        out[i] = it->second;
    }
    return out;
}

}  // namespace mdv
