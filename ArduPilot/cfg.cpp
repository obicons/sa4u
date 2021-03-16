#include <iostream>
#include <set>
#include <optional>
#include <unordered_map>
#include "cfg.hpp"
#include "common.hpp"
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

unordered_map<string, vector<vector<string>>> trace_map;

static vector<vector<string>> get_storage_trace(const string &fn,
                                                set<string> &visited,
                                                const unordered_map<string, set<unsigned>> &name_to_tu,
                                                const vector<map<string, FunctionSummary>> &fn_summaries,
                                                const set<string> &fns_with_intrinsic_variables,
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
                if (!fs.store_to_typeinfo.empty())
                        results.push_back({fn});
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

vector<vector<string>> get_unconstrained_traces(const unordered_map<string, set<unsigned>> &name_to_tu,
                                                const vector<map<string, FunctionSummary>> &fn_summaries,
                                                const set<string> &fns_with_intrinsic_variables) {
        vector<vector<string>> result;
        int i = 1;
        for (const auto &fn: fns_with_intrinsic_variables) {
                cout << i << " / " << fns_with_intrinsic_variables.size() << endl;
                set<string> visited;
                vector<vector<string>> traces = get_storage_trace(
                        fn,
                        visited,
                        name_to_tu, 
                        fn_summaries, 
                        fns_with_intrinsic_variables
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
