#pragma once

#include <iostream>
#include <set>
#include <vector>

using namespace std;

struct VariableSemanticInfo {
        set<string> coordinate_frames;
        set<string> units;
};

struct VariableEntry {
        string variable_name;
        VariableSemanticInfo semantic_info;
};

vector<VariableEntry> read_variable_info(istream &in);
