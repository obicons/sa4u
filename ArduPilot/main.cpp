#include <algorithm>
#include <cctype>
#include <cstring>
#include <iostream>
#include <fstream>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <vector>
using namespace std;

extern "C" {
#include <clang-c/CXCompilationDatabase.h>
#include <clang-c/Index.h>
#include <unistd.h>
}

// see https://pugixml.org/docs/manual.html
#include <pugixml.hpp>

#include "mav.hpp"
#include "util.hpp"

#define UNUSED /* UNUSED */

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
};

struct FunctionSummary {
        set<string> callees;
        map<string, vector<TypeInfo>> calling_context;
};

struct ASTContext {
        // maps each mavlink struct name to its frame field
        const map<string, string> &types_to_frame_field;

        // maps each mavlink struct name to a map relating fields to units
        const map<string, map<string, int>> &type_to_field_to_unit;

        // stores the number of units there are
        const int num_units;

        // communicates context to AST walkers
        ConstraintType constraint;
        set<string> &possible_frames;
        bool in_mav_constraint;
        map<unsigned, set<string>> &scope_to_tainted;
        bool had_mav_constraint;
        bool had_taint;

        // maps variables to their type info
        vector<map<string, TypeInfo>> &var_types;

        // maps function names to their summary
        map<string, FunctionSummary> &fn_summary;

        // stores the current function name
        string current_fn;

        // stores the names of the parameters of the current function
        set<string> &current_fn_params;

        // maps the name of parameters to their number in args list
        map<string, int> &param_to_number;

        // counts total # of parameters to this function
        int total_params;
};

string trim(const string& str,
            const string& whitespace = " ") {
    const auto strBegin = str.find_first_not_of(whitespace);
    if (strBegin == std::string::npos)
        return ""; // no content

    const auto strEnd = str.find_last_not_of(whitespace);
    const auto strRange = strEnd - strBegin + 1;

    return str.substr(strBegin, strRange);
}

/**
 * Returns the type information associated with varname
 */
optional<TypeInfo> get_var_typeinfo(const string &varname, const vector<map<string, TypeInfo>> &var_types) {
        for (auto i = 0ul; i < var_types.size(); i++) {
                const auto &m = var_types[var_types.size() - 1 - i];
                const auto &p = m.find(varname);
                if (p != m.end()) {
                        return p->second;
                }
        }
        return {};
}

string get_full_path(CXString compile_dir, CXString filename) {
        const char *filename_cstr = clang_getCString(filename);
        const char *dir_cstr = clang_getCString(compile_dir);
        if (filename_cstr[0] == '/')
                return string(filename_cstr);
        else
                return string(dir_cstr) + string("/") + string(filename_cstr);
}

enum CXChildVisitResult count_left_tokens(CXCursor c, CXCursor UNUSED, CXClientData cd) {
        unsigned *count = static_cast<unsigned*>(cd);
        CXTranslationUnit unit = clang_Cursor_getTranslationUnit(c);
        CXSourceRange range = clang_getCursorExtent(c);
        CXToken *tokens;
        clang_tokenize(unit, range, &tokens, count);
        clang_disposeTokens(unit, tokens, *count);
        return CXChildVisit_Break;
}

// Returns the binary operator at cursor.
// How this is accomplished:
//   A binary operator has two children.
//   We count the number of tokens of the left child,
//   so then the next token must be the operator.
string get_binary_operator(CXCursor cursor) {
        unsigned left_tokens;
        clang_visitChildren(cursor, count_left_tokens, &left_tokens);

        CXTranslationUnit unit = clang_Cursor_getTranslationUnit(cursor);
        CXSourceRange range = clang_getCursorExtent(cursor);
        CXToken *tokens;
        unsigned count;
        clang_tokenize(unit, range, &tokens, &count);
        
        string result;
        if (left_tokens < count) {
                CXString token_spelling = clang_getTokenSpelling(unit, tokens[left_tokens]);
                result = string(clang_getCString(token_spelling));
                clang_disposeString(token_spelling);
        }

        clang_disposeTokens(unit, tokens, count);

        return result;
}

