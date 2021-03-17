#pragma once

#include "common.hpp"
#include "deduce.hpp"
#include <map>
#include <unordered_map>
#include <vector>

vector<vector<string>> get_unconstrained_traces(const unordered_map<string, set<unsigned>> &name_to_tu,
                                                const vector<map<string, FunctionSummary>> &fn_summaries,
                                                const set<string> &fns_with_intrinsic_variables,
                                                const map<string, TypeInfo> &prior_types,
                                                int num_units);
