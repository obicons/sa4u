#include <cassert>
#include <iostream>
#include <set>
#include <optional>
#include <unordered_map>
#include "cfg.hpp"
#include "common.hpp"
#include "mav.hpp"
using namespace std;

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

static unordered_map<string, vector<vector<string>>> trace_map;

static vector<vector<string>> get_storage_trace(const string &fn,
                                                set<string> &visited,
                                                const unordered_map<string, set<unsigned>> &name_to_tu,
                                                const vector<map<string, FunctionSummary>> &fn_summaries,
                                                const set<string> &fns_with_intrinsic_variables,
                                                const vector<TypeInfo> &argtypes,
                                                const map<string, TypeInfo> &prior_types,
                                                int depth=0) {
        if (trace_map.find(fn) != trace_map.end())
                return trace_map[fn];
        else if (depth > 6)
                return {};

        for (auto i = 0; i < 2 * depth; i++)
                cout << " ";
        cout << fn << endl;

        vector<FunctionSummary> summaries = get_fn_summaries(fn, name_to_tu, fn_summaries);
        vector<vector<string>> results;
        visited.insert(fn);
        for (const auto &fs: summaries) {
                // if we write to a global, add our name to the the trace
                // TODO: this should happen only if we are not in the right constraint
                for (const auto &store: fs.store_to_typeinfo) {
                        for (const auto &source: store.second.source) {
                                if (source.kind == SOURCE_PARAM) {
                                        assert(source.param_no < argtypes.size());
                                        const TypeInfo &the_param = argtypes.at(source.param_no);

                                        const auto &it = prior_types.find(store.first);
                                        assert(it != prior_types.end());

                                        if (it->second != the_param) {
                                                results.push_back({fn});
                                        }
                                }
                        }
                }

                // iterate over each function we call
                for (const auto &ccs: fs.calling_context) {
                        string callee_name = ccs.first;
                        if (visited.find(callee_name) != visited.end())
                                continue;

                        // iterate over each call site
                        for (const auto &call: ccs.second) {
                                // TODO: supply call site information to get_storage_trace
                                vector<vector<string>> traces = get_storage_trace(
                                        callee_name,
                                        visited,
                                        name_to_tu,
                                        fn_summaries,
                                        fns_with_intrinsic_variables,
                                        call,
                                        prior_types,
                                        depth+1
                                );
                                // add fn to the front of every trace and add to our results
                                for (const auto &trace: traces) {
                                        vector<string> new_trace = {fn};
                                        new_trace.insert(new_trace.end(), trace.begin(), trace.end());
                                        results.push_back(new_trace);
                                }
                                // only consider a call site once
                                break;
                        }
                }
        }
        visited.erase(fn);
        trace_map[fn] = results;
        return results;
}

static vector<TypeInfo> get_initial_argtypes(const string &fn,
                                             const unordered_map<string, set<unsigned>> &name_to_tu,
                                             const vector<map<string, FunctionSummary>> &fn_summaries,
                                             const set<string> &fns_with_intrinsic_variables,
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

vector<vector<string>> get_unconstrained_traces(const unordered_map<string, set<unsigned>> &name_to_tu,
                                                const vector<map<string, FunctionSummary>> &fn_summaries,
                                                const set<string> &fns_with_intrinsic_variables,
                                                const map<string, TypeInfo> &prior_types,
                                                int num_units) {
        vector<vector<string>> result;
        int i = 1;
        for (const auto &fn: fns_with_intrinsic_variables) {
                cout << i << " / " << fns_with_intrinsic_variables.size() << endl;
                set<string> visited;
                const vector<TypeInfo> args = get_initial_argtypes(
                        fn, 
                        name_to_tu, 
                        fn_summaries, 
                        fns_with_intrinsic_variables,
                        num_units
                );
                vector<vector<string>> traces = get_storage_trace(
                        fn,
                        visited,
                        name_to_tu, 
                        fn_summaries, 
                        fns_with_intrinsic_variables,
                        args,
                        prior_types
                );
                for (const auto &trace: traces) {
                        string sep = "";
                        cout << "BUG: ";
                        for (const auto &fn: trace) {
                                cout << sep << fn;
                                sep = " -> ";
                        }
                        cout << endl;
                }
                result.insert(result.end(), traces.begin(), traces.end());
                i++;
        }
        return result;
}