// returns the underlying typename associated with type.
// e.g. if there are qualifiers like const, those are removed.
string get_object_typename(CXType type) {
        // recurse to get pointer types
        if (type.kind == CXType_Pointer)
                return get_object_typename(clang_getPointeeType(type));

        CXString type_spelling = clang_getTypeSpelling(type);
        string result(clang_getCString(type_spelling));
        clang_disposeString(type_spelling);

        size_t pos;
        while ((pos = result.find("const ")) != string::npos)
                result.erase(pos, 6);
        while ((pos = result.find("&")) != string::npos)
                result.erase(pos, 1);

        // TODO: make this function less wrong
        return trim(result);
}

enum CXChildVisitResult check_mavlink(CXCursor cursor, CXCursor parent, CXClientData client_data) {
        ASTContext *ctx = static_cast<ASTContext*>(client_data);
        ctx->in_mav_constraint = false;
        if (clang_getCursorKind(cursor) == CXCursor_DeclRefExpr && clang_getCursorKind(parent) == CXCursor_MemberRefExpr) {
                CXType type = clang_getCursorType(cursor);
                string the_type = get_object_typename(type);

                CXString parent_member = clang_getCursorSpelling(parent);
                string the_parent_member = string(clang_getCString(parent_member));
                clang_disposeString(parent_member);

                const auto &pair = ctx->types_to_frame_field.find(the_type);
                if (pair != ctx->types_to_frame_field.end() && pair->second == the_parent_member) {
                        ctx->in_mav_constraint = true;
                        ctx->had_mav_constraint = true;
                }

                return CXChildVisit_Break;
        }
        return CXChildVisit_Recurse;
}

enum CXChildVisitResult pretty_print_memberRefExpr_walker(CXCursor c, CXCursor UNUSED, CXClientData cd) {
        string *sptr = static_cast<string*>(cd);
        CXCursorKind kind = clang_getCursorKind(c);
        CXString cursor_spelling = clang_getCursorSpelling(c);
        string cursor_str = clang_getCString(cursor_spelling);
        clang_disposeString(cursor_spelling);
        if (kind == CXCursor_DeclRefExpr) {
                *sptr = cursor_str + *sptr;
        } else if (kind == CXCursor_MemberRefExpr) {
                *sptr = "." + cursor_str + *sptr;
        }
        return CXChildVisit_Recurse;
}

string pretty_print_memberRefExpr(CXCursor c) {
        string result;
        CXString cursor_spelling = clang_getCursorSpelling(c);
        string cursor_str = clang_getCString(cursor_spelling);
        clang_disposeString(cursor_spelling);
        clang_visitChildren(c, pretty_print_memberRefExpr_walker, &result);
        result += "." + cursor_str;
        return result;
}

enum CXChildVisitResult pretty_print_store_walker(CXCursor c, CXCursor UNUSED, CXClientData cd) {
        string *sptr = static_cast<string*>(cd);
        CXCursorKind kind = clang_getCursorKind(c);
        if (kind == CXCursor_MemberRefExpr) {
                *sptr = pretty_print_memberRefExpr(c);
        } else if (kind == CXCursor_ArraySubscriptExpr) {
                return CXChildVisit_Recurse;
        } else {
                CXString cursor_spelling = clang_getCursorSpelling(c);
                *sptr = clang_getCString(cursor_spelling);
                clang_disposeString(cursor_spelling);
        }
        return CXChildVisit_Break;
}

string pretty_print_store(CXCursor c) {
        string result;
        clang_visitChildren(c, pretty_print_store_walker, &result);
        return result;
}

