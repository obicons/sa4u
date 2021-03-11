#include <cassert>
#include <sstream>
#include <rapidjson/document.h>
#include "deduce.hpp"
using namespace std;

// useful for debugging.
ostream& operator<<(ostream &out, const VariableEntry &ve) {
        out << ve.variable_name << endl;
        for (const auto &entry: ve.semantic_info.coordinate_frames)
                out << entry << " ";
        out << endl;
        for (const auto &entry: ve.semantic_info.units)
                out << entry << " ";
        return out;
}

// reads a stream containing a JSON array of VariableEntry's.
vector<VariableEntry> read_variable_info(istream &in) {
        vector<VariableEntry> result;

        stringstream ss;
        string line;
        while (getline(in, line))
                ss << line;

        rapidjson::Document d;
        d.Parse(ss.str().c_str());

        auto array = d.GetArray();
        for (auto i = array.Begin(); i != array.End(); i++) {
                VariableEntry entry;
                entry.variable_name = (*i)["VariableName"].GetString();
                
                auto coord_frames = (*i)["SemanticInfo"]["CoordinateFrames"].GetArray();
                for (auto j = coord_frames.Begin(); j != coord_frames.End(); j++) {
                        entry.semantic_info.coordinate_frames.insert(j->GetString());
                }

                auto units = (*i)["SemanticInfo"]["Units"].GetArray();
                for (auto j = units.Begin(); j != units.End(); j++) {
                        entry.semantic_info.units.insert(j->GetString());
                }

                result.push_back(entry);
        }

        return result;
}
