#pragma once
#include <string>
#include <vector>
#include <map>
#include <istream>
#include <ostream>

class IniLexicon
{
public:
    inline IniLexicon() {}
    IniLexicon(std::istream &stream);

    void Save(std::ostream &stream) const;

    const std::vector<std::string>& GetSectionNames() const;
    const std::vector<std::pair<std::string, std::string>>& GetSectionValues(const std::string &sectionName) const;

    bool ValueExists(const std::string &sectionName, const std::string &valueName) const;
    const std::string& GetValue(const std::string &sectionName, const std::string &valueName) const; //returns empty string if not found
    void SetValue(const std::string &sectionName, const std::string &valueName, const std::string &value);

private:
    std::vector<std::string> sectionNames;
    std::map<std::string, std::vector<std::pair<std::string, std::string>>> sectionValues;
};
