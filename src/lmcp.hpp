#pragma once

#include <string>
#include <map>
#include <vector>

using namespace std;

#include <pugixml.hpp>

#include "units.hpp"

string lmcp_field_to_get_function_name(const string &structure_name, const string &field_name);
string lmcp_field_to_set_function_name(const string &structure_name, const string &field_name);

map<string, TypeInfo> get_units_of_functions(const pugi::xml_document &doc,
                                             map<string, int> &unitname_to_id, 
                                             int &num_units);