// if c is part of a variable initialization, returns a cursor representing the declaration
CXCursor get_initialization_decl(CXCursor c) {
        CXCursor parent = c;
        while (clang_getCursorKind(parent) != CXCursor_VarDecl) {
                parent = clang_getCursorSemanticParent(parent);
        }
        return parent;
}

/**
 * t - a known type
 * name - variable name
 * type_to_field_to_unit - relates types to fields to units
 * tinfo - type info
 */
void add_inner_vars(const string &t, const string &name,
                    const map<string, map<string, int>> &type_to_field_to_unit,
                    const TypeSource &source,                    
                    map<string, TypeInfo> &tinfo) {
        auto typeinfo = type_to_field_to_unit.find(t);
        if (typeinfo == type_to_field_to_unit.end())
                return;
        for (const auto &pair: typeinfo->second) {
                string varname = name + "." + pair.first;
                tinfo[varname].units.insert(pair.second);

                for (int i = MAV_FRAME_GLOBAL; i < MAV_FRAME_NONE; i++)
                        tinfo[varname].frames.insert(i);

                tinfo[varname].source.push_back(source);
        }
}

enum CXChildVisitResult check_tainted_decl_walker(CXCursor c, CXCursor UNUSED, CXClientData cd) {
        ASTContext *ctx = static_cast<ASTContext*>(cd);
        CXCursorKind kind = clang_getCursorKind(c);

        // TODO: refactor this into an expression-level type checker.
        string varname;
        if (kind == CXCursor_DeclRefExpr) {
                varname = get_cursor_spelling(c);
        } else if (kind == CXCursor_MemberRefExpr) {
                varname = pretty_print_memberRefExpr(c);
        } else {
                return CXChildVisit_Recurse;
        }

        optional<TypeInfo> ti = get_var_typeinfo(varname, ctx->var_types);
        if (ti.has_value() && !ctx->var_types.empty()) {
                CXCursor lhs = get_initialization_decl(c);
                string new_varname = get_cursor_spelling(lhs);
                ctx->var_types.back()[new_varname] = ti.value();
                // cout << "Tainted decl " << new_varname << " found" << endl;
                return CXChildVisit_Break;
        }

        return CXChildVisit_Recurse;
}

// Checks if cursor stores a mavlink message field into a variable declaration
void check_tainted_decl(CXCursor cursor, ASTContext *ctx) {
        string cursor_typename = get_object_typename(clang_getCursorType(cursor));
        bool is_known_type = ctx->types_to_frame_field.find(cursor_typename)
                != ctx->types_to_frame_field.end();
        if (is_known_type && !ctx->var_types.empty()) {
                TypeSource source = {SOURCE_INTRINSIC, 0, ""};
                add_inner_vars(cursor_typename, get_cursor_spelling(cursor),
                               ctx->type_to_field_to_unit, source, ctx->var_types.back());
        } else {
                clang_visitChildren(cursor, check_tainted_decl_walker, ctx);
        }
}

static int ctaw_childno;
enum CXChildVisitResult check_tainted_assgn_walker(CXCursor c, CXCursor UNUSED, CXClientData cd) {
        if (!ctaw_childno) {
                ctaw_childno++;
                return CXChildVisit_Continue;
        }

        pair<optional<TypeInfo>, ASTContext*> *p = static_cast<pair<optional<TypeInfo>, ASTContext*>*>(cd);
        CXCursorKind kind = clang_getCursorKind(c);
        string varname;
        if (kind == CXCursor_MemberRefExpr)
                varname = pretty_print_memberRefExpr(c);
        else if (kind == CXCursor_DeclRefExpr)
                varname = get_cursor_spelling(c);

        if (varname == "")
                return CXChildVisit_Recurse;

        p->first = get_var_typeinfo(varname, p->second->var_types);

        if (p->first.has_value())
                return CXChildVisit_Break;
        else
                return CXChildVisit_Recurse;

        return CXChildVisit_Recurse;
}

