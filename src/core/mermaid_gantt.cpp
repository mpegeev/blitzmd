// Mermaid Gantt charts: tasks as bars on a time axis, grouped in sections
// (bands of alternating color), done / active / crit / milestone tasks, the
// dates in dateFormat, durations, "after" and "until" other tasks, excluded
// days, the axis ticks in axisFormat, a line for today.
#include <algorithm>
#include <cmath>
#include <ctime>
#include <cwchar>
#include <map>
#include <optional>

#include "core/mermaid_internal.h"

namespace mdv::mermaid {
namespace {

// ---------------------------------------------------------------------------
// Dates: milliseconds since 1970-01-01, no time zones (as written).
// ---------------------------------------------------------------------------

constexpr double kDay = 86400000.0;
constexpr double kMaxTime = 4e14;  // about ±12,700 years (Mermaid takes years within ±10000)

// Days since 1970-01-01 of a civil date (proleptic Gregorian).
long long DaysFromCivil(long long y, unsigned m, unsigned d) {
    y -= m <= 2;
    const long long era = (y >= 0 ? y : y - 399) / 400;
    const unsigned yoe = static_cast<unsigned>(y - era * 400);
    const unsigned doy = (153 * (m > 2 ? m - 3 : m + 9) + 2) / 5 + d - 1;
    const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + static_cast<long long>(doe) - 719468;
}

struct Civil {
    long long y;
    unsigned m, d;
};

Civil CivilFromDays(long long z) {
    z += 719468;
    const long long era = (z >= 0 ? z : z - 146096) / 146097;
    const unsigned doe = static_cast<unsigned>(z - era * 146097);
    const unsigned yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    const long long y = static_cast<long long>(yoe) + era * 400;
    const unsigned doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    const unsigned mp = (5 * doy + 2) / 153;
    const unsigned d = doy - (153 * mp + 2) / 5 + 1;
    const unsigned m = mp < 10 ? mp + 3 : mp - 9;
    return {y + (m <= 2), m, d};
}

double FromDays(long long days) { return static_cast<double>(days) * kDay; }
long long DayOf(double t) { return static_cast<long long>(std::floor(t / kDay)); }
// 0 Sunday (1970-01-01 was a Thursday).
int Weekday(long long day) { return static_cast<int>(((day % 7) + 11) % 7); }

// Now as a date and time in the local calendar (as Mermaid's today).
double LocalNow() {
    const std::time_t now = std::time(nullptr);
    std::tm lt{};
    localtime_s(&lt, &now);
    return FromDays(DaysFromCivil(lt.tm_year + 1900LL, static_cast<unsigned>(lt.tm_mon + 1), static_cast<unsigned>(lt.tm_mday))) +
           ((lt.tm_hour * 60.0 + lt.tm_min) * 60.0 + lt.tm_sec) * 1000.0;
}

const wchar_t* const kMonths[] = {L"January", L"February", L"March",     L"April",   L"May",      L"June",
                                  L"July",    L"August",   L"September", L"October", L"November", L"December"};
const wchar_t* const kWeekdays[] = {L"Sunday", L"Monday", L"Tuesday", L"Wednesday", L"Thursday", L"Friday", L"Saturday"};

// A date in a dayjs format (YYYY-MM-DD, DD/MM/YYYY HH:mm, MMM D YYYY, X...).
// Strictly as dayjs reads it for Mermaid (MM two digits, M no leading zero);
// else leniently, as JavaScript's Date takes more. Parts not given are
// today's, as in dayjs.
std::optional<double> ParseDate(std::wstring_view s, std::wstring_view format, bool strict = true) {
    long long year = 0, month = 0, day = 0, hour = 0, minute = 0, second = 0;
    bool has_year = false, has_month = false, has_day = false;
    double fraction = 0;
    bool pm = false, ampm = false;
    size_t i = 0, f = 0;
    auto digits = [&](size_t min, size_t max, long long* out) {
        size_t n = 0;
        long long v = 0;
        while (n < max && i < s.size() && s[i] >= L'0' && s[i] <= L'9') {
            v = v * 10 + (s[i] - L'0');
            ++i;
            ++n;
        }
        if (n < min) return false;
        *out = v;
        return true;
    };
    // A two-digit token (MM): exactly two, strictly; a one-digit one (M):
    // one or two, no leading zero strictly.
    auto two = [&](long long* out) { return digits(strict ? 2 : 1, 2, out); };
    auto one = [&](long long* out) {
        const size_t from = i;
        return digits(1, 2, out) && !(strict && i - from == 2 && s[from] == L'0');
    };
    while (f < format.size()) {
        const std::wstring_view rest = format.substr(f);
        auto token = [&](std::wstring_view t) {
            if (!rest.starts_with(t)) return false;
            f += t.size();
            return true;
        };
        if (token(L"YYYY")) {
            const bool sign = i < s.size() && (s[i] == L'-' || s[i] == L'+');
            const bool minus = sign && s[i] == L'-';
            if (sign) ++i;
            if (!digits(4, sign ? 6 : 4, &year)) return std::nullopt;
            if (minus) year = -year;
            has_year = true;
        } else if (token(L"YY")) {
            if (!digits(2, 2, &year)) return std::nullopt;
            year += year < 69 ? 2000 : 1900;  // as dayjs
            has_year = true;
        } else if (token(L"MMMM") || token(L"MMM")) {
            // Month names, whole or their first three letters.
            bool found = false;
            for (int m = 0; m < 12 && !found; ++m) {
                const std::wstring_view name = kMonths[m];
                const bool full = rest.starts_with(L"MMMM");
                for (const std::wstring_view n : {name, name.substr(0, 3)}) {
                    if (strict && (n.size() == 3) == full) continue;
                    if (s.size() - i >= n.size() && EqualsNoCase(s.substr(i, n.size()), n)) {
                        i += n.size();
                        month = m + 1;
                        found = true;
                        break;
                    }
                }
            }
            if (!found) return std::nullopt;
            has_month = true;
        } else if (token(L"MM") || token(L"M")) {
            if (!(rest.starts_with(L"MM") ? two(&month) : one(&month))) return std::nullopt;
            has_month = true;
        } else if (token(L"Do")) {
            if (!digits(1, 2, &day) || s.size() - i < 2) return std::nullopt;
            i += 2;  // 1st, 2nd, 3rd, 4th
            has_day = true;
        } else if (token(L"DD") || token(L"D")) {
            if (!(rest.starts_with(L"DD") ? two(&day) : one(&day))) return std::nullopt;
            has_day = true;
        } else if (token(L"HH") || token(L"H")) {
            if (!(rest.starts_with(L"HH") ? two(&hour) : one(&hour))) return std::nullopt;
        } else if (token(L"hh") || token(L"h")) {
            if (!(rest.starts_with(L"hh") ? two(&hour) : one(&hour))) return std::nullopt;
            ampm = true;
        } else if (token(L"mm") || token(L"m")) {
            if (!(rest.starts_with(L"mm") ? two(&minute) : one(&minute))) return std::nullopt;
        } else if (token(L"ss") || token(L"s")) {
            if (!(rest.starts_with(L"ss") ? two(&second) : one(&second))) return std::nullopt;
        } else if (rest[0] == L'S') {
            // Fractions of a second: S, SS, SSS.
            size_t n = 0;
            while (f < format.size() && format[f] == L'S') {
                ++f;
                ++n;
            }
            long long v = 0;
            const size_t from = i;
            if (!digits(strict ? std::min<size_t>(n, 9) : 1, std::min<size_t>(n, 9), &v)) return std::nullopt;
            fraction = static_cast<double>(v) / std::pow(10.0, static_cast<double>(i - from));
        } else if (token(L"ZZ") || token(L"Z")) {
            // An offset: +05:00, -0800 or Z, read and left aside.
            if (i < s.size() && s[i] == L'Z') {
                ++i;
            } else {
                if (i >= s.size() || (s[i] != L'+' && s[i] != L'-')) return std::nullopt;
                ++i;
                long long v = 0;
                if (!digits(2, 2, &v)) return std::nullopt;
                if (i < s.size() && s[i] == L':') ++i;
                if (!digits(2, 2, &v)) return std::nullopt;
            }
        } else if (token(L"A") || token(L"a")) {
            if (s.size() - i < 2) return std::nullopt;
            const std::wstring_view ap = s.substr(i, 2);
            if (EqualsNoCase(ap, L"pm")) pm = true;
            else if (!EqualsNoCase(ap, L"am")) return std::nullopt;
            ampm = true;
            i += 2;
        } else if (token(L"X") || token(L"x")) {
            // Unix time, in seconds or milliseconds.
            const bool seconds = rest[0] == L'X';
            const bool minus = i < s.size() && s[i] == L'-';
            if (minus) ++i;
            long long v = 0;
            if (!digits(1, 15, &v) || i != s.size()) return std::nullopt;
            const double t = (minus ? -1.0 : 1.0) * static_cast<double>(v) * (seconds ? 1000.0 : 1.0);
            if (std::abs(t) > kMaxTime) return std::nullopt;
            return t;
        } else if (rest[0] == L'[') {
            // [literal text]
            const size_t close = rest.find(L']');
            if (close == std::wstring_view::npos) return std::nullopt;
            const std::wstring_view literal = rest.substr(1, close - 1);
            if (s.substr(i).substr(0, literal.size()) != literal) return std::nullopt;
            i += literal.size();
            f += close + 1;
        } else {
            if (i >= s.size() || s[i] != format[f]) return std::nullopt;
            ++i;
            ++f;
        }
    }
    if (i != s.size()) return std::nullopt;
    if (ampm) {
        if (hour < 1 || hour > 12) return std::nullopt;
        hour = hour % 12 + (pm ? 12 : 0);
    }
    if (!has_year || !has_month || !has_day) {
        const Civil today = CivilFromDays(static_cast<long long>(std::floor(LocalNow() / kDay)));
        if (!has_day) day = !has_year && !has_month ? today.d : 1;
        if (!has_month) month = has_year ? 1 : today.m;
        if (!has_year) year = today.y;
    }
    if (month < 1 || month > 12 || day < 1 || day > 31 || hour > 23 || minute > 59 || second > 59) return std::nullopt;
    // No 31st of April (dayjs's strict parsing).
    const long long days = DaysFromCivil(year, static_cast<unsigned>(month), static_cast<unsigned>(day));
    if (CivilFromDays(days).m != month) return std::nullopt;
    const double t = FromDays(days) + static_cast<double>((hour * 60 + minute) * 60 + second) * 1000.0 + fraction * 1000.0;
    if (std::abs(t) > kMaxTime) return std::nullopt;
    return t;
}

// Adds n months (whole: dayjs drops the fraction), the day kept within the
// month: Jan 31 + 1 month is Feb 29 (or 28).
double AddMonths(double t, double n) {
    if (std::abs(n) > 200000) return std::copysign(1e300, n);
    const long long day = DayOf(t);
    const double time = t - FromDays(day);
    const Civil c = CivilFromDays(day);
    const long long m0 = static_cast<long long>(c.m) - 1 + static_cast<long long>(std::trunc(n));
    const long long y = c.y + (m0 >= 0 ? m0 / 12 : (m0 - 11) / 12);
    const unsigned m = static_cast<unsigned>(((m0 % 12) + 12) % 12) + 1;
    const unsigned last = CivilFromDays(DaysFromCivil(m == 12 ? y + 1 : y, m == 12 ? 1 : m + 1, 1) - 1).d;
    return FromDays(DaysFromCivil(y, m, std::min(c.d, last))) + time;
}

// 3d, 2w, 12h, 30m, 1.5d, 100ms, 1M, 2y added to t as dayjs adds them:
// days and weeks in whole days (rounded), months and years by the
// calendar, the rest exactly.
std::optional<double> AddDuration(double t, std::wstring_view s) {
    size_t k = 0;
    while (k < s.size() && s[k] >= L'0' && s[k] <= L'9') ++k;
    if (k == 0) return std::nullopt;
    if (k < s.size() && s[k] == L'.') {
        const size_t dot = k++;
        while (k < s.size() && s[k] >= L'0' && s[k] <= L'9') ++k;
        if (k == dot + 1) return std::nullopt;
    }
    if (k > 20) return std::nullopt;
    const std::wstring number(s.substr(0, k));
    const double v = std::wcstod(number.c_str(), nullptr);
    const std::wstring_view unit = s.substr(k);
    if (unit == L"ms") return t + v;
    if (unit == L"s") return t + v * 1000;
    if (unit == L"m") return t + v * 60000;
    if (unit == L"h") return t + v * 3600000;
    if (unit == L"d") return t + std::floor(v + 0.5) * kDay;
    if (unit == L"w") return t + std::floor(v * 7 + 0.5) * kDay;
    if (unit == L"M") return AddMonths(t, v);
    if (unit == L"y") return AddMonths(t, std::trunc(v) * 12);
    return std::nullopt;
}

// A date in strftime's style (%Y-%m-%d, %b %d, %H:%M...), as d3 formats axes.
std::wstring FormatDate(double t, std::wstring_view format) {
    const long long day = DayOf(t);
    const Civil c = CivilFromDays(day);
    const long long msd = static_cast<long long>(t - FromDays(day));
    const long long hour = msd / 3600000, minute = msd / 60000 % 60, second = msd / 1000 % 60, milli = msd % 1000;
    const int weekday = Weekday(day);
    std::wstring out;
    wchar_t buf[32];
    for (size_t i = 0; i < format.size(); ++i) {
        if (format[i] != L'%' || i + 1 >= format.size()) {
            out.push_back(format[i]);
            continue;
        }
        wchar_t f = format[++i];
        wchar_t pad = 0;  // a flag: - no padding, _ spaces, 0 zeros
        if ((f == L'-' || f == L'_' || f == L'0') && i + 1 < format.size()) {
            pad = f;
            f = format[++i];
        }
        auto number = [&](long long v, int width, wchar_t fill) {
            if (pad == L'-') width = 0;
            if (pad == L'_') fill = L' ';
            if (pad == L'0') fill = L'0';
            std::swprintf(buf, 32, fill == L' ' ? L"%*lld" : L"%0*lld", width, v);
            out += buf;
        };
        switch (f) {
        case L'Y': number(c.y, 0, L'0'); break;
        case L'y': number(((c.y % 100) + 100) % 100, 2, L'0'); break;
        case L'm': number(c.m, 2, L'0'); break;
        case L'd': number(c.d, 2, L'0'); break;
        case L'e': number(c.d, 2, L' '); break;
        case L'b': out.append(kMonths[c.m - 1], 3); break;
        case L'B': out += kMonths[c.m - 1]; break;
        case L'a': out.append(kWeekdays[weekday], 3); break;
        case L'A': out += kWeekdays[weekday]; break;
        case L'w': number(weekday, 0, L'0'); break;
        case L'u': number(weekday ? weekday : 7, 0, L'0'); break;
        case L'H': number(hour, 2, L'0'); break;
        case L'I': number((hour + 11) % 12 + 1, 2, L'0'); break;
        case L'M': number(minute, 2, L'0'); break;
        case L'S': number(second, 2, L'0'); break;
        case L'L': number(milli, 3, L'0'); break;
        case L'p': out += hour < 12 ? L"AM" : L"PM"; break;
        case L'j': number(day - DaysFromCivil(c.y, 1, 1) + 1, 3, L'0'); break;
        case L'U': number((day - DaysFromCivil(c.y, 1, 1) + 7 - weekday) / 7, 2, L'0'); break;
        case L'W': number((day - DaysFromCivil(c.y, 1, 1) + 7 - (weekday + 6) % 7) / 7, 2, L'0'); break;
        case L'V':
        case L'G':
        case L'g': {
            // ISO weeks: the week (and its year) of its Thursday.
            const long long thursday = day - (weekday + 6) % 7 + 3;
            const long long iso_year = CivilFromDays(thursday).y;
            if (f == L'V') number((thursday - DaysFromCivil(iso_year, 1, 1)) / 7 + 1, 2, L'0');
            else if (f == L'G') number(iso_year, 0, L'0');
            else number(((iso_year % 100) + 100) % 100, 2, L'0');
            break;
        }
        case L'c': out += FormatDate(t, L"%-m/%-d/%Y, %-I:%M:%S %p"); break;
        case L'x': out += FormatDate(t, L"%-m/%-d/%Y"); break;
        case L'X': out += FormatDate(t, L"%-I:%M:%S %p"); break;
        case L'Z': out += L"+0000"; break;
        case L'f': number(milli * 1000, 6, L'0'); break;
        case L'Q': number(static_cast<long long>(t), 0, L'0'); break;
        case L's': number(static_cast<long long>(std::floor(t / 1000)), 0, L'0'); break;
        case L'%': out.push_back(L'%'); break;
        default:
            out.push_back(L'%');
            out.push_back(f);
            break;
        }
    }
    // (Runs of spaces show as one.)
    std::wstring collapsed;
    for (wchar_t ch : out) {
        if (ch == L' ' && (collapsed.empty() || collapsed.back() == L' ')) continue;
        collapsed.push_back(ch);
    }
    while (!collapsed.empty() && collapsed.back() == L' ') collapsed.pop_back();
    return collapsed;
}

// ---------------------------------------------------------------------------
// Parser
// ---------------------------------------------------------------------------

struct Text {
    std::wstring raw;
    uint32_t pos = 0, len = 0;
};

struct Task {
    Text text;
    std::wstring id;
    bool done = false, active = false, crit = false, milestone = false, vert = false;
    std::wstring start_raw, end_raw;  // as written ("" start: after the one before)
    size_t line = 0;                  // for errors
    int section = -1;
    double start = 0, end = 0;
    double render_end = 0;            // the bar stops before excluded days at its end
    bool resolved = false;
};

class Parser {
public:
    explicit Parser(const Source& src) : src_(src), c_(src.text, src.body) {
        compact = EqualsNoCase(src.display_mode, L"compact");
    }

