#include <cassert>
#include <iostream>
#include <set>
#include <optional>
#include <unordered_map>
#include <sstream>
#include "cfg.hpp"
#include "common.hpp"
#include "mav.hpp"
using namespace std;

#define MAX_DEPTH 8

static vector<FunctionSummary> get_fn_summaries(const string &fn, 
                                                const unordered_map<string, set<unsigned>> &name_to_tu,
                                                const vector<map<string, FunctionSummary>> &fn_summaries) {
        vector<FunctionSummary> s;
        const auto tus = name_to_tu.find(fn);
        if (tus == name_to_tu.end())
                return s;
        for (unsigned tu: tus->second) {
                if (tu > fn_summaries.size()) {
                        cerr << "get_fn_summaries(): invalid translation unit number" << endl;
                        continue;
                }
                const auto fs = fn_summaries[tu].find(fn);
                if (fs != fn_summaries[tu].end())
                        s.push_back(fs->second);
        }
        return s;
}

// Maps (function names, call information) to their traces of buggy stores.
static unordered_map<string, unordered_map<vector<TypeInfo>, vector<vector<string>>, TypeInfoHash>> memoized_traces;

// Maps variable names to their type.
// For now, we just store the first type that was stored to the variable.
// If types differ later, then we probably found a bug.
// TODO: We could deduce what type is correct based on a majority voting scheme.
static unordered_map<string, TypeInfo> variable_name_to_type;

static vector<vector<string>> get_storage_trace(const string &fn,
                                                set<string> &visited,
                                                vector<vector<string>> &inconsistent_storage_traces,
                                                const unordered_map<string, set<unsigned>> &name_to_tu,
                                                const vector<map<string, FunctionSummary>> &fn_summaries,
                                                const set<string> &fns_with_intrinsic_variables,
                                                const vector<TypeInfo> &argtypes,
                                                const map<string, TypeInfo> &prior_types,
                                                int depth=0) {
        auto memoized_map = memoized_traces.find(fn);
        if (memoized_map != memoized_traces.end()) {
                auto memoized_result = memoized_map->second.find(argtypes);
                if (memoized_result != memoized_map->second.end()) {
                        return memoized_result->second;
                }
        }

        if (depth > MAX_DEPTH)
                return {};

        vector<FunctionSummary> summaries = get_fn_summaries(fn, name_to_tu, fn_summaries);
        vector<vector<string>> results;
        visited.insert(fn);
        for (const auto &fs: summaries) {
                // If we write to a global, add our name to the trace.
                for (const auto &store: fs.store_to_typeinfo) {
                        for (const auto &source: store.second.source) {
                                TypeInfo variable_type = store.second;

                                if (source.kind == SOURCE_PARAM) {
                                        if ((size_t) source.param_no >= argtypes.size()) continue;

				        assert((size_t) source.param_no < argtypes.size());
                                        const TypeInfo &the_param = argtypes.at(source.param_no);

                                        const auto &it = prior_types.find(store.first);
                                        assert(it != prior_types.end());

                                        // TODO: Only check types that were already known.
                                        if (it->second != the_param) {
                                                results.push_back({fn});
                                        }
                                        results.push_back({fn});

                                        variable_type = the_param;
                                }

                                // Check if this store matches the type of a previous store.
                                // If it doesn't, then we have found an inconsistent storage violation.
                                const auto &previously_found_type = variable_name_to_type.find(store.first);
                                if (previously_found_type != variable_name_to_type.end() &&
                                        previously_found_type->second != variable_type) {
                                                inconsistent_storage_traces.push_back({fn});
                                } else {
                                        variable_name_to_type[store.first] = store.second;
                                }
                        }
                }

                // Iterate over each function we call.
                for (const auto &ccs: fs.calling_context) {
                        string callee_name = ccs.first;
                        if (visited.find(callee_name) != visited.end())
                                continue;

                        // Iterate over each call site.
                        for (const auto &call: ccs.second) {
                                vector<vector<string>> callee_inconsistent_traces;
                                vector<vector<string>> traces = get_storage_trace(
                                        callee_name,
                                        visited,
                                        callee_inconsistent_traces,
                                        name_to_tu,
                                        fn_summaries,
                                        fns_with_intrinsic_variables,
                                        call,
                                        prior_types,
                                        depth+1
                                );
                                // Add fn to the front of every trace and add to our results.
                                for (const auto &trace : traces) {
                                        vector<string> new_trace = {fn};
                                        new_trace.insert(new_trace.end(), trace.begin(), trace.end());
                                        results.push_back(new_trace);
                                }
                                // Add fn to the front of every inconsistent storage trace.
                                for (auto &inconsistent_trace : callee_inconsistent_traces) {
                                        vector<string> new_trace = {fn};
                                        new_trace.insert(new_trace.end(), inconsistent_trace.begin(), inconsistent_trace.end());
                                        inconsistent_storage_traces.push_back(new_trace);
                                }
                        }
                }
        }
        visited.erase(fn);
        memoized_traces[fn][argtypes] = results;
        return results;
}

