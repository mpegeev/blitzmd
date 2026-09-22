#include "spec_runner.h"

#include <cctype>
#include <cstdio>
#include <string>
#include <vector>

#include "core/parser.h"
#include "core/util.h"
#include "html_dump.h"
#include "md4c-html.h"
#include "md4c.h"

namespace mdv::test {
namespace {

struct Example {
    int number = 0;
    int line = 0;
    std::string section;
    std::string markdown;
    std::string html;
    std::string kind;  // "", "table", "strikethrough", "autolink", "tagfilter", "disabled"
    std::string flags;
};

std::vector<Example> LoadSpec(const std::string& text) {
    std::vector<Example> out;
    const std::string fence = "````````````````````````````````";
    size_t pos = 0;
    int line_no = 0;
    std::string section;
    int state = 0;  // 0 = prose, 1 = markdown, 2 = html
    Example cur;
    while (pos < text.size()) {
        size_t eol = text.find('\n', pos);
        if (eol == std::string::npos) eol = text.size();
        std::string line = text.substr(pos, eol - pos);
        if (!line.empty() && line.back() == '\r') line.pop_back();
        pos = eol + 1;
        ++line_no;
        if (state == 0) {
            if (line.rfind(fence + " example", 0) == 0) {
                state = 1;
                cur = Example{};
                cur.line = line_no;
                cur.section = section;
                cur.kind = line.size() > fence.size() + 9 ? line.substr(fence.size() + 9) : "";
            } else if (!line.empty() && line[0] == '#') {
                size_t h = line.find_first_not_of('#');
                if (h != std::string::npos && line[h] == ' ') section = line.substr(h + 1);
            }
        } else if (state == 1) {
            if (line == ".") state = 2;
            else cur.markdown += line + "\n";
        } else {
            if (line.rfind(fence, 0) == 0) {
                state = 0;
                cur.number = static_cast<int>(out.size()) + 1;
                out.push_back(cur);
            } else if (state == 3) {
                cur.flags += line + " ";  // md4c test files: "--ftables" etc.
            } else if (line == ".") {
                state = 3;
            } else {
                cur.html += line + "\n";
            }
        }
    }
    // Spec files use U+2192 for tabs.
    auto tabs = [](std::string& s) {
        const std::string arrow = "\xE2\x86\x92";
        for (size_t p = 0; (p = s.find(arrow, p)) != std::string::npos;) s.replace(p, 3, "\t");
    };
    for (auto& e : out) {
        tabs(e.markdown);
        tabs(e.html);
    }
    return out;
}

void AppendOut(const MD_CHAR* text, MD_SIZE size, void* ud) {
    static_cast<std::string*>(ud)->append(text, size);
}

std::string Md4cHtml(const std::string& md) {
    std::string out;
    md_html(md.data(), static_cast<MD_SIZE>(md.size()), AppendOut, &out,
            MD_FLAG_PERMISSIVEAUTOLINKS | MD_FLAG_TABLES | MD_FLAG_STRIKETHROUGH |
                MD_FLAG_TASKLISTS | MD_FLAG_ADMONITIONS | MD_FLAG_FOOTNOTES,
            0);
    return out;
}

bool HasRawHtml(const std::string& md) {
    for (size_t i = 0; i + 1 < md.size(); ++i) {
        if (md[i] != '<') continue;
        const char n = md[i + 1];
        if (!(std::isalpha(static_cast<unsigned char>(n)) || n == '/' || n == '!' || n == '?')) continue;
        // Autolinks like <http://...> or <foo@bar> are not raw HTML.
        const size_t close = md.find('>', i);
        const std::string inner = md.substr(i + 1, close == std::string::npos ? 0 : close - i - 1);
        if (inner.find(':') != std::string::npos && inner.find(' ') == std::string::npos) continue;
        if (inner.find('@') != std::string::npos && inner.find(' ') == std::string::npos) continue;
        return true;
    }
    return false;
}

}  // namespace

int RunSpec(const std::string& path, bool verbose, int only_example) {
    std::string bytes;
    if (!ReadFileBytes(Utf8ToWide(path).c_str(), bytes, 64u << 20)) {
        std::fprintf(stderr, "cannot read %s\n", path.c_str());
        return 2;
    }
    const auto examples = LoadSpec(bytes);
    int pass = 0, fail = 0, html_fail = 0, md4c_fail = 0, skipped = 0;
    ParseOptions opt;
    opt.emoji_shortcodes = false;
    opt.math = false;
    opt.highlight_code = false;
    for (const Example& e : examples) {
        if (only_example > 0 && e.number != only_example) continue;
        if (e.kind == "disabled") {
            ++skipped;
            continue;
        }
        auto doc = ParseMarkdown(e.markdown, L"", opt);
        const std::string ours = NormalizeHtml(DumpHtml(*doc));
        const std::string expected = NormalizeHtml(e.html);
        const std::string reference = NormalizeHtml(Md4cHtml(e.markdown));
        if (ours == expected) {
            ++pass;
            continue;
        }
        const bool raw_html = HasRawHtml(e.markdown) || e.section == "HTML blocks" ||
                              e.section == "Raw HTML" || e.kind == "tagfilter";
        const bool md4c_differs = reference != expected;
        if (raw_html) ++html_fail;
        else if (md4c_differs) ++md4c_fail;
        else ++fail;
        if (verbose || (!raw_html && !md4c_differs) || only_example > 0) {
            std::printf("--- example %d (line %d, %s)%s%s\n", e.number, e.line, e.section.c_str(),
                        raw_html ? " [raw html]" : "", md4c_differs ? " [md4c differs]" : "");
            std::printf("markdown:\n%s", e.markdown.c_str());
            std::printf("expected: %s\n", expected.c_str());
            std::printf("ours:     %s\n", ours.c_str());
            if (md4c_differs) std::printf("md4c:     %s\n", reference.c_str());
        }
    }
    std::printf("\n%s: %d examples, %d pass, %d FAIL, %d raw-html (interpreted, expected to differ), "
                "%d md4c-differs, %d skipped\n",
                path.c_str(), static_cast<int>(examples.size()), pass, fail, html_fail, md4c_fail,
                skipped);
    return fail == 0 ? 0 : 1;
}

}  // namespace mdv::test