enum CXChildVisitResult check_for_param_rhs(CXCursor c, CXCursor UNUSED, CXClientData cd) {
        if (!ctaw_childno) {
                ctaw_childno++;
                return CXChildVisit_Continue;
        }

        pair<optional<TypeInfo>, ASTContext*> *p = static_cast<pair<optional<TypeInfo>, ASTContext*>*>(cd);
        CXCursorKind kind = clang_getCursorKind(c);
        string varname;
        if (kind == CXCursor_DeclRefExpr)
                varname = get_cursor_spelling(c);
        else
                return CXChildVisit_Recurse;

        // cout << p->second->current_fn << endl;
        // for (const auto &str: p->second->current_fn_params)
        //         cout << "  " << str << endl;
        // cout << endl;

        bool is_param = p->second->current_fn_params.find(varname) != p->second->current_fn_params.end();
        if (is_param) {
                TypeInfo ti;
                // any frame.
                for (int i = MAV_FRAME_GLOBAL; i < MAV_FRAME_NONE; i++)
                        ti.frames.insert(i);
                for (int i = 0; i < p->second->num_units; i++)
                        ti.units.insert(i);
                TypeSource source = {
                        SOURCE_PARAM,
                        p->second->param_to_number[varname],
                        "",
                };
                ti.source.push_back(source);
                p->first = ti;
                return CXChildVisit_Break;
        }

        return CXChildVisit_Recurse;
}

// Checks if cursor stores (op =) a mavlink message field into another object
void check_tainted_store(CXCursor cursor, ASTContext *ctx) {
        pair<optional<TypeInfo>, ASTContext*> p({}, ctx);
        ctaw_childno = 0;
        clang_visitChildren(cursor, check_tainted_assgn_walker, &p);
        if (!p.first){
                ctaw_childno = 0;
                clang_visitChildren(cursor, check_for_param_rhs, &p);
        } else {
                string varname = pretty_print_store(cursor);
                cout << "CONSTRAINED Typed store to " << varname << " detected" << endl;
        }
        if (p.first && !ctx->var_types.empty()) {
                string varname = pretty_print_store(cursor);
                // cout << "Typed store to " << varname << " detected" << endl;
                ctx->var_types.back()[varname] = p.first.value();
        }
}

// Unifies types that appear in last two scope levels
void unify_scopes(map<string, TypeInfo> &old, const map<string, TypeInfo> &latest) {
        for (const auto &p: latest) {
                const auto &it = old.find(p.first);
                if (it != old.end()) {
                        const auto &latest_frames = p.second.frames;
                        const auto &latest_units = p.second.units;
                        const auto &sources = p.second.source;
                        it->second.frames.insert(latest_frames.begin(),
                                                 latest_frames.end());
                        it->second.units.insert(latest_units.begin(),
                                                latest_units.end());
                        it->second.source.insert(it->second.source.end(),
                                                 sources.begin(),
                                                 sources.end());
                }
        }
}

enum CXChildVisitResult function_ast_walker(CXCursor cursor, CXCursor UNUSED, CXClientData client_data) {
        enum CXChildVisitResult next_action = CXChildVisit_Recurse;

        ASTContext *ctx = static_cast<ASTContext*>(client_data);
        if (ctx->constraint == IFCONDITION) {
                ctx->constraint = UNCONSTRAINED;

                // check if we're comparing against a mavlink frame
                if (clang_getCursorKind(cursor) == CXCursor_BinaryOperator
                    && get_binary_operator(cursor) == "==") {
                        clang_visitChildren(cursor, check_mavlink, client_data);
                }

                return CXChildVisit_Continue;
        } else if (ctx->constraint == SWITCHSTMT) {
                // this indicates the cursor is the control expression of a switch statement.
                // we'll visit this control expression and see if it's on a mavlink field.
                ctx->constraint = UNCONSTRAINED;
                clang_visitChildren(cursor, check_mavlink, client_data);
                return CXChildVisit_Break;
        }

