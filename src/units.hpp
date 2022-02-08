#pragma once

#include <map>
#include <optional>

#include "common.hpp"

const Dimension centimeter = {.coefficients = {1, 0, 0, 0, 0, 0, 0}, .scalar_numerator = 1, .scalar_denominator = 100};
const Dimension meter = {.coefficients = {1, 0, 0, 0, 0, 0, 0}, .scalar_numerator = 1, .scalar_denominator = 1};

static const map<string, Dimension> unit_name_to_coefficients = {
    {"centimeter", centimeter},
    {"meter", meter},
    // TODO: fill in with other units!
};

// Try to convert spelling into a dimension.
optional<Dimension> string_to_dimension(const string &spelling);

ostream& operator<<(ostream &o, const Dimension &d);