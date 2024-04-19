// Copyright (c) Microsoft Corporation.
// Licensed under the MIT license.

#pragma once
#include <string>
#include <algorithm>
#include <cctype>
#include <type_traits>

#undef min //workaround windows.h problems

template<typename T>
inline T SkipWhitespace(T i, const T &e)
{
    while (i != e)
    {
        if (*i == ' ' || *i == '\t')
            ++i;
        else
            break;
    }

    return i;
}

template<typename T>
inline T SkipWhitespaceOrLineBreak(T i, const T &e)
{
    while (i != e)
    {
        if (*i == ' ' || *i == '\t' || *i == '\r' || *i == '\n')
            ++i;
        else
            break;
    }

    return i;
}

inline std::string TrimString(const std::string &s)
{
    if (s.empty())
        return std::string();

    auto start = SkipWhitespace(s.begin(), s.end());
    auto end = SkipWhitespace(s.rbegin(), s.rend());

    if (start >= s.end())
        return std::string();

    return std::string(&*start, &*end + 1);
}

inline std::string TrimStringOrLineBreak(const std::string &s)
{
    if (s.empty())
        return std::string();

    auto start = SkipWhitespaceOrLineBreak(s.begin(), s.end());
    auto end = SkipWhitespaceOrLineBreak(s.rbegin(), s.rend());

    if (start >= s.end())
        return std::string();

    return std::string(&*start, &*end + 1);
}

inline bool IsAlpha(char c)
{
    return (c >= 'a' && c <= 'z') || c >= 'A' && c <= 'Z';
}

inline void StripSymbolPrefixFromString(std::string &s)
{
    if (s.empty())
        return;

    size_t realStart = 0;
    for (size_t i = 0; i < s.size(); ++i)
    {
        char c = s[i];
        if (!IsAlpha(c))
            realStart = i + 1;
        else
            break;
    }

    if (realStart == 0)
        return;

    s = std::string(s.begin() + realStart, s.end());
}

//allows any element type
template<typename TJoiner, typename TIter, typename TStringer>
inline std::string StringJoin(TJoiner joiner, TIter first, TIter last, TStringer &&stringer)
{
    std::string final;
    TIter cur = first;
    while (cur != last)
    {
        if (cur != first)
            final += joiner;
        final += stringer(*cur);

        ++cur;
    }

    return final;
}

//expects elements to be strings
template<typename TJoiner, typename TIter>
inline std::string StringJoin(TJoiner joiner, TIter first, TIter last)
{
    size_t requiredChars = 0;
    for (TIter cur = first; cur != last; ++cur)
    {
        if (cur != first)
            ++requiredChars;
        requiredChars += cur->size();
    }

    std::string final;
    final.reserve(requiredChars);
    TIter cur = first;
    while (cur != last)
    {
        if (cur != first)
            final += joiner;
        final += *cur;

        ++cur;
    }

    return final;
}

inline std::vector<std::string> StringSplit(char joiner, const std::string &str)
{
    std::vector<std::string> pieces;

    auto cur = str.begin();
    auto blockStart = cur;
    while (cur != str.end())
    {
        if (*cur == joiner)
        {
            pieces.emplace_back(blockStart, cur);
            blockStart = cur + 1;
        }

        ++cur;
    }

    pieces.emplace_back(blockStart, cur);

    return std::move(pieces);
}

inline std::vector<std::string> StringSplit(std::initializer_list<char> joiners, const std::string &str)
{
    std::vector<std::string> pieces;

    auto cur = str.begin();
    auto blockStart = cur;
    while (cur != str.end())
    {
        if (std::find(joiners.begin(), joiners.end(), *cur) != joiners.end())
        {
            pieces.emplace_back(blockStart, cur);
            blockStart = cur + 1;
        }

        ++cur;
    }

    pieces.emplace_back(blockStart, cur);

    return std::move(pieces);
}

inline bool StartsWith(const std::string &haystack, const std::string &needle)
{
    if (needle.size() > haystack.size())
        return false;

    for (size_t i = 0; i < needle.size(); ++i)
    {
        if (haystack[i] != needle[i])
            return false;
    }

    return true;
}

inline bool EndsWith(const std::string &haystack, const std::string &needle)
{
    if (needle.size() > haystack.size())
        return false;

    for (size_t i = needle.size() - 1; i != (size_t)0 - 1; --i)
    {
        if (haystack[haystack.size() - needle.size() + i] != needle[i])
            return false;
    }

    return true;
}

inline void AppendFixedDigitInteger(std::string &target, int val, int digits)
{
    char temp[32];
    temp[31] = 0;
    if (!_ltoa_s(val, temp, 10))
    {
        int actualLen = (int)strlen(temp);
        for (int p = 0; p < digits - actualLen; ++p)
            target.push_back('0');
        target.append(temp);
    }
}