    void Parse() {
        c_.SkipStatementGap();
        if (!c_.EatKeyword(L"gantt")) c_.Fail(Tr(L"Expected \"gantt\"", L"Ожидалось «gantt»"));
        for (;;) {
            c_.SkipStatementGap();
            if (c_.AtEnd()) break;
            Statement();
            c_.SkipBlanks();
            if (!c_.AtStatementEnd()) c_.Fail(Tr(L"Unexpected text", L"Непонятный текст"));
        }
        // The days in excludes / includes, known only with the final dateFormat.
        for (const auto& [list, exclude] : days_) Days(list, exclude);
        std::sort(exclude_days_.begin(), exclude_days_.end());
        std::sort(include_days_.begin(), include_days_.end());
        Resolve();
    }

    Text title;
    std::wstring date_format = L"YYYY-MM-DD", axis_format, tick_interval;
    bool today_marker = true, compact = false, top_axis = false;
    int week_start = 0;  // the weekday weeks begin with, for the ticks
    std::vector<Text> sections;
    std::vector<Task> tasks;

    bool Excluded(long long day) const {
        if (std::binary_search(include_days_.begin(), include_days_.end(), day)) return false;
        const int w = Weekday(day);
        if (exclude_weekday_[static_cast<size_t>(w)]) return true;
        if (exclude_weekends_ && (w == weekend_start_ || w == (weekend_start_ + 1) % 7)) return true;
        return std::binary_search(exclude_days_.begin(), exclude_days_.end(), day);
    }
    bool AnyExcluded() const {
        return exclude_weekends_ || !exclude_days_.empty() ||
               std::find(exclude_weekday_.begin(), exclude_weekday_.end(), true) != exclude_weekday_.end();
    }

private:
    void Statement() {
        auto setting = [&](const wchar_t* kw, std::wstring* out) {
            if (!c_.EatKeyword(kw)) return false;
            c_.SkipBlanks();
            *out = std::wstring(c_.LineRest());
            return true;
        };
        if (c_.EatKeyword(L"title")) {
            c_.SkipBlanks();
            title = TextHere(c_.WholeLine());
            return;
        }
        std::wstring value;
        if (setting(L"dateFormat", &value)) {
            date_format = value;
            return;
        }
        if (setting(L"axisFormat", &axis_format)) return;
        if (setting(L"tickInterval", &tick_interval)) return;
        if (setting(L"excludes", &value)) {
            days_.push_back({value, true});
            return;
        }
        if (setting(L"includes", &value)) {
            days_.push_back({value, false});
            return;
        }
        if (setting(L"todayMarker", &value)) {
            today_marker = !EqualsNoCase(value, L"off");
            return;
        }
        if (setting(L"displayMode", &value)) {
            compact = EqualsNoCase(value, L"compact");
            return;
        }
        if (setting(L"weekend", &value)) {
            weekend_start_ = EqualsNoCase(value, L"friday") ? 5 : 6;
            return;
        }
        if (setting(L"weekday", &value)) {
            for (int w = 0; w < 7; ++w) {
                if (EqualsNoCase(value, kWeekdays[w])) week_start = w;
            }
            return;
        }
        if (c_.EatKeyword(L"inclusiveEndDates")) {
            inclusive_end_ = true;
            return;
        }
        if (c_.EatKeyword(L"topAxis")) {
            top_axis = true;
            return;
        }
        for (const wchar_t* kw : {L"click", L"accTitle"}) {
            if (setting(kw, &value)) return;
        }
        if (c_.EatKeyword(L"accDescr")) {
            c_.SkipBlanks();
            if (c_.Eat(L'{')) {
                while (!c_.AtEnd() && c_.Peek() != L'}') c_.Advance();
                c_.Eat(L'}');
            } else {
                c_.LineRest();
            }
            return;
        }
        if (c_.EatKeyword(L"section")) {
            c_.SkipBlanks();
            if (sections.size() >= kMaxNodes) c_.Fail(Tr(L"Too many sections", L"Слишком много разделов"));
            sections.push_back(TextHere(c_.WholeLine()));
            return;
        }
        // Task name : [tags,] [id,] [start,] end
        Task t;
        t.line = c_.pos();
        const size_t start = c_.pos();
        while (!c_.AtStatementEnd() && c_.Peek() != L':') c_.Advance();
        const std::wstring_view name = Trim(src_.text.substr(start, c_.pos() - start));
        if (name.empty() || !c_.Eat(L':')) c_.Fail(Tr(L"Expected a task (Name : data)", L"Ожидалась задача (Название : данные)"));
        t.text = TextHere(name);
        std::vector<std::wstring> data;
        const std::wstring_view rest = c_.LineRest();
        if (const size_t hash = rest.find(L'#'); hash != std::wstring_view::npos) {
            c_.set_pos(static_cast<size_t>(rest.data() - src_.text.data()) + hash);
            c_.Fail(Tr(L"Unexpected text", L"Непонятный текст"));
        }
        for (size_t s = 0; s <= rest.size();) {
            size_t e = rest.find(L',', s);
            if (e == std::wstring_view::npos) e = rest.size();
            data.emplace_back(Trim(rest.substr(s, e - s)));
            s = e + 1;
        }
        size_t tags = 0;
        for (; tags < data.size(); ++tags) {
            const std::wstring& d = data[tags];
            if (d == L"done") t.done = true;
            else if (d == L"active") t.active = true;
            else if (d == L"crit") t.crit = true;
            else if (d == L"milestone") t.milestone = true;
            else if (d == L"vert") t.vert = true;
            else break;
        }
        data.erase(data.begin(), data.begin() + static_cast<std::ptrdiff_t>(tags));
        if (data.size() == 1) {
            t.end_raw = data[0];
        } else if (data.size() == 2) {
            t.start_raw = data[0];
            t.end_raw = data[1];
        } else if (data.size() == 3) {
            t.id = data[0];
            t.start_raw = data[1];
            t.end_raw = data[2];
        } else {
            c_.Fail(Tr(L"Expected [id,] [start,] end", L"Ожидалось [id,] [начало,] конец"));
        }
        t.section = static_cast<int>(sections.size()) - 1;
        if (tasks.size() >= kMaxEdges) c_.Fail(Tr(L"Too many tasks", L"Слишком много задач"));
        tasks.push_back(std::move(t));
    }

