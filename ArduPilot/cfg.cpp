#include <iostream>
#include <set>
#include <optional>
#include "cfg.hpp"
#include "common.hpp"
using namespace std;

/* returns the param context if it describes a call worth investigating.
 * a call is worth investigating if it involves an intrinsic variable.
 * a variable is intrinsic if:
 *   (a) it has an intrinsic type; that is, a user specified its semantic type
 *       OR an intrinsic type transitively affected its value.
 *   (b) its source is a parameter bound to an intrinsic variable.
 */
static optional<vector<TypeInfo>>
get_interesting_param_context(const vector<TypeInfo> &call_context,
                              const vector<TypeInfo> &param_context) {
        bool had_interesting_param = false;
        vector<TypeSource> result;
        int param_no = 0;
        for (const auto &param: call_context) {
                TypeInfo ti;
                for (const auto &source: param.source) {
                        if (source.kind == SOURCE_INTRINSIC
                            || (source.kind == SOURCE_PARAM 
                                && ((size_t) source.param_no) < param_context.size()
                                && param_context.at(source.param_no) 
                                   == SOURCE_INTRINSIC)) {
                                had_interesting_param = true;
                                ti.source.push_back(SOURCE_INTRINSIC);
                        } else {
                                ti.source.push_back(SOURCE_UNKNOWN);
                        }
                }
        }

        if (!had_interesting_param)
                return {};
        else
                return result;
}

/**
 * returns the summaries of all functions with the name fn_name
 */
static vector<FunctionSummary> 
get_matching_summaries(const unordered_map<string, set<unsigned>> &name_to_tu,
                       const vector<map<string, FunctionSummary>> &fn_summaries,
                       const string &fn_name) {
        vector<FunctionSummary> results;
        const auto &translation_units = name_to_tu.find(fn_name);
        if (translation_units == name_to_tu.end())
                return results;
        for (auto translation_unit: translation_units->second) {
                const auto &fn_to_summary = fn_summaries.at(translation_unit);
                const auto &summary = fn_to_summary.find(fn_name);
                if (summary != fn_to_summary.end())
                        results.push_back(summary->second);
        }
        return results;
}

static vector<vector<string>>
check_trace(const unordered_map<string, set<unsigned>> &name_to_tu,
            const vector<map<string, FunctionSummary>> &fn_summaries,
            const string &fn_name,
            const vector<TypeInfo> &param_source,
            set<string> &visited_nodes) {

        // 1: find all function summaries
        vector<FunctionSummary> summaries = get_matching_summaries(name_to_tu, fn_summaries, fn_name);

        // 3: visit each callee in each summary
        vector<vector<string>> results;
        for (const auto &fs: summaries) {
                // 3 a: add this summary to the results trace if it is a write
                if (fs.store_to_typeinfo.size() > 0) {
                        // TODO: make sure that this write is coming from an intrinsic source
                        results.push_back({fn_name});
                }

                for (const auto &callee: fs.calling_context) {
                        for (const auto &call: callee.second) {
                                // check if we've already visited callee in this exploration
                                if (visited_nodes.find(callee.first) != visited_nodes.end())
                                        continue;

                                // check if this is an interesting function call
                                optional<vector<TypeSourceKind>> param_ctx =
                                        get_interesting_param_context(call, param_source);
                                if (!param_ctx)
                                        continue;

                                visited_nodes.insert(callee.first);

                                // using recursion here should be fine. 
                                // call chains shouldn't get too deep.
                                optional<vector<vector<string>>> traces = 
                                        check_trace(
                                                name_to_tu, 
                                                fn_summaries, 
                                                callee.first, 
                                                param_ctx.value(),
                                                visited_nodes
                                        );
                                if (traces.has_value()) {
                                        // we found something interesting!
                                         for (const auto &tr: traces.value()) {
                                                vector<string> updated_trace = {fn_name};
                                                updated_trace.insert(
                                                        updated_trace.end(),
                                                        tr.begin(), 
                                                        tr.end()
                                                );
                                                results.push_back(updated_trace);
                                       }
                                }

                                // it's alright to visit a callee multiple times, 
                                // just not from the same trace
                                visited_nodes.erase(callee.first);
                        }
                }
        }
        return results;
}

static vector<TypeInfo> get_initial_typeinfo(int num_params, const map<int, TypeSourceKind> &param_to_typesource) {
        vector<TypeInfo> result;
        for (int i = 0; i < num_params && ((size_t) i) < param_to_typesource.size(); i++) {
                TypeInfo ti;
                TypeSource ts;
                ts.kind = param_to_typesource.at(i);
                ts.param_no = i;
                ti.source.push_back(ts);
                result.push_back(ti);
        }
        return result;
}

vector<vector<string>> get_unconstrained_traces(const unordered_map<string, set<unsigned>> &name_to_tu,
                                                const vector<map<string, FunctionSummary>> &fn_summaries,
                                                const set<string> &fns_with_intrinsic_variables) {
        vector<vector<string>> results;
        int i = 1;
        for (const auto &fn_name: fns_with_intrinsic_variables) {
                cout << "processing " << i 
                     << " / " << fns_with_intrinsic_variables.size() << endl;
                vector<FunctionSummary> summaries = get_matching_summaries(
                        name_to_tu, 
                        fn_summaries, 
                        fn_name
                );
                if (summaries.size() == 0) {
                        cerr << "get_unconstrained_traces(): empty summary for " << fn_name << endl;
                        continue;
                }
                FunctionSummary first_summary = summaries[0];
                set<string> visited;
                vector<TypeInfo> type_info = get_initial_typeinfo(
                        first_summary.num_params, 
                        first_summary.param_to_typesource_kind
                );
                vector<vector<string>> traces = check_trace(
                        name_to_tu, 
                        fn_summaries, 
                        fn_name, 
                        type_info,
                        visited
                );
                if (!traces.empty())
                        results.insert(results.end(), traces.begin(), traces.end());
        }

        return results;
}
