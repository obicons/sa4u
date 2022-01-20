#pragma once

#include <array>
#include <functional>
#include <map>
#include <optional>
#include <set>
#include <vector>

#include "util.hpp"
using namespace std;

const int SI_BASE_UNITS_COUNT = 7;

enum ConstraintType {
        UNCONSTRAINED,
        IFCONDITION,
        SWITCHSTMT,
};

enum TypeSourceKind {
        // a store from param -> var
        SOURCE_PARAM,

        // a store from global var -> var
        SOURCE_VAR,

        // the variable has intrinsic type information
        SOURCE_INTRINSIC,

        // indicates that we don't know the source of the type
        SOURCE_UNKNOWN,
};

struct TypeSource {
        // stores the kind of type source
        TypeSourceKind kind;

        // stores the # in the param list (if applicable)
        int param_no;

        // stores the variable name (if applicable)
        string var_name;
};

// Represents the dimension (e.g. cm) of a measurement.
struct Dimension {
        // Stores the coefficients of each base unit.
        // Order: <m(eter), s(econd), g(ram), A(mp), K(elvin), m(ol), cd (candela)>.
        // Examples:
        //   m/s is <1, -1, 0, 0, 0, 0, 0>.
        //   bottom is <0, 0, 0, 0, 0, 0, 0>.
        array<int, SI_BASE_UNITS_COUNT> coefficients;        

        // Stores the numerator of the scalar multiple of a unit.
        // e.g. 1 cm = 1/100 * 1m
        int scalar_numerator;
        int scalar_denominator;

        // Returns true if this dimension is the bottom dimension.
        bool bottom() const {
                bool is_bottom = true;
                for (size_t i = 0; i < coefficients.size() && is_bottom; i++) {
                        is_bottom = coefficients[i] == 0;
                }
                return is_bottom;
        }

        // Handles the multiplication case.
        // Example: m * s = meter seconds.
        Dimension operator*(const Dimension &other) const {
                Dimension d;
                for (size_t i = 0; i < coefficients.size(); i++) {
                        d.coefficients[i] = coefficients[i] + other.coefficients[i];
                }
                d.scalar_denominator = scalar_numerator * other.scalar_numerator;
                d.scalar_numerator = scalar_denominator * other.scalar_denominator;
                int factor = gcd(d.scalar_numerator, d.scalar_denominator);
                if (factor != 0) {
                        d.scalar_denominator /= factor;
                        d.scalar_numerator /= factor;
                }
                return d;
        }

        // Handles the division case.
        // Example: m / s = m * s^-1.
        Dimension operator/(const Dimension &other) const {
                Dimension d;
                for (size_t i = 0; i < coefficients.size(); i++) {
                        d.coefficients[i] = coefficients[i] - other.coefficients[i];
                }
                d.scalar_numerator = scalar_numerator * other.scalar_denominator;
                d.scalar_denominator = scalar_denominator * other.scalar_numerator;
                int factor = gcd(d.scalar_numerator, d.scalar_denominator);
                if (factor != 1) {
                        d.scalar_denominator /= factor;
                        d.scalar_numerator /= factor;
                }
                return d;
        }

        bool operator==(const Dimension &d) const {
                return d.coefficients == coefficients &&
                       d.scalar_denominator == scalar_denominator &&
                       d.scalar_numerator == scalar_numerator;
        }

        bool operator!=(const Dimension &d) const {
                return !(d == *this);
        }
};

struct TypeInfo {
        // stores possible frames the object can take on
        set<int> frames;

        // stores possible units the object can take on
        set<int> units;

        // tracks why a type has a particular value
        vector<TypeSource> source;

        // The high-fidelity type representation.
        optional<Dimension> dimension;

        bool operator==(const TypeInfo &other) const {
                if (other.dimension && dimension) {
                        return dimension.value() == other.dimension.value();
                }
                return frames == other.frames &&
                units == other.units;
        }

        bool operator!=(const TypeInfo &other) const {
                if (other.dimension && dimension) {
                        return dimension.value() != other.dimension.value();
                }
                return frames != other.frames ||
                units != other.units;
        }
};

static size_t hash_typeinfo(const TypeInfo &ti) {
        size_t hash = 0;
        for (auto i: ti.frames) hash ^= i;
        for (auto i: ti.units) hash ^= i;
        return hash;    
}

struct TypeInfoHash {
        size_t operator()(const vector<TypeInfo> &v) const noexcept {
                size_t hash = 0;
                for (const auto &ti: v) hash ^= hash_typeinfo(ti);
                return hash;    
        }
};

struct FunctionSummary {
        // functions this function calls
        set<string> callees;

        // maps function names to a collection of function calls
        map<string, vector<vector<TypeInfo>>> calling_context;

        // tracks the type source of parameters
        map<int, TypeSourceKind> param_to_typesource_kind;

        // number of parameters the function takes
        int num_params;

        // tracks interesting stores that occur
        // maps C++ type info to our internal type info
        map<string, TypeInfo> store_to_typeinfo;
};