inline std::string BuildSortableDate(int year, int month, int day, int hour, int minute, int second, int millisecond)
{
    std::string outDate;
    outDate.reserve(23);

    AppendFixedDigitInteger(outDate, year, 4);
    outDate.push_back('-');
    AppendFixedDigitInteger(outDate, month, 2);
    outDate.push_back('-');
    AppendFixedDigitInteger(outDate, day, 2);
    outDate.push_back(' ');
    AppendFixedDigitInteger(outDate, hour, 2);
    outDate.push_back(':');
    AppendFixedDigitInteger(outDate, minute, 2);
    outDate.push_back(':');
    AppendFixedDigitInteger(outDate, second, 2);
    outDate.push_back('.');
    AppendFixedDigitInteger(outDate, millisecond, 3);

    return std::move(outDate);
}

inline void AppendHexDigits(std::string &target, uint8_t val)
{
    int v0 = val >> 4;
    int v1 = val & 0x0f;

    if (v0 < 10)
        target.push_back((char)('0' + v0));
    else
        target.push_back((char)('A' + v0 - 10));

    if (v1 < 10)
        target.push_back((char)('0' + v1));
    else
        target.push_back((char)('A' + v1 - 10));
}

inline void AppendHexDigits(std::string &target, uint32_t val)
{
    AppendHexDigits(target, *((uint8_t*)&val + 3));
    AppendHexDigits(target, *((uint8_t*)&val + 2));
    AppendHexDigits(target, *((uint8_t*)&val + 1));
    AppendHexDigits(target, *((uint8_t*)&val + 0));
}

inline void AppendHexDigits(std::string &target, uint64_t val)
{
    AppendHexDigits(target, *((uint8_t*)&val + 7));
    AppendHexDigits(target, *((uint8_t*)&val + 6));
    AppendHexDigits(target, *((uint8_t*)&val + 5));
    AppendHexDigits(target, *((uint8_t*)&val + 4));
    AppendHexDigits(target, *((uint8_t*)&val + 3));
    AppendHexDigits(target, *((uint8_t*)&val + 2));
    AppendHexDigits(target, *((uint8_t*)&val + 1));
    AppendHexDigits(target, *((uint8_t*)&val + 0));
}

inline std::string BuildGuid(const uint8_t *guidBytes)
{
    std::string outGuid;

    size_t cur = 0;
    for (int i = 4 - 1; i >= 0; --i)
        AppendHexDigits(outGuid, guidBytes[cur + i]);
    cur += 4;

    for (int r = 0; r < 2; ++r)
    {
        outGuid.push_back('-');
        for (int i = 2 - 1; i >= 0; --i)
            AppendHexDigits(outGuid, guidBytes[cur + i]);
        cur += 2;
    }

    outGuid.push_back('-');
    for (int i = 0; i < 2; ++i)
        AppendHexDigits(outGuid, guidBytes[cur + i]);
    cur += 2;

    outGuid.push_back('-');
    for (int i = 0; i < 6; ++i)
        AppendHexDigits(outGuid, guidBytes[cur + i]);

    return std::move(outGuid);
}

//class with similarities to std::string, but represents a fixed substring of a string whose memory is externally managed
//TODO: Now that string_view exists, we should consider deleting this class altogether.
template <typename TChar>
class ExternalSubstring
{
public:
    ExternalSubstring() : pBegin(nullptr), pEnd(nullptr)
    {
    }

    ExternalSubstring(TChar *iterBegin, TChar *iterEnd) : pBegin(iterBegin), pEnd(iterEnd)
    {
    }

    template <typename TStrIter>
    ExternalSubstring(TStrIter iterBegin, TStrIter iterEnd)
    {
        if (iterBegin == iterEnd)
        {
            pBegin = nullptr;
            pEnd = nullptr;
        }
        else
        {
            pBegin = &*iterBegin;
            pEnd = pBegin + (iterEnd - iterBegin);
        }
    }

    inline TChar* begin()
    {
        return pBegin;
    }

    inline const TChar* begin() const
    {
        return pBegin;
    }

    inline TChar* end()
    {
        return pEnd;
    }

    inline const TChar* end() const
    {
        return pEnd;
    }

    inline size_t size() const
    {
        return pEnd - pBegin;
    }

    inline bool empty() const
    {
        return pBegin == pEnd;
    }

    size_t find(TChar *iterStart, TChar *iterEnd, size_t startPos = 0) const
    {
        TChar *searchCur = pBegin + startPos;
        while (searchCur < pEnd)
        {
            TChar *searchCurInner = searchCur;
            TChar *subCur = iterStart;
            while (subCur < iterEnd)
            {
                if (*searchCurInner != *subCur)
                    break;

                ++subCur;
                ++searchCurInner;
            }

            if (subCur == iterEnd)
                return searchCur - pBegin;

            ++searchCur;
        }

        return std::string::npos;
    }