        // find all if statements that constrain mavlink messages
        CXCursorKind kind = clang_getCursorKind(cursor);
        if (kind == CXCursor_IfStmt) {
                // cout << "IF_STMT" << endl;
                ctx->constraint = IFCONDITION;
                
                // Set up a new scope for the body of the if statement.
                // Subsequent definitions will affect the if statement's scope.
                // The winning definition (e.g. last stores in if)
                // are propagated to the current scope.
                map<string, TypeInfo> if_var_types;
                ctx->var_types.push_back(if_var_types);

                // Use the new scope.
                clang_visitChildren(cursor, function_ast_walker, client_data);

                // Unify scopes.
                unify_scopes(ctx->var_types[ctx->var_types.size() - 1], ctx->var_types.back());

                // Destroy latest scope.
                ctx->var_types.pop_back();

                // We've already looked at the child of the if statement.
                // Time to move on.
                next_action = CXChildVisit_Continue;
                // cout << "DONE IF" << endl;
        } else if (kind == CXCursor_ForStmt || kind == CXCursor_WhileStmt) {
                // cout << "FOR/WHILE" << endl;

                // Set up a new scope for the body of the loop statement.
                // Subsequent definitions will affect the loop statement's scope.
                // The winning definitions (e.g. last stores in the loop)
                // are propagated to the current scope.
                map<string, TypeInfo> loop_var_types;
                ctx->var_types.push_back(loop_var_types);

                // Use the new scope.
                clang_visitChildren(cursor, function_ast_walker, client_data);

                // Unify scopes.
                unify_scopes(ctx->var_types[ctx->var_types.size() - 1], ctx->var_types.back());

                // Destroy latest scope.
                ctx->var_types.pop_back();

                // We've already looked at the child of the loop statement.
                // Time to move on.
                next_action = CXChildVisit_Continue;
                // cout << "DONE FOR/WHILE" << endl;
        } else if (kind == CXCursor_BreakStmt) {
                // cout << "BREAK" << endl;
                // A break causes an instant unification of the type information.
                unify_scopes(ctx->var_types[ctx->var_types.size() - 2], ctx->var_types.back());
                // cout << "DONE BREAK" << endl;
        } else if (kind == CXCursor_SwitchStmt) {
                // cout << "SWITCH" << endl;

                ctx->constraint = SWITCHSTMT;
                clang_visitChildren(cursor, function_ast_walker, client_data);
                if (ctx->in_mav_constraint) {
                        cout << "Found a MAVLink frame switch!" << endl;
                }

                // Set up a new scope for the body of the switch statement.
                // Subsequent definitions will affect the switch statement's scope.
                // The winning definitions (e.g. last stores in the switch)
                // are propagated to the current scope.
                map<string, TypeInfo> switch_var_types;
                ctx->var_types.push_back(switch_var_types);

                // Use the new scope.
                clang_visitChildren(cursor, function_ast_walker, client_data);

                // Unify scopes.
                unify_scopes(ctx->var_types[ctx->var_types.size() - 1], ctx->var_types.back());

                // Destroy latest scope.
                ctx->var_types.pop_back();

                // We've already looked at the child of the switch statement.
                // Time to move on.
                next_action = CXChildVisit_Continue;
                // cout << "DONE SWITCH" << endl;
        } else if (kind == CXCursor_VarDecl) {
                // cout << "considering var: " << get_cursor_spelling(cursor) << endl;
                // cout << "SWITCH" << endl;
                check_tainted_decl(cursor, ctx);
                // cout << "DONE SWITCH" << endl;
                // TODO: use taint information
        } else if (kind == CXCursor_BinaryOperator) {
                // cout << "op=" << endl;
                string op = get_binary_operator(cursor);
                if (op == "=") {
                        check_tainted_store(cursor, ctx);
                        // TODO: use taint information
                }
                // cout << "done op=" << endl;
        } else if (kind == CXCursor_CallExpr) {
                // cout << "call expr" << endl;
                string spelling = get_cursor_spelling(cursor);
                if (spelling == "operator=") {
                        check_tainted_store(cursor, ctx);
                        // TODO: use taint information
                } else {
                        string this_fn = ctx->current_fn;
                        ctx->fn_summary[this_fn].callees.insert(spelling);
                }
                // cout << "done call" << endl;
        } else if (kind == CXCursor_ParmDecl) {
                // cout << "parm decl" << endl;
                CXType t = clang_getCursorType(cursor);
                string t_type = get_object_typename(t);
                bool is_mav_type = ctx->types_to_frame_field.find(t_type)
                        != ctx->types_to_frame_field.end()
                        || ctx->type_to_field_to_unit.find(t_type)
                        != ctx->type_to_field_to_unit.end();
                string param_name = get_cursor_spelling(cursor);
                ctx->param_to_number[param_name] = ctx->total_params;                
                if (is_mav_type) {
                        TypeSource source = {SOURCE_PARAM, ctx->total_params, ""};
                        add_inner_vars(t_type,
                                       param_name,
                                       ctx->type_to_field_to_unit,
                                       source,
                                       ctx->var_types.back());
                } else {
                        ctx->current_fn_params.insert(param_name);
                }
                ctx->total_params++;
                // cout << "done parm decl" << endl;
        }