    Text TextHere(std::wstring_view s) {
        if (s.size() > kMaxLabel) c_.Fail(Tr(L"Text too long", L"Слишком длинный текст"));
        return Text{std::wstring(s), static_cast<uint32_t>(s.data() - src_.text.data()), static_cast<uint32_t>(s.size())};
    }

    // excludes weekends, sunday, 2024-01-01 (in dateFormat)
    void Days(const std::wstring& list, bool exclude) {
        for (size_t s = 0; s <= list.size();) {
            size_t e = list.find_first_of(L", ", s);
            if (e == std::wstring::npos) e = list.size();
            const std::wstring_view item = Trim(std::wstring_view(list).substr(s, e - s));
            s = e + 1;
            if (item.empty()) continue;
            if (exclude && EqualsNoCase(item, L"weekends")) {
                exclude_weekends_ = true;
                continue;
            }
            bool named = false;
            for (int w = 0; w < 7; ++w) {
                if (EqualsNoCase(item, kWeekdays[w])) {
                    if (exclude) exclude_weekday_[static_cast<size_t>(w)] = true;
                    named = true;
                }
            }
            if (named) continue;
            if (const auto d = Date(std::wstring(item))) (exclude ? exclude_days_ : include_days_).push_back(DayOf(*d));
        }
    }