static vector<TypeInfo> get_initial_argtypes(const string &fn,
                                             const unordered_map<string, set<unsigned>> &name_to_tu,
                                             const vector<map<string, FunctionSummary>> &fn_summaries,
                                             int num_units) {
        // find translation units containing fn
        auto it = name_to_tu.find(fn);
        assert(it != name_to_tu.end());
        const set<unsigned> &tu_no = it->second;

        // TODO - fix me! for now, just use the first argument
        assert(!tu_no.empty());
        unsigned tu = *tu_no.begin();

        // access the translation unit containing fn
        assert(fn_summaries.size() > tu);
        const map<string, FunctionSummary> &name_to_summary = fn_summaries.at(tu);

        // lookup fn inside the translation unit
        auto summary_it = name_to_summary.find(fn);
        assert(summary_it != name_to_summary.end());
        const FunctionSummary &summary = summary_it->second;

        // build the initial argtypes
        vector<TypeInfo> args;
        for (const auto &param_and_source: summary.param_to_typesource_kind) {
                TypeInfo ti;
                for (int i = MAV_FRAME_GLOBAL; i < MAV_FRAME_NONE; i++)
                        ti.frames.insert(i);
                for (int i = 0; i < num_units; i++)
                        ti.units.insert(i);
                ti.source.push_back({param_and_source.second, param_and_source.first, ""});
                args.push_back(ti);
        }

        return args;
}

void print_trace(ostream &of, const vector<string> &trace) {
        string sep = "";
        for (const auto &fn: trace) {
                of << sep << fn;
                sep = " -> ";
        }
}

/**
 * @brief Returns the traces that contain an unconstrained store to a variable with a type already known.
 * 
 * @param name_to_tu A map relating function names to the translation unit containing their definitions.
 * @param fn_summaries A collection of function summaries. fn_summaries[0] is the summary of each function in TU 0, etc.
 * @param fns_with_intrinsic_variables The set of functions that contain variables with intrinsic semantic types.
 * @param prior_types A map relating variable names to their type information.
 * @param num_units The number of translation units.
 * @return vector<vector<string>> A vector of traces, e.g. [["fn1", "fn2", "lastFn"], ...]
 */
vector<vector<string>> get_unconstrained_traces(const unordered_map<string, set<unsigned>> &name_to_tu,
                                                const vector<map<string, FunctionSummary>> &fn_summaries,
                                                const set<string> &fns_with_intrinsic_variables,
                                                const map<string, TypeInfo> &prior_types,
                                                int num_units) {
        vector<vector<string>> result;
        int i = 1;
        set<string> found_traces;
        for (const auto &fn: fns_with_intrinsic_variables) {
                cout << i << " / " << fns_with_intrinsic_variables.size() << endl;
                set<string> visited;
                const vector<TypeInfo> args = get_initial_argtypes(
                        fn, 
                        name_to_tu, 
                        fn_summaries, 
                        num_units
                );
                vector<vector<string>> inconsistent_storage_traces;
                vector<vector<string>> traces = get_storage_trace(
                        fn,
                        visited,
                        inconsistent_storage_traces,
                        name_to_tu, 
                        fn_summaries, 
                        fns_with_intrinsic_variables,
                        args,
                        prior_types
                );
                for (const auto &trace: traces) {
                        stringstream ss;
                        print_trace(ss, trace);
                        string trace_str = ss.str();
                        if (found_traces.find(trace_str) == found_traces.end()) {
                                found_traces.insert(trace_str);
                                cout << "BUG: " << trace_str << endl;
                        }
                }
                set<string> inconsistent_traces;
                for (const auto &trace: inconsistent_storage_traces) {
                        stringstream ss;
                        print_trace(ss, trace);
                        auto trace_str = ss.str();
                        if (inconsistent_traces.find(trace_str) == inconsistent_traces.end()) {
                                inconsistent_traces.insert(trace_str);
                                cout << "Inconsistent store: " << trace_str << endl;
                        }
                }
                result.insert(result.end(), traces.begin(), traces.end());
                i++;
        }
        return result;
}