        return next_action;
}

enum CXChildVisitResult ast_walker(CXCursor cursor, CXCursor UNUSED, CXClientData client_data) {
        CXCursorKind kind = clang_getCursorKind(cursor);
        if (kind == CXCursor_FunctionDecl || kind == CXCursor_CXXMethod) {
                // cout << "In: " << get_USR(cursor) << endl;

                ASTContext *ctx = static_cast<ASTContext*>(client_data);
                ctx->had_mav_constraint = false;
                ctx->had_taint = false;
                ctx->current_fn = get_cursor_spelling(cursor);

                map<string, TypeInfo> scope;
                ctx->var_types.push_back(scope);

                clang_visitChildren(cursor, function_ast_walker, client_data);
                ctx->scope_to_tainted.erase(clang_hashCursor(cursor));

                if (ctx->had_taint && !ctx->had_mav_constraint) {
                        CXString cursor_spelling = clang_getCursorSpelling(cursor);
                        cout << "unconstrained MAV frame used in: "
                             << clang_getCString(cursor_spelling) << endl;
                        clang_disposeString(cursor_spelling);
                }

                // cout << get_cursor_spelling(cursor) << endl;
                // for (const auto &param: ctx->current_fn_params)
                //         cout << "  " << param << endl;
                // cout << endl;

                // const auto &summary = ctx->fn_summary[ctx->current_fn];
                // for (const auto &callee: summary.callees)
                //         cout << "  " << callee << endl;

                // cout << get_cursor_spelling(cursor) << endl;
                // for (const auto &p: ctx->var_types.back()) {
                //         cout << "  " << p.first << ":" << endl;
                //         for (const auto &src: p.second.source)
                //                 cout << "    " << src.kind
                //                      << " " << src.param_no << endl;
                // }

                // clean up this function's mess
                ctx->var_types.pop_back();
                ctx->current_fn_params.clear();
                ctx->param_to_number.clear();
                ctx->total_params = 0;
                return CXChildVisit_Continue;
        }
        return CXChildVisit_Recurse;
}

