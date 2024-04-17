#include "IniLexicon.h"
#include "StringUtils.h"

namespace
{
    const std::vector<std::pair<std::string, std::string>> emptyValueSet;
    const std::string emptyString;
}

IniLexicon::IniLexicon(std::istream &stream)
{
    std::string currentSection;
    std::string line;
    while (!stream.eof() && std::getline(stream, line) && !stream.fail())
    {
        if (line.empty() || line[0] == ';')
            continue;

        std::string::iterator i = SkipWhitespace(line.begin(), line.end());
        if (i == line.end())
            continue;

        //check for section change
        if (*i == '[')
        {
            std::string::iterator e = std::find(i, line.end(), ']');
            if (e == line.end()) //opening [ without a closing ]
                continue;

            currentSection = TrimString(std::string(i + 1, e));
            if (sectionValues.find(currentSection) == sectionValues.end())
            {
                if (std::find(sectionNames.begin(), sectionNames.end(), currentSection) == sectionNames.end())
                    sectionNames.push_back(currentSection);
                sectionValues.insert(std::make_pair(currentSection, std::vector<std::pair<std::string, std::string>>()));
            }

            continue;
        }

        //check for value declaration
        std::string::iterator equalsIter = std::find(i, line.end() - 1, '=');
        if (equalsIter == line.end()) //line is just invalid
            continue;

        std::string k = TrimString(std::string(i, equalsIter));
        std::string v = TrimString(std::string(equalsIter + 1, line.end()));
        if (!k.empty())
            sectionValues[currentSection].emplace_back(k, v);
    }
}

void IniLexicon::Save(std::ostream &stream) const
{
    for (auto &sectionName : sectionNames)
    {
        stream << "[" << sectionName << "]\n";

        auto svi = sectionValues.find(sectionName);
        if (svi != sectionValues.end())
        {
            for (auto &sv : svi->second)
                stream << sv.first << "=" << sv.second << "\n";
        }

        stream << "\n";
    }
}

const std::vector<std::string>& IniLexicon::GetSectionNames() const
{
    return sectionNames;
}

const std::vector<std::pair<std::string, std::string>>& IniLexicon::GetSectionValues(const std::string &sectionName) const
{
    auto i = sectionValues.find(sectionName);
    if (i != sectionValues.end())
        return i->second;

    return emptyValueSet;
}

bool IniLexicon::ValueExists(const std::string &sectionName, const std::string &valueName) const
{
    for (auto &sv : GetSectionValues(sectionName))
    {
        if (sv.first == valueName)
            return true;
    }

    return false;
}

const std::string& IniLexicon::GetValue(const std::string &sectionName, const std::string &valueName) const
{
    for (auto &sv : GetSectionValues(sectionName))
    {
        if (sv.first == valueName)
            return sv.second;
    }

    return emptyString;
}

void IniLexicon::SetValue(const std::string &sectionName, const std::string &valueName, const std::string &value)
{
    auto svi = sectionValues.find(sectionName);
    if (svi == sectionValues.end())
    {
        svi = sectionValues.emplace(sectionName, std::vector<std::pair<std::string, std::string>>()).first;
        sectionNames.push_back(sectionName);
    }

    for (auto &val : svi->second)
    {
        if (val.first == valueName)
        {
            val.second = value;
            return;
        }
    }

    svi->second.emplace_back(valueName, value);
}
