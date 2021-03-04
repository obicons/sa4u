#pragma once
#include <string>
#include <map>
#include <vector>

#include <pugixml.hpp>

using namespace std;

string mavlink_msgname_to_typename(const string &msgname);

map<string, string> get_types_to_frame_field(const pugi::xml_document &doc);

map<string, map<string, int>> get_type_to_field_to_unit(const pugi::xml_document &doc,
                                                        map<string, int> &unitname_to_id);
