#include "XmlLexicon.h"
#include "StringUtils.h"
#include <sstream>

//#define DUMP_PERF
#ifdef DUMP_PERF
#include <chrono>
#include "GuiStatusMonitor.h"
extern GuiStatusMonitor globalStatus;
#endif

namespace
{
    class AutoSortProperties
    {
    public:
        AutoSortProperties(std::vector<std::pair<std::string, std::string>> &p) : properties(p)
        {
        }

        ~AutoSortProperties()
        {
            std::sort(properties.begin(), properties.end());
        }

    private:
        std::vector<std::pair<std::string, std::string>> &properties;
    };

    std::string::iterator FindNextTag(const std::string::iterator &start, const std::string::iterator &end)
    {
        std::string::iterator cur = start;

        bool isInNormalComment = false;
        bool isInQuestionComment = false;
        while (cur < end)
        {
            if (isInNormalComment)
            {
                if (*cur == '-' && cur + 2 < end && *(cur + 1) == '-' && *(cur + 2) == '>')
                {
                    cur += 2;
                    isInNormalComment = false;
                }
            }
            else if (isInQuestionComment)
            {
                if (*cur == '?' && cur + 1 < end && *(cur + 1) == '>')
                {
                    ++cur;
                    isInQuestionComment = false;
                }
            }
            else if (*cur == '<')
            {
                if (cur + 3 < end && *(cur + 1) == '!' && *(cur + 2) == '-' && *(cur + 3) == '-')
                {
                    isInNormalComment = true;
                }
                else if (cur + 1 < end && *(cur + 1) == '?')
                    isInQuestionComment = true;
                else
                    break;
            }

            ++cur;
        }

        return cur;
    }

