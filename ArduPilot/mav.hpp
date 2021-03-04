#pragma once

#include <string>
#include <map>
#include <vector>

#include <pugixml.hpp>

using namespace std;

string mavlink_msgname_to_typename(const string &msgname);

map<string, string> get_types_to_frame_field(const pugi::xml_document &doc);

map<string, map<string, int>> get_type_to_field_to_unit(const pugi::xml_document &doc,
                                                        map<string, int> &unitname_to_id,
                                                        int &num_units);

enum MAVFrame {
        MAV_FRAME_GLOBAL,
        MAV_FRAME_LOCAL_NED,
        MAV_FRAME_MISSION,
        MAV_FRAME_GLOBAL_RELATIVE_ALT,
        MAV_FRAME_LOCAL_ENU,
        MAV_FRAME_GLOBAL_INT,
        MAV_FRAME_GLOBAL_RELATIVE_ALT_INT,
        MAV_FRAME_LOCAL_OFFSET_NED,
        MAV_FRAME_BODY_NED,
        MAV_FRAME_BODY_OFFSET_NED,
        MAV_FRAME_GLOBAL_TERRAIN_ALT,
        MAV_FRAME_GLOBAL_TERRAIN_ALT_INT,
        MAV_FRAME_BODY_FRD,
        MAV_FRAME_LOCAL_FRD,
        MAV_FRAME_LOCAL_FLU,
        MAV_FRAME_NONE,
};