    size_t find_insensitive(TChar *iterStart, TChar *iterEnd, size_t startPos = 0) const
    {
        TChar *searchCur = pBegin + startPos;
        while (searchCur < pEnd)
        {
            TChar *searchCurInner = searchCur;
            TChar *subCur = iterStart;
            while (subCur < iterEnd)
            {
                typename std::remove_const<TChar>::type c1 = *searchCurInner;
                if (c1 >= 'A' && c1 <= 'Z')
                    c1 = (c1 - 'A') + 'a';

                typename std::remove_const<TChar>::type c2 = *subCur;
                if (c2 >= 'A' && c2 <= 'Z')
                    c2 = (c2 - 'A') + 'a';

                if (c1 != c2)
                    break;

                ++subCur;
                ++searchCurInner;
            }

            if (subCur == iterEnd)
                return searchCur - pBegin;

            ++searchCur;
        }

        return std::string::npos;
    }

    template <typename TStr>
    inline size_t find(TStr &str, size_t startPos = 0) const
    {
        if (str.empty())
            return std::string::npos;

        return find(str.data(), str.data() + str.size(), startPos);
    }

    template <typename TStr>
    inline size_t find_insensitive(TStr &str, size_t startPos = 0) const
    {
        if (str.empty())
            return std::string::npos;

        return find_insensitive(str.data(), str.data() + str.size(), startPos);
    }

    inline size_t find(TChar c, size_t startPos = 0) const
    {
        TChar *pos = begin() + startPos;
        while (pos < end())
        {
            if (*pos == c)
                return pos - begin();

            ++pos;
        }

        return std::string::npos;
    }

    inline std::string str() const
    {
        return std::string(pBegin, pEnd);
    }

    template <typename StrType>
    bool operator==(const StrType &o) const
    {
        if (size() != o.size())
            return false;

        auto me = begin();
        auto them = o.begin();

        while (me != end())
        {
            if (*me != *them)
                return false;

            ++me;
            ++them;
        }

        return true;
    }

    template <typename StrType>
    bool CaseInsensitiveCompare(const StrType &o) const
    {
        if (size() != o.size())
            return false;

        auto me = begin();
        auto them = o.begin();

        while (me != end())
        {
            auto meChar = *me;
            if (meChar >= 'A' && meChar <= 'Z')
                meChar = (meChar - 'A') + 'a';

            auto themChar = *them;
            if (themChar >= 'A' && themChar <= 'Z')
                themChar = (themChar - 'A') + 'a';

            if (meChar != themChar)
                return false;

            ++me;
            ++them;
        }

        return true;
    }

    template <typename StrType>
    bool CaseInsensitiveLessThan(const StrType &o) const
    {
        auto me = begin();
        auto them = o.begin();

        while (me != end() && them != o.end())
        {
            auto meChar = *me;
            if (meChar >= 'A' && meChar <= 'Z')
                meChar = (meChar - 'A') + 'a';

            auto themChar = *them;
            if (themChar >= 'A' && themChar <= 'Z')
                themChar = (themChar - 'A') + 'a';

            if (meChar < themChar)
                return true;
            else if (meChar > themChar)
                return false;

            ++me;
            ++them;
        }

        if (me == end() && them != o.end())
            return true;

        return false;
    }

    template <typename StrType>
    bool operator<(const StrType &o) const
    {
        size_t minSize = std::min(size(), o.size());

        for (size_t i = 0; i < minSize; ++i)
        {
            auto l = *(begin() + i);
            auto r = *(o.begin() + i);

            if (l < r)
                return true;
            else if (l != r)
                return false;
        }

        if (size() < o.size())
            return true;

        return false;
    }

    template <typename StrType>
    bool operator>(const StrType &o) const
    {
        size_t minSize = std::min(size(), o.size());

        for (size_t i = 0; i < minSize; ++i)
        {
            auto l = *(begin() + i);
            auto r = *(o.begin() + i);

            if (l > r)
                return true;
            else if (l != r)
                return false;
        }

        if (size() > o.size())
            return true;

        return false;
    }

private:
    TChar *pBegin, *pEnd;
};

inline bool CaseInsensitiveCompare(const std::string &a, const std::string &b)
{
    return ExternalSubstring<const char>(a.begin(), a.end()).CaseInsensitiveCompare(b);
}

inline bool CaseInsensitiveLessThan(const std::string &a, const std::string &b)
{
    return ExternalSubstring<const char>(a.begin(), a.end()).CaseInsensitiveLessThan(b);
}

inline void TransformStringToLower(std::string &s)
{
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return (unsigned char)std::tolower(c); });
}

inline std::string StringToLower(const std::string &s)
{
    std::string n = s;
    TransformStringToLower(n);
    return std::move(n);
}

struct CaseInsensitiveStringComparer
{
    bool operator()(const std::string &a, const std::string &b) const
    {
        return CaseInsensitiveLessThan(a, b);
    }
};

inline std::string TruncateWideString(const std::wstring &orig)
{
    std::string result;
    result.resize(orig.size());
    std::transform(orig.cbegin(), orig.cend(), result.begin(), [](wchar_t wc) { return (char)wc; });
    return result;
}
