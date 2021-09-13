#pragma once

#include <functional>
#include <map>
#include <set>
#include <vector>
using namespace std;

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

struct TypeInfo {
        // stores possible frames the object can take on
        set<int> frames;

        // stores possible units the object can take on
        set<int> units;

        // tracks why a type has a particular value
        vector<TypeSource> source;

        bool operator==(const TypeInfo &other) const {
                return frames == other.frames &&
                units == other.units;
        }

        bool operator!=(const TypeInfo &other) const {
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
