#include <algorithm>
#include <cstring>
#include "mav.hpp"

// returns the internal type that will be used to represent the mavlink message with name
string mavlink_msgname_to_typename(const string &msgname) {
        string result = msgname;
        transform(result.begin(), result.end(), result.begin(), [](unsigned char c){ return std::tolower(c); });
        result = "mavlink_" + result + "_t";
        return result;
}

// returns a map relating mavlink message types to their frame field
map<string, string> get_types_to_frame_field(const pugi::xml_document &doc) {
        map<string, string> result;
        for (const pugi::xml_node &msg: doc.child("mavlink").child("messages")) {
                pugi::xml_attribute node_type = msg.attribute("name");
                if (node_type) {
                        bool frame_member = false;
                        string frame_member_name;
                        for (const pugi::xml_node &param: msg) {
                                // check if there's a frame member
                                if (param.type() == pugi::node_element &&
                                    strcmp(param.name(), "field") == 0 &&
                                    strcmp(param.attribute("enum").value(), "MAV_FRAME") == 0) {
                                        frame_member_name = string(param.attribute("name").value());
                                        frame_member = true;
                                        break;
                                }
                        }

                        if (frame_member) {
                                // use the name of the message to generate the c++ struct name
                                string structname = mavlink_msgname_to_typename(node_type.value());
                                result[structname] = frame_member_name;
                        }
                }
        }

        return result;
}


// returns a map relating mavlink message types to their fields to their unit kinds
map<string, map<string, int>> get_type_to_field_to_unit(const pugi::xml_document &doc,
                                                        map<string, int> &unitname_to_id) {
        int nextid = 0;
        map<string, map<string, int>> type_to_field_to_unit;
        for (const pugi::xml_node &msg: doc.child("mavlink").child("messages")) {
                pugi::xml_attribute msg_name = msg.attribute("name");
                if (msg_name) {
                        string structname = mavlink_msgname_to_typename(msg_name.value());
                        for (const pugi::xml_node &param: msg) {
                                if (param.type() == pugi::node_element && param.attribute("units")) {
                                        string unit_type = param.attribute("units").value();

                                        // see if we do not have an ID for this unit type already.
                                        // if we don't, create one.
                                        if (unitname_to_id.find(unit_type) == unitname_to_id.end()) {
                                                unitname_to_id[unit_type] = nextid++;
                                        }

                                        string field_name(param.attribute("name").value());
                                        type_to_field_to_unit[structname][field_name] = unitname_to_id[unit_type];
                                }
                        }
                }
        }
        return type_to_field_to_unit;
}
