#include "units.hpp"

// Try to convert spelling into a dimension.
optional<Dimension> string_to_dimension(const string &spelling) {
    if (unit_name_to_coefficients.find(spelling) != unit_name_to_coefficients.end()) {
        return unit_name_to_coefficients.at(spelling);
    }
    return {};
}


ostream& operator<<(ostream &o, const Dimension &d) {
    string sep = "";
    o << d.scalar_numerator << "/" << d.scalar_denominator << " * [";
    for (int i : d.coefficients) {
        o << sep << i;
        sep = ", ";
    }
    return o << "]";
}