    std::optional<double> Date(const std::wstring& s) const {
        if (auto d = ParseDate(s, date_format)) return d;
        // (Mermaid falls back to JavaScript's Date: ISO dates and the like.)
        for (const wchar_t* f : {L"YYYY-M-D", L"YYYY-M-DTHH:mm:ss", L"YYYY-M-D HH:mm:ss", L"YYYY-M-DTHH:mm", L"YYYY-M-D HH:mm",
                                 L"YYYY-M-DTHH:mm:ss.SSS", L"YYYY", L"YYYY-M", L"YYYY/M/D", L"YYYY/M/D HH:mm", L"YYYY/M/D HH:mm:ss",
                                 L"M/D/YYYY", L"M/D/YYYY HH:mm", L"M.D.YYYY", L"M-D-YYYY", L"MMM D YYYY", L"MMM D, YYYY",
                                 L"D MMM YYYY", L"MMM D YYYY HH:mm", L"MMM D, YYYY HH:mm"}) {
            if (auto d = ParseDate(s, f, false)) return d;
        }
        return std::nullopt;
    }

    static double Today() { return FromDays(DayOf(LocalNow())); }

    // Starts and ends, as far as tasks refer to each other ("after", "until"
    // in any order). As in Mermaid: unknown tasks after "after" or "until"
    // mean today, an end neither a date nor a duration makes an empty task,
    // and excluded days lengthen tasks (those not ending at a YYYY-MM-DD).
    void Resolve() {
        std::map<std::wstring, size_t> ids;
        for (size_t k = 0; k < tasks.size(); ++k) {
            if (!tasks[k].id.empty()) ids[tasks[k].id] = k;  // (the last one, as in Mermaid)
        }
        auto refs = [&](std::wstring_view list) {
            std::vector<size_t> out;
            for (size_t s = 0; s <= list.size();) {
                size_t e = list.find(L' ', s);
                if (e == std::wstring_view::npos) e = list.size();
                const std::wstring id(Trim(list.substr(s, e - s)));
                s = e + 1;
                if (auto it = ids.find(id); it != ids.end()) out.push_back(it->second);
            }
            return out;
        };
        auto keyword = [](const std::wstring& s, std::wstring_view kw) {
            return s.size() > kw.size() && StartsWithNoCase(s, kw) && (s[kw.size()] == L' ' || s[kw.size()] == L'\t');
        };
        // The first task without a start: at the earliest date anywhere.
        std::optional<double> earliest;
        for (const Task& t : tasks) {
            if (auto d = Date(t.start_raw)) earliest = earliest ? std::min(*earliest, *d) : *d;
        }
        const double fallback = earliest ? *earliest : Today();
        const bool excludes = AnyExcluded();
        for (;;) {
            bool progress = false, missing = false;
            for (size_t k = 0; k < tasks.size(); ++k) {
                Task& t = tasks[k];
                if (t.resolved) continue;
                // Start.
                std::optional<double> start;
                if (t.start_raw.empty()) {
                    if (k == 0) start = fallback;
                    else if (tasks[k - 1].resolved) start = tasks[k - 1].end;
                } else if (keyword(t.start_raw, L"after")) {
                    bool ready = true;
                    std::optional<double> latest;
                    for (size_t a : refs(std::wstring_view(t.start_raw).substr(6))) {
                        if (!tasks[a].resolved) ready = false;
                        else latest = latest ? std::max(*latest, tasks[a].end) : tasks[a].end;
                    }
                    if (ready) start = latest ? *latest : Today();
                } else {
                    start = Date(t.start_raw);
                    if (!start) mermaid::Fail(t.line, Tr(L"Not a date in dateFormat: ", L"Дата не в формате dateFormat: ") + t.start_raw);
                }
                if (!start) {
                    missing = true;
                    continue;
                }
                // End.
                double end = *start;
                bool at_date = false;
                if (keyword(t.end_raw, L"until")) {
                    bool ready = true;
                    std::optional<double> first_start;
                    for (size_t u : refs(std::wstring_view(t.end_raw).substr(6))) {
                        if (!tasks[u].resolved) ready = false;
                        else first_start = first_start ? std::min(*first_start, tasks[u].start) : tasks[u].start;
                    }
                    if (!ready) {
                        missing = true;
                        continue;
                    }
                    end = first_start ? *first_start : Today();
                } else if (auto date = ParseDate(t.end_raw, date_format)) {
                    end = *date + (inclusive_end_ ? kDay : 0.0);
                    at_date = ParseDate(t.end_raw, L"YYYY-MM-DD").has_value();
                } else if (auto d = AddDuration(*start, t.end_raw)) {
                    end = *d;
                }
                if (!(std::abs(end) <= kMaxTime)) mermaid::Fail(t.line, Tr(L"The date is out of range", L"Дата вне допустимого диапазона"));
                // (An end before the start stays: the next task starts there.)
                t.start = *start;
                t.end = t.render_end = end;
                // Excluded days push the end further, counted from the day
                // after the start (Mermaid's checkTaskDates); the bar stops
                // before those at its end.
                if (excludes && !at_date) {
                    bool excluded = false;
                    int steps = 0;
                    for (double day = t.start + kDay; day <= t.end && steps < 20000; day += kDay, ++steps) {
                        if (!excluded) t.render_end = t.end;
                        excluded = Excluded(DayOf(day));
                        if (excluded) t.end += kDay;
                    }
                }
                t.resolved = true;
                progress = true;
            }
            if (!missing) break;
            if (!progress) {
                for (const Task& t : tasks) {
                    if (!t.resolved) mermaid::Fail(t.line, Tr(L"Tasks wait for each other in a circle", L"Задачи ждут друг друга по кругу"));
                }
            }
        }
    }

