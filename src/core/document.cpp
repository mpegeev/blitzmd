#include "core/document.h"

#include <algorithm>

#include "core/util.h"

namespace mdv {

Document::Document() { root = NewBlock(BlockType::Document); }

Block* Document::NewBlock(BlockType type) {
    storage.push_back(std::make_unique<Block>());
    Block* b = storage.back().get();
    b->type = type;
    return b;
}

std::wstring Document::GlobalText() const {
    std::wstring out;
    out.reserve(text_length);
    for (const Block* leaf : leaves) {
        out.append(leaf->text);
        out.push_back(L'\n');
    }
    return out;
}

Block* Document::LeafAt(uint32_t pos) const {
    if (leaves.empty()) return nullptr;
    // Last leaf whose text_start <= pos.
    auto it = std::upper_bound(leaves.begin(), leaves.end(), pos,
                               [](uint32_t p, const Block* b) { return p < b->text_start; });
    if (it == leaves.begin()) return leaves.front();
    return *(it - 1);
}

const Anchor* Document::FindAnchor(std::wstring_view fragment) const {
    if (!fragment.empty() && fragment.front() == L'#') fragment.remove_prefix(1);
    if (fragment.empty()) return nullptr;
    auto find = [this](const std::wstring& key) -> const Anchor* {
        auto it = anchors.find(key);
        return it == anchors.end() ? nullptr : &it->second;
    };
    std::wstring key(fragment);
    if (auto* a = find(key)) return a;
    key = PercentDecode(fragment);
    if (auto* a = find(key)) return a;
    constexpr std::wstring_view kPrefix = L"user-content-";
    if (key.size() > kPrefix.size() && key.compare(0, kPrefix.size(), kPrefix) == 0) {
        key.erase(0, kPrefix.size());
        if (auto* a = find(key)) return a;
    }
    const std::wstring lower = ToLower(key);
    if (auto* a = find(lower)) return a;
    // Last resort: case-insensitive scan.
    for (const auto& [name, anchor] : anchors) {
        if (ToLower(name) == lower) return &anchor;
    }
    return nullptr;
}

}  // namespace mdv