    std::string::iterator PopulateNode(XmlLexicon::Node &node, const std::string::iterator &allStart, const std::string::iterator &allEnd)
    {
        std::string::iterator openStart = FindNextTag(allStart, allEnd);
        if (openStart == allEnd)
            return allEnd;

        //name
        std::string::iterator nameStart = openStart + 1;
        nameStart = SkipWhitespaceOrLineBreak(nameStart, allEnd);
        std::string::iterator nameEnd = nameStart;
        while (nameEnd < allEnd)
        {
            if (*nameEnd == ' ' || *nameEnd == '\t' || *nameEnd == '\r' || *nameEnd == '\n' || *nameEnd == '/' || *nameEnd == '>')
                break;
            ++nameEnd;
        }
        if (nameEnd >= allEnd)
            return allEnd;

        node.Name = std::string(nameStart, nameEnd);

        //properties
        std::string::iterator propertyNameStart = SkipWhitespaceOrLineBreak(nameEnd, allEnd);
        std::string::iterator tagClose = allEnd;

        {
            AutoSortProperties autoProperties(node.PropertyList);
            while (propertyNameStart < allEnd)
            {
                std::string::iterator propertyNameEnd = propertyNameStart;
                while (propertyNameEnd < allEnd)
                {
                    if (*propertyNameEnd == ' ' || *propertyNameEnd == '\t' || *propertyNameEnd == '\r' || *propertyNameEnd == '\n' || *propertyNameEnd == '/' || *propertyNameEnd == '>' || *propertyNameEnd == '=')
                        break;
                    ++propertyNameEnd;
                }
                if (propertyNameEnd >= allEnd)
                    return allEnd;

                if (TrimString(std::string(propertyNameStart, propertyNameEnd)).empty())
                {
                    tagClose = SkipWhitespaceOrLineBreak(propertyNameStart, allEnd);
                    break;
                }

                std::string::iterator valueStart = SkipWhitespaceOrLineBreak(propertyNameEnd, allEnd);
                if (valueStart >= allEnd)
                    return allEnd;
                if (*valueStart == '=')
                {
                    valueStart = SkipWhitespaceOrLineBreak(valueStart + 1, allEnd);
                    if (valueStart >= allEnd)
                        return allEnd;

                    char valueTermChar = *valueStart;
                    std::string::iterator valueEnd;
                    if (valueTermChar == '\"' || valueTermChar == '\'')
                    {
                        ++valueStart;
                        valueEnd = valueStart;

                        while (valueEnd < allEnd && *valueEnd != valueTermChar)
                            ++valueEnd;
                    }
                    else
                    {
                        valueEnd = valueStart;

                        while (valueEnd < allEnd)
                        {
                            if (*valueEnd == ' ' || *valueEnd == '\t' || *valueEnd == '\r' || *valueEnd == '\n' || *valueEnd == '/' || *valueEnd == '>')
                                break;

                            ++valueEnd;
                        }
                    }
                    if (valueEnd >= allEnd)
                        return allEnd;

                    node.PropertyList.emplace_back(std::string(propertyNameStart, propertyNameEnd), std::string(valueStart, valueEnd));
                    propertyNameStart = SkipWhitespaceOrLineBreak(valueEnd + 1, allEnd);
                    tagClose = propertyNameStart;
                }
                else
                {
                    node.PropertyList.emplace_back(std::string(propertyNameStart, propertyNameEnd), std::string());
                    propertyNameStart = valueStart;
                    tagClose = valueStart;
                }

                if (tagClose == allEnd)
                    return allEnd;
                if (*tagClose == '/' || *tagClose == '>')
                    break;
            }
        }

        //tag that closes itself
        if (tagClose == allEnd)
            return allEnd;
        else if (*tagClose == '/')
        {
            tagClose = SkipWhitespaceOrLineBreak(tagClose + 1, allEnd);
            if (tagClose + 1 < allEnd)
                return tagClose + 1;
            return allEnd;
        }

        //children or contents or closing tag
        std::string::iterator childStart = FindNextTag(tagClose + 1, allEnd);
        while (childStart < allEnd)
        {
            std::string::iterator childNameStart = SkipWhitespaceOrLineBreak(childStart + 1, allEnd);
            std::string::iterator childNameEnd = childNameStart;
            while (childNameEnd < allEnd)
            {
                if (*childNameEnd == ' ' || *childNameEnd == '\t' || *childNameEnd == '\r' || *childNameEnd == '\n' || *childNameEnd == '/' || *childNameEnd == '>')
                    break;
                ++childNameEnd;
            }
            if (childNameEnd == allEnd)
                return allEnd;

            if (*childNameStart == '/') //closing tag
            {
                if (node.Children.empty()) //string contents should be stored
                    node.Value = TrimString(std::string(tagClose + 1, childStart));

                std::string::iterator selfEnd = childNameEnd;
                while (selfEnd < allEnd && *selfEnd != '>')
                    ++selfEnd;
                if (selfEnd + 1 <= allEnd)
                    return SkipWhitespaceOrLineBreak(selfEnd + 1, allEnd);
                return selfEnd;
            }

            //tag is a child
            node.Children.emplace_back();
            std::string::iterator childEnd = PopulateNode(node.Children.back(), childStart, allEnd);
            childStart = FindNextTag(childEnd, allEnd);
        }

        return allEnd;
    }
}

XmlLexicon::XmlLexicon(std::istream &stream)
{
#ifdef DUMP_PERF
    std::chrono::high_resolution_clock::time_point timerStart = std::chrono::high_resolution_clock::now();
#endif

    std::streampos startPos = stream.tellg();
    stream.seekg(0, std::ios::end);
    std::streampos endPos = stream.tellg();
    stream.seekg(startPos, std::ios::beg);

    std::string s;
    s.reserve(endPos - startPos);
    s.assign(std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>());

#ifdef DUMP_PERF
    std::chrono::high_resolution_clock::time_point timerAfterRead = std::chrono::high_resolution_clock::now();
#endif

    std::string::iterator next = s.begin();
    while (next < s.end())
    {
        roots.emplace_back();
        next = PopulateNode(roots.back(), next, s.end());
    }

    if (!roots.empty() && roots.back().Name.empty())
        roots.pop_back();

#ifdef DUMP_PERF
    std::chrono::high_resolution_clock::time_point timerAfterParse = std::chrono::high_resolution_clock::now();
    std::stringstream ss;
    ss << "XmlLexicon: Read time=" << std::chrono::duration_cast<std::chrono::milliseconds>(timerAfterRead - timerStart).count() << "ms   Parse time=" << std::chrono::duration_cast<std::chrono::milliseconds>(timerAfterParse - timerAfterRead).count() << std::endl;
    globalStatus.AddDebugOutput(ss.str());
#endif
}

const std::vector<XmlLexicon::Node>& XmlLexicon::GetRoots() const
{
    return roots;
}