    const Source& src_;
    Cursor c_;
    std::vector<std::pair<std::wstring, bool>> days_;  // excludes (true) and includes lists
    bool inclusive_end_ = false;
    bool exclude_weekends_ = false;
    int weekend_start_ = 6;  // Saturday (weekend friday: Friday and Saturday)
    std::vector<bool> exclude_weekday_ = std::vector<bool>(7, false);
    std::vector<long long> exclude_days_, include_days_;
};

// ---------------------------------------------------------------------------
// The time axis
// ---------------------------------------------------------------------------

enum class Unit { Ms, Second, Minute, Hour, Day, Week, Month, Year };
struct Interval {
    Unit unit;
    long long n;
};

double UnitMs(Unit u) {
    switch (u) {
    case Unit::Ms: return 1;
    case Unit::Second: return 1000;
    case Unit::Minute: return 60000;
    case Unit::Hour: return 3600000;
    case Unit::Day: return kDay;
    case Unit::Week: return 7 * kDay;
    case Unit::Month: return 30 * kDay;
    case Unit::Year: return 365 * kDay;
    }
    return 1;
}

// d3's tickStep: 1, 2 or 5 times a power of ten.
double TickStep(double start, double stop, double count) {
    const double step0 = std::abs(stop - start) / count;
    double step1 = std::pow(10.0, std::floor(std::log10(step0)));
    const double error = step0 / step1;
    if (error >= std::sqrt(50.0)) step1 *= 10;
    else if (error >= std::sqrt(10.0)) step1 *= 5;
    else if (error >= std::sqrt(2.0)) step1 *= 2;
    return step1;
}

constexpr Interval kIntervals[] = {
    {Unit::Second, 1},  {Unit::Second, 5}, {Unit::Second, 15}, {Unit::Second, 30}, {Unit::Minute, 1}, {Unit::Minute, 5},
    {Unit::Minute, 15}, {Unit::Minute, 30}, {Unit::Hour, 1},   {Unit::Hour, 3},     {Unit::Hour, 6},   {Unit::Hour, 12},
    {Unit::Day, 1},     {Unit::Day, 2},    {Unit::Week, 1},    {Unit::Month, 1},    {Unit::Month, 3},  {Unit::Year, 1}};

// The next coarser interval than iv.
Interval Coarser(Interval iv) {
    auto ms = [](const Interval& i) { return UnitMs(i.unit) * static_cast<double>(i.n); };
    for (const Interval& i : kIntervals) {
        if (ms(i) > ms(iv)) return i;
    }
    // Years: 1, 2, 5, 10, 20, 50...
    long long n = std::max(1LL, iv.unit == Unit::Year ? iv.n : 1);
    long long p = 1;
    while (p * 10 <= n) p *= 10;
    const long long lead = n / p;
    return {Unit::Year, lead < 2 ? 2 * p : lead < 5 ? 5 * p : 10 * p};
}

// The interval d3 picks for about ten ticks.
Interval AutoInterval(double t0, double t1) {
    auto ms = [](const Interval& i) { return UnitMs(i.unit) * static_cast<double>(i.n); };
    const double target = (t1 - t0) / 10;
    size_t i = 0;
    while (i < std::size(kIntervals) && ms(kIntervals[i]) <= target) ++i;
    if (i == std::size(kIntervals)) {
        const double year = UnitMs(Unit::Year);
        return {Unit::Year, std::max(1LL, std::llround(TickStep(t0 / year, t1 / year, 10)))};
    }
    if (i == 0) return {Unit::Ms, std::max(1LL, std::llround(TickStep(t0, t1, 10)))};
    return target / ms(kIntervals[i - 1]) < ms(kIntervals[i]) / target ? kIntervals[i - 1] : kIntervals[i];
}

// tickInterval: 1day, 2week, 1month, 30minute...
std::optional<Interval> ParseInterval(const std::wstring& s) {
    size_t k = 0;
    while (k < s.size() && s[k] >= L'0' && s[k] <= L'9') ++k;
    if (k == 0 || k > 6 || s[0] == L'0') return std::nullopt;
    const long long n = std::stoll(s.substr(0, k));
    const std::wstring_view unit = std::wstring_view(s).substr(k);
    static const std::pair<const wchar_t*, Unit> kUnits[] = {
        {L"millisecond", Unit::Ms}, {L"second", Unit::Second}, {L"minute", Unit::Minute}, {L"hour", Unit::Hour},
        {L"day", Unit::Day},        {L"week", Unit::Week},     {L"month", Unit::Month},   {L"year", Unit::Year}};
    for (const auto& [name, u] : kUnits) {
        if (unit == name) return Interval{u, n};
    }
    return std::nullopt;
}

// Ticks on the boundaries of an interval as d3 counts them: seconds, minutes
// and hours whose number divides by n, days of the month (1st, 3rd...),
// months of the year, years; weeks from week_start.
std::vector<double> Ticks(double t0, double t1, Interval iv, int week_start) {
    constexpr size_t kMaxTicks = 200;
    std::vector<double> out;
    const double unit = UnitMs(iv.unit);
    if ((t1 - t0) / (unit * static_cast<double>(iv.n)) > kMaxTicks) return out;
    auto add = [&](double t) {
        if (t >= t0 && t <= t1 && out.size() < kMaxTicks) out.push_back(t);
    };
    switch (iv.unit) {
    case Unit::Ms: {
        const double step = static_cast<double>(iv.n);
        for (double t = std::ceil(t0 / step) * step; t <= t1 && out.size() < kMaxTicks; t += step) add(t);
        break;
    }
    case Unit::Second:
    case Unit::Minute:
    case Unit::Hour: {
        const double parent = iv.unit == Unit::Second ? 60000 : iv.unit == Unit::Minute ? 3600000 : kDay;
        const long long per = std::llround(parent / unit);
        for (double p = std::floor(t0 / parent) * parent; p <= t1 && out.size() < kMaxTicks; p += parent) {
            for (long long k = 0; k < per; k += iv.n) add(p + static_cast<double>(k) * unit);
        }
        break;
    }
    case Unit::Day:
        for (long long d = DayOf(t0); FromDays(d) <= t1 && out.size() < kMaxTicks; ++d) {
            if ((CivilFromDays(d).d - 1) % iv.n == 0) add(FromDays(d));
        }
        break;
    case Unit::Week: {
        long long d = DayOf(t0);
        d -= (Weekday(d) - week_start + 7) % 7;
        for (long long w = 0; FromDays(d) <= t1 && out.size() < kMaxTicks; d += 7, ++w) {
            if (w % iv.n == 0) add(FromDays(d));
        }
        break;
    }
    case Unit::Month: {
        Civil c = CivilFromDays(DayOf(t0));
        for (;;) {
            const double t = FromDays(DaysFromCivil(c.y, c.m, 1));
            if (t > t1 || out.size() >= kMaxTicks) break;
            if ((c.m - 1) % iv.n == 0) add(t);
            if (++c.m > 12) {
                c.m = 1;
                ++c.y;
            }
        }
        break;
    }
    case Unit::Year: {
        const long long first = CivilFromDays(DayOf(t0)).y;
        for (long long y = first - (first % iv.n + iv.n) % iv.n;; y += iv.n) {
            const double t = FromDays(DaysFromCivil(y, 1, 1));
            if (t > t1 || out.size() >= kMaxTicks) break;
            add(t);
        }
        break;
    }
    }
    if (out.size() >= kMaxTicks) out.clear();  // too many: none
    return out;
}

// ---------------------------------------------------------------------------
// Drawing, in Mermaid's default gantt colors
// ---------------------------------------------------------------------------

constexpr float kChartW = 680;           // the time axis: all about as wide as GitHub's column
constexpr float kBar = 20, kBarGap = 4;  // Mermaid's barHeight, barGap
constexpr float kMargin = 20;
constexpr float kSide = 75;              // Mermaid's leftPadding / rightPadding
constexpr float kScale = 0.75f;          // task text: 12px
constexpr float kAxisScale = 0.625f;     // axis labels: 10px, as Mermaid's

std::shared_ptr<DiagramScene> Build(const Parser& p, SceneBuilder& b, const Source& src) {
    using M = SceneBuilder::Measured;
    const bool own_title = !p.title.raw.empty();
    const std::wstring& title_text = own_title ? p.title.raw : src.title;
    M title;
    if (!title_text.empty()) title = b.Measure(ParseLabel(title_text, false), 0, false, 1.125f);
    double t0 = 1e300, t1 = -1e300;
    for (const Task& t : p.tasks) {
        t0 = std::min(t0, t.start);
        t1 = std::max(t1, t.vert ? t.start : t.render_end);
    }
    if (p.tasks.empty()) t0 = t1 = FromDays(DayOf(LocalNow()));
    if (t1 <= t0) t1 = t0 + kDay;
    // Rows: a task each, or in compact mode packed per section by start
    // (Mermaid's getMaxIntersections); vert markers take none.
    std::vector<int> row(p.tasks.size(), -1);
    int rows = 0;
    for (size_t k = 0; k < p.tasks.size();) {
        size_t e = k;
        std::vector<size_t> section;
        for (; e < p.tasks.size() && p.tasks[e].section == p.tasks[k].section; ++e) {
            if (!p.tasks[e].vert) section.push_back(e);
        }
        k = e;
        if (!p.compact) {
            for (size_t i : section) row[i] = rows++;
            continue;
        }
        std::stable_sort(section.begin(), section.end(), [&](size_t a, size_t b) { return p.tasks[a].start < p.tasks[b].start; });
        std::vector<double> ends;  // where each row is free from
        for (size_t i : section) {
            size_t r = 0;
            while (r < ends.size() && ends[r] > p.tasks[i].start) ++r;
            if (r == ends.size()) ends.push_back(0);
            ends[r] = p.tasks[i].end;
            row[i] = rows + static_cast<int>(r);
        }
        rows += static_cast<int>(ends.size());
    }
    // Section titles on the left, as wide as they need.
    std::vector<M> section_titles;
    float left = kSide;
    for (const Text& s : p.sections) {
        section_titles.push_back(b.Measure(ParseLabel(s.raw, false), 0, false, kScale, false));
        left = std::max(left, section_titles.back().w + 20);
    }
    const float x0 = kMargin + left, x1 = x0 + kChartW;
    auto X = [&](double t) { return x0 + static_cast<float>((t - t0) / (t1 - t0)) * kChartW; };
    const float axis_h = 24;
    const float chart_top = kMargin + (title.layout ? title.h + 12 : 0.f) + (p.top_axis ? axis_h : 0.f);
    const float row_h = kBar + kBarGap;
    // vert markers are named in a row of their own at the bottom.
    const bool verts = std::any_of(p.tasks.begin(), p.tasks.end(), [](const Task& t) { return t.vert; });
    const float chart_bottom = chart_top + static_cast<float>(std::max(rows, 1) + verts) * row_h + kBarGap;
    float width = x1 + kSide;
    const float height = chart_bottom + (p.top_axis ? 0.f : axis_h) + kMargin;
    // Section bands, in four alternating colors (Mermaid's section0..3, at
    // the 20% opacity its style gives them).
    static const uint32_t kBands[] = {0x196666FF, 0x00000000, 0x33FFF400, 0x00000000};
    for (size_t k = 0, band = 0; k < p.tasks.size(); ++band) {
        const int s = p.tasks[k].section;
        int r0 = rows, r1 = -1;
        for (; k < p.tasks.size() && p.tasks[k].section == s; ++k) {
            if (row[k] < 0) continue;
            r0 = std::min(r0, row[k]);
            r1 = std::max(r1, row[k]);
        }
        if (r1 < 0) continue;
        const float y0 = chart_top + static_cast<float>(r0) * row_h, y1 = chart_top + static_cast<float>(r1 + 1) * row_h + kBarGap / 2;
        if (kBands[band % 4] >> 24) b.Rect(D2D1::RectF(kMargin, y0, x1 + 10, y1), 0, Tint(kBands[band % 4]), Role(kDgNone));
        if (s >= 0) {
            const M& m = section_titles[static_cast<size_t>(s)];
            const Text& t = p.sections[static_cast<size_t>(s)];
            b.Text(m, D2D1::Point2F(kMargin + 6, (y0 + y1) / 2 - m.h / 2), Role(kDgText), t.pos, t.len);
        }
    }
    // Excluded days: grey columns.
    if (p.AnyExcluded() && t1 - t0 < 3650 * kDay) {
        for (long long d = DayOf(t0); FromDays(d) < t1; ++d) {
            if (!p.Excluded(d)) continue;
            const float a = std::max(X(FromDays(d)), x0), z = std::min(X(FromDays(d + 1)), x1);
            if (z > a) b.Rect(D2D1::RectF(a, chart_top, z, chart_bottom), 0, Tint(0xFFEEEEEE), Role(kDgNone));
        }
    }
    // The grid and the axis labels: tickInterval's ticks, or d3's (coarser
    // while their labels would run into each other).
    const auto set = ParseInterval(p.tick_interval);
    Interval iv = set ? *set : AutoInterval(t0, t1);
    std::vector<double> ticks = Ticks(t0, t1, iv, p.week_start);
    if (set && ticks.empty()) ticks = Ticks(t0, t1, iv = AutoInterval(t0, t1), p.week_start);  // (too many)
    std::vector<M> labels;
    auto axis_label = [&](double t) {
        // Mermaid's %Y-%m-%d, but times between days when ticks are hours apart.
        std::wstring_view format = p.axis_format;
        if (format.empty()) format = iv.unit < Unit::Day && std::fmod(t, kDay) != 0 ? L"%H:%M" : L"%Y-%m-%d";
        RichText text;
        text.text = FormatDate(t, format);
        return b.Measure(text, 0, false, kAxisScale);
    };
    for (int round = 0; round < 12; ++round) {
        labels.clear();
        bool fits = true;
        for (size_t i = 0; i < ticks.size(); ++i) {
            labels.push_back(axis_label(ticks[i]));
            if (i > 0 && X(ticks[i]) - X(ticks[i - 1]) < (labels[i].w + labels[i - 1].w) / 2 + 6) fits = false;
        }
        if (fits || set) break;
        // (Not so coarse that no two ticks are left.)
        const Interval coarser = Coarser(iv);
        std::vector<double> fewer = Ticks(t0, t1, coarser, p.week_start);
        if (fewer.size() < 2) break;
        iv = coarser;
        ticks = std::move(fewer);
    }
    const float axis_y = p.top_axis ? chart_top - axis_h + 6 : chart_bottom + 6;
    for (size_t i = 0; i < ticks.size(); ++i) {
        const float x = X(ticks[i]);
        b.Begin(D2D1::Point2F(x, chart_top));
        b.LineTo(D2D1::Point2F(x, chart_bottom));
        b.End(Role(kDgNone), Tint(0xCCD3D3D3), 1.f);
        b.Text(labels[i], D2D1::Point2F(x - labels[i].w / 2, axis_y), Role(kDgText));
    }
    // Tasks.
    for (size_t k = 0; k < p.tasks.size(); ++k) {
        const Task& t = p.tasks[k];
        if (t.vert) continue;
        const float y = chart_top + static_cast<float>(row[k]) * row_h + kBarGap;
        // Active before done; crit then only in the border.
        const uint32_t fill = t.active ? 0xFFBFC7FF : t.done ? 0xFFD3D3D3 : t.crit ? 0xFFFF0000 : 0xFF8A90DD;
        const uint32_t stroke = t.crit ? 0xFFFF8888 : t.done && !t.active ? 0xFF808080 : 0xFF534FBC;
        float a = X(t.start), z = X(t.render_end);
        if (t.milestone) {
            // A diamond at the middle of its time.
            const float cx = (a + z) / 2, cy = y + kBar / 2, r = kBar * 0.57f;
            const std::vector<D2D1_POINT_2F> diamond{{cx, cy - r}, {cx + r, cy}, {cx, cy + r}, {cx - r, cy}};
            b.Polygon(diamond, Role(kDgPage), Role(kDgNone));  // (opaque in the dark theme too)
            b.Polygon(diamond, Tint(fill), Argb(stroke), 2.f);
            a = cx - r;
            z = cx + r;
        } else {
            z = std::max(z, a + 1);
            b.Rect(D2D1::RectF(a, y, z, y + kBar), 3, Role(kDgPage), Role(kDgNone));  // (opaque in the dark theme too)
            b.Rect(D2D1::RectF(a, y, z, y + kBar), 3, Tint(fill), Argb(stroke), 2.f);
        }
        // The name inside (white on the plain bars), or else beside it:
        // right of it, left at the chart's end.
        RichText label = ParseLabel(t.text.raw, false);
        if (t.milestone) label.spans.push_back({0, static_cast<uint32_t>(label.text.size()), false, true});
        const M m = b.Measure(label, 0, false, kScale);
        const bool inside = !t.milestone && m.w + 8 <= z - a;
        float tx = inside ? (a + z) / 2 - m.w / 2 : z + 6;
        if (!inside && tx + m.w > x1 + kSide - 5 && a - 6 - m.w >= x0) tx = a - 6 - m.w;
        width = std::max(width, tx + m.w + kMargin);
        const DgColor color = inside && !t.active && !t.done ? Argb(0xFFFFFFFF) : Role(kDgText);
        b.Text(m, D2D1::Point2F(tx, y + (kBar - m.h) / 2), color, t.text.pos, t.text.len);
    }
    // vert markers: a dashed line across the chart at their date, named below.
    for (const Task& t : p.tasks) {
        if (!t.vert) continue;
        const float x = X(t.start), label_y = chart_bottom - row_h;
        b.Begin(D2D1::Point2F(x, chart_top));
        b.LineTo(D2D1::Point2F(x, label_y));
        b.End(Role(kDgNone), Role(kDgLine), 2.f, 1);
        const M m = b.Measure(ParseLabel(t.text.raw, false), 0, false, kScale);
        // Centered under the line, kept inside the chart; a label wider than
        // the chart starts at its left edge and widens the diagram.
        const float tx = std::max(kMargin, std::min(x - m.w / 2, x1 + kSide - m.w));
        width = std::max(width, tx + m.w + kMargin);
        b.Text(m, D2D1::Point2F(tx, label_y + (row_h - m.h) / 2), Role(kDgText), t.text.pos, t.text.len);
    }
    // Today, if in the chart.
    if (p.today_marker) {
        const double now = LocalNow();
        if (now >= t0 && now <= t1) {
            const float x = X(now);
            b.Begin(D2D1::Point2F(x, chart_top));
            b.LineTo(D2D1::Point2F(x, chart_bottom));
            b.End(Role(kDgNone), Argb(0xFFFF0000), 2.f);
        }
    }
    width = std::max(width, title.w + 2 * kMargin);
    if (title.layout) {
        b.Text(title, D2D1::Point2F((width - title.w) / 2, kMargin), Role(kDgText), own_title ? p.title.pos : src.title_pos,
               own_title ? p.title.len : src.title_len);
    }
    return b.Finish(width, height);
}

}  // namespace

std::shared_ptr<DiagramScene> Gantt(const Source& src, SceneBuilder* builder) {
    Parser p(src);
    p.Parse();
    if (!builder) return nullptr;
    return Build(p, *builder, src);
}

}  // namespace mdv::mermaid
