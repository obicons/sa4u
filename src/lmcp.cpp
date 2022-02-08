#include <algorithm>
#include <cstring>
#include "lmcp.hpp"
#include "mav.hpp"

// returns the internal type that will be used to represent the lmcp get function name
string lmcp_field_to_get_function_name(const string &structure_name, const string &field_name) {
        string result = field_name;
        result[0] = std::toupper(result[0]);
        return "afrl::cmasi::"+structure_name+"::get"+result;
}

// returns the internal type that will be used to represent the lmcp set function name
string lmcp_field_to_set_function_name(const string &structure_name, const string &field_name) {
        string result = field_name;
        result[0] = std::toupper(result[0]);
        return "afrl::cmasi::"+structure_name+"::set"+result;
}

// returns a map relating lmcp message functions to their unit kinds
map<string, TypeInfo> get_units_of_functions(const pugi::xml_document &doc, map<string, int> &unitname_to_id, int &nextid) {
        nextid = 0;
        map<string, TypeInfo> functions_to_units;
        for (const pugi::xml_node &structure: doc.child("MDM").child("StructList")) {
                string structure_name = structure.attribute("Name").value();
                for (const pugi::xml_node &field: structure){
                        if (field.type() == pugi::node_element && field.name() == "Field"s && field.attribute("Units")) {
                                string unit_type = field.attribute("Units").value();
                                if (unitname_to_id.find(unit_type) == unitname_to_id.end()) {
                                        unitname_to_id[unit_type] = nextid++;
                                }
                                string field_name = field.attribute("Name").value();
                                functions_to_units[lmcp_field_to_get_function_name(structure_name, field_name)] = {
                                        // TODO: frame is incorrect, but fine for now.
                                        .frames = {MAV_FRAME_GLOBAL},
                                        .units = {unitname_to_id[unit_type]},
                                        .source = {},
                                        .dimension = string_to_dimension(unit_type),
                                };
                                functions_to_units[lmcp_field_to_set_function_name(structure_name, field_name)] = {
                                        // TODO: frame is incorrect, but fine for now.
                                        .frames = {MAV_FRAME_GLOBAL},
                                        .units = {unitname_to_id[unit_type]},
                                        .source = {},
                                        .dimension = string_to_dimension(unit_type),
                                };
                        }
                }
        }
        return functions_to_units;
}
