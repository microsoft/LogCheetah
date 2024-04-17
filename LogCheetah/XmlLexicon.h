#pragma once
#include <string>
#include <vector>
#include <map>
#include <istream>
#include <algorithm>

class XmlLexicon
{
public:
    class Node
    {
    public:
        std::string Name;
        std::vector<Node> Children;
        std::string Value; //set to the contents in the case that there are no children

        inline const std::string* FindProperty(const std::string &name) const
        {
            auto i = std::lower_bound(PropertyList.begin(), PropertyList.end(), name, [](const std::pair<std::string, std::string> &a, const std::string &b) { return a.first < b; });
            if (i != PropertyList.end() && i->first == name)
                return &i->second;

            return nullptr;
        }

        std::vector<std::pair<std::string, std::string>> PropertyList; //should be sorted by first

        Node() = default;
        Node(Node &&o) = default;
        Node& operator=(Node &&o) = default;
    };

    XmlLexicon(std::istream &stream);

    const std::vector<Node>& GetRoots() const;

private:
    std::vector<Node> roots;
};
