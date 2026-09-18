#include "FilamentSort.hpp"
#include "Utils.hpp"

#include <boost/locale.hpp>
#include <cctype>
#include <cstdint>
#include <map>
#include <vector>

namespace Slic3r {

namespace {

enum NameCategory : char {
    CAT_DIGIT = 0,
    CAT_ASCII = 1,
    CAT_CJK   = 2,
    CAT_OTHER = 3,
    CAT_EMPTY = 4,
};

NameCategory category_of(uint32_t cp)
{
    if (cp >= '0' && cp <= '9')
        return CAT_DIGIT;
    if ((cp >= 'A' && cp <= 'Z') || (cp >= 'a' && cp <= 'z'))
        return CAT_ASCII;
    if ((cp >= 0x4E00 && cp <= 0x9FFF) || (cp >= 0x3400 && cp <= 0x4DBF))
        return CAT_CJK;
    return CAT_OTHER;
}

void append_cp(std::string& out, uint32_t cp)
{
    out.push_back((char) (cp >> 16));
    out.push_back((char) (cp >> 8));
    out.push_back((char) cp);
}

void decode_utf8(const std::string& s, std::vector<uint32_t>& out)
{
    out.reserve(s.size());
    for (size_t i = 0; i < s.size();) {
        unsigned char c = (unsigned char) s[i];
        uint32_t      cp;
        size_t        len;
        if (c < 0x80) {
            cp  = c;
            len = 1;
        } else if ((c & 0xE0) == 0xC0) {
            cp  = c & 0x1F;
            len = 2;
        } else if ((c & 0xF0) == 0xE0) {
            cp  = c & 0x0F;
            len = 3;
        } else if ((c & 0xF8) == 0xF0) {
            cp  = c & 0x07;
            len = 4;
        } else {
            cp  = 0xFFFD;
            len = 1;
        }
        for (size_t k = 1; k < len && i + k < s.size(); ++k) {
            unsigned char cc = (unsigned char) s[i + k];
            if ((cc & 0xC0) != 0x80) {
                cp  = 0xFFFD;
                len = k;
                break;
            }
            cp = (cp << 6) | (cc & 0x3F);
        }
        out.push_back(cp > 0x10FFFF ? 0xFFFD : cp);
        i += len;
    }
}

// First-batch pinyin keys for filament vocabulary (versioned by git history;
// characters outside the table fall back to code point order after all
// table-covered characters).
const std::map<uint32_t, std::string>& pinyin_table()
{
    static const std::map<uint32_t, std::string> table = {
        {0x805A, "ju"},  {0x4E73, "ru"},  {0x9178, "suan"}, {0x6811, "shu"},  {0x8102, "zhi"},   {0x78B3, "tan"},   {0x7EA4, "xian"},
        {0x7EF4, "wei"}, {0x73BB, "bo"},  {0x7483, "li"},   {0x67D4, "rou"},  {0x8F6F, "ruan"},  {0x786C, "ying"},  {0x9AD8, "gao"},
        {0x6E29, "wen"}, {0x4F4E, "di"},  {0x97E7, "ren"},  {0x6027, "xing"}, {0x5149, "guang"}, {0x4EAE, "liang"}, {0x54D1, "ya"},
        {0x9ED1, "hei"}, {0x767D, "bai"}, {0x7070, "hui"},  {0x7EA2, "hong"}, {0x9EC4, "huang"}, {0x84DD, "lan"},   {0x7EFF, "lv"},
        {0x91D1, "jin"}, {0x94F6, "yin"}, {0x94DC, "tong"}, {0x900F, "tou"},  {0x660E, "ming"},  {0x8367, "ying2"},
    };
    return table;
}

std::string sort_key(const std::string& nfc)
{
    std::vector<uint32_t> cps;
    decode_utf8(nfc, cps);
    if (cps.empty())
        return std::string(1, CAT_EMPTY);

    std::string key(1, category_of(cps.front()));
    size_t      i = 0;
    while (i < cps.size()) {
        uint32_t cp = cps[i];
        if (cp >= '0' && cp <= '9') {
            size_t j = i;
            while (j < cps.size() && cps[j] >= '0' && cps[j] <= '9')
                ++j;
            size_t first_nonzero = i;
            while (first_nonzero + 1 < j && cps[first_nonzero] == '0')
                ++first_nonzero;
            size_t significant = j - first_nonzero;
            size_t leading0    = first_nonzero - i;
            key.push_back('\x01');
            key.push_back((char) (significant >> 8));
            key.push_back((char) significant);
            for (size_t k = first_nonzero; k < j; ++k)
                key.push_back((char) cps[k]);
            key.push_back((char) leading0);
            i = j;
        } else if (cp < 128) {
            key.push_back((char) std::tolower((int) cp));
            ++i;
        } else if (category_of(cp) == CAT_CJK) {
            auto it = pinyin_table().find(cp);
            if (it != pinyin_table().end()) {
                key.push_back('\x02');
                key += it->second;
            } else {
                key.push_back('\x03');
                append_cp(key, cp);
            }
            ++i;
        } else {
            key.push_back('\x04');
            append_cp(key, cp);
            ++i;
        }
    }
    return key;
}

} // namespace

int compare_filament_name(const std::string& a, const std::string& b)
{
    const std::string nfc_a = normalize_utf8_nfc(a.c_str());
    const std::string nfc_b = normalize_utf8_nfc(b.c_str());
    const std::string key_a = sort_key(nfc_a);
    const std::string key_b = sort_key(nfc_b);
    if (key_a != key_b)
        return key_a < key_b ? -1 : 1;
    return 0;
}

bool filament_name_less(const std::string& a, const std::string& b) { return compare_filament_name(a, b) < 0; }

} // namespace Slic3r