int main(int argc, char **argv) {
        if (argc != 3) {
                cerr << "usage: " << argv[0] << " [compilation database directory] [message definitions]" << endl;
                exit(1);
        }

        pugi::xml_document doc;
        ifstream xml_in(argv[2]);
        if (!doc.load(xml_in)) {
                cerr << "error: cannot load xml" << endl;
                exit(1);
        }

        map<string, string> type_to_semantic = get_types_to_frame_field(doc);
        // for (const auto &p: type_to_semantic)
        //         cout << p.first << endl;

        int num_units;
        map<string, int> unitname_to_id;
        map<string, map<string, int>> type_to_field_to_unit =
                get_type_to_field_to_unit(doc, unitname_to_id, num_units);

        // for (const auto &p: type_to_field_to_unit)
        //         cout << p.first << endl;

        // exit(0);

        // (1) load database
        CXCompilationDatabase_Error err;
        CXCompilationDatabase cdatabase = clang_CompilationDatabase_fromDirectory(argv[1], &err);
        if (err != CXCompilationDatabase_NoError) {
                cerr << "error: cannot load database" << endl;
                exit(1);
        }

        // (2) get compilation commands
        CXCompileCommands cmds = clang_CompilationDatabase_getAllCompileCommands(cdatabase);

        map<string, FunctionSummary> fn_summary;

        // (3) search each file in the compilation commands for mavlink messages
        unsigned num_cmds = clang_CompileCommands_getSize(cmds);
        CXIndex index = clang_createIndex(0, 0);
        for (auto i = 0u; i < num_cmds; i++) {
                CXCompileCommand cmd = clang_CompileCommands_getCommand(cmds, i);

                // (3 a) print the filename that was compiled by this command
                CXString filename = clang_CompileCommand_getFilename(cmd);
                CXString compile_dir = clang_CompileCommand_getDirectory(cmd);

                // if (strcmp(clang_getCString(filename), "../../libraries/GCS_MAVLink/GCS_Common.cpp") != 0) {
                //         clang_disposeString(filename);
                //         clang_disposeString(compile_dir);
                //         continue;
                // }
                cout << clang_getCString(filename) << endl;

                // (3 b) build a translation unit from the compilation command
                // (3 b i) collect arguments
                unsigned num_args = clang_CompileCommand_getNumArgs(cmd);
                vector<CXString> args;
                const char **cmd_argv = new const char*[num_args];
                for (auto j = 0u; j < num_args; j++) {
                        args.push_back(clang_CompileCommand_getArg(cmd, j));
                        cmd_argv[j] = clang_getCString(args[j]);
                }

                // (3 b ii) construct translation unit
                if (chdir(clang_getCString(compile_dir))) {
                        cerr << "[WARN] unable to cd to " << clang_getCString(compile_dir) << ". skipping." << endl;
                        continue;
                }

                CXTranslationUnit unit = clang_createTranslationUnitFromSourceFile(index, nullptr,
                                                                                   num_args, cmd_argv, 0, nullptr);

                set<string> possible_frames;
                map<unsigned, set<string>> scope_to_tainted;
                vector<map<string, TypeInfo>> var_types;
                set<string> current_fn_params;
                map<string, int> param_to_number;
                ASTContext ctx = {
                        type_to_semantic,
                        type_to_field_to_unit,
                        num_units,
                        UNCONSTRAINED,
                        possible_frames,
                        false,
                        scope_to_tainted,
                        false,
                        false,
                        var_types,
                        fn_summary,
                        "",
                        current_fn_params,
                        param_to_number,
                        0,
                };
                if (unit) {
                        CXCursor cursor = clang_getTranslationUnitCursor(unit);
                        clang_visitChildren(cursor, ast_walker, &ctx);
                } else {
                        cerr << "[WARN] error building translation unit for "
                             << get_full_path(compile_dir, filename) << ". (" << err << "). skipping." << endl;                        
                }

                // free memory from (3 b ii)
                clang_disposeTranslationUnit(unit);

                // free memory from (3 b i)
                delete[] cmd_argv;
                for (auto str: args)
                        clang_disposeString(str);

                // free memory from (3 b a)
                clang_disposeString(filename);
                clang_disposeString(compile_dir);
        }

        clang_disposeIndex(index);
        clang_CompileCommands_dispose(cmds);
        clang_CompilationDatabase_dispose(cdatabase);
}
