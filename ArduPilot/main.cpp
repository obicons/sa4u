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

#define UNUSED /* UNUSED */

enum ConstraintType {
        UNCONSTRAINED,
        IFCONDITION,
        SWITCHSTMT,
};

struct TypeInfo {
        set<int> frames;
        set<int> units;
};

struct ASTContext {
        // maps each mavlink struct name to its frame field
        const map<string, string> &types_to_frame_field;

        // maps each mavlink struct name to a map relating fields to units
        const map<string, map<string, int>> &type_to_field_to_unit;

        // communicates context to AST walkers
        ConstraintType constraint;
        set<string> &possible_frames;
        bool in_mav_constraint;
        map<unsigned, set<string>> &scope_to_tainted;
        bool had_mav_constraint;
        bool had_taint;

        // maps variables to their type info
        vector<map<string, TypeInfo>> &var_types;
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

// returns the cursor's spelling as a string
string get_cursor_spelling(CXCursor c) {
        CXString spelling = clang_getCursorSpelling(c);
        string result(clang_getCString(spelling));
        clang_disposeString(spelling);
        return result;
}

enum CXChildVisitResult check_tainted_decl_walker(CXCursor c, CXCursor p, CXClientData cd) {
        ASTContext *ctx = static_cast<ASTContext*>(cd);
        CXCursorKind kind = clang_getCursorKind(c);
        if (kind == CXCursor_DeclRefExpr) {
                CXType type = clang_getCursorType(c);
                string type_str = get_object_typename(type);

                CXCursor ref_def = clang_getCursorDefinition(c);
                CXCursor ref_def_parent = clang_getCursorSemanticParent(ref_def);
                unsigned hash = clang_hashCursor(ref_def_parent);

                CXString cursor_spelling = clang_getCursorSpelling(c);
                string cursor_name(clang_getCString(cursor_spelling));
                clang_disposeString(cursor_spelling);

                const set<string> &taint_set = ctx->scope_to_tainted[hash];

                bool has_known_type = ctx->types_to_frame_field.find(type_str) != ctx->types_to_frame_field.end();
                if (has_known_type || taint_set.find(cursor_name) != taint_set.end()) {
                        CXCursor parent = p;
                        while (clang_getCursorKind(parent) != CXCursor_VarDecl) {
                                parent = clang_getCursorSemanticParent(parent);
                        }

                        CXString parent_spelling = clang_getCursorSpelling(parent);
                        cout << "TAINTED: " << pretty_print_store(p) << endl;
                        ctx->had_taint = true;

                        CXCursor definition = clang_getCursorDefinition(parent);
                        CXCursor definition_parent = clang_getCursorSemanticParent(definition);
                        unsigned hash = clang_hashCursor(definition_parent);
                        ctx->scope_to_tainted[hash].insert(string(clang_getCString(parent_spelling)));

                        clang_disposeString(parent_spelling);
                }

                return CXChildVisit_Break;
        } else if (kind == CXCursor_MemberRefExpr) {
                string ref = pretty_print_memberRefExpr(c);
                optional<TypeInfo> ti = get_var_typeinfo(ref, ctx->var_types);
                if (ti.has_value()) {
                        cout << "I FOUND A DECLARATION WITH A KNOWN TYPE!!!" << endl;
                        CXCursor parent = p;
                        while (clang_getCursorKind(parent) != CXCursor_VarDecl) {
                                parent = clang_getCursorSemanticParent(parent);
                        }

                        if (!ctx->var_types.empty()) {
                                ctx->var_types.back()[get_cursor_spelling(parent)] = ti.value();
                        }
                }

                // TODO: CXChildVisit_Break before returning
        }
        return CXChildVisit_Recurse;
}

/**
 * t - a known type
 * name - variable name
 * type_to_field_to_unit - relates types to fields to units
 * tinfo - type info
 */
void add_inner_vars(const string &t,
                    const string &name,
                    const map<string, map<string, int>> &type_to_field_to_unit,
                    map<string, TypeInfo> &tinfo) {
        auto typeinfo = type_to_field_to_unit.find(t);
        if (typeinfo == type_to_field_to_unit.end())
                return;
        for (const auto &pair: typeinfo->second) {
                string varname = name + "." + pair.first;
                tinfo[varname].units.insert(pair.second);
        }
}

// Checks if cursor stores a mavlink message field into a variable declaration
void check_tainted_decl(CXCursor cursor, ASTContext *ctx) {
        string cursor_typename = get_object_typename(clang_getCursorType(cursor));
        bool is_known_type = ctx->types_to_frame_field.find(cursor_typename)
                != ctx->types_to_frame_field.end();
        if (is_known_type && !ctx->var_types.empty()) {
                add_inner_vars(cursor_typename, get_cursor_spelling(cursor),
                               ctx->type_to_field_to_unit, ctx->var_types.back());
        } else {
                clang_visitChildren(cursor, check_tainted_decl_walker, ctx);
        }
}

// Marks the lhs of the binary operator as tainted
enum CXChildVisitResult mark_store_tainted(CXCursor c, CXCursor UNUSED, CXClientData cd) {
        ASTContext *ctx = static_cast<ASTContext*>(cd);
        if (clang_getCursorKind(c) == CXCursor_DeclRefExpr) {
                CXString cursor_spelling = clang_getCursorSpelling(c);

                cout << "TAINTED " << clang_getCString(cursor_spelling) << endl;
                ctx->had_taint = true;

                CXCursor definition = clang_getCursorDefinition(c);
                CXCursor definition_parent = clang_getCursorSemanticParent(definition);

                unsigned hash = clang_hashCursor(definition_parent);
                ctx->scope_to_tainted[hash].insert(string(clang_getCString(cursor_spelling)));

                clang_disposeString(cursor_spelling);
                return CXChildVisit_Break;
        }
        return CXChildVisit_Recurse;
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

// Checks if cursor stores (op =) a mavlink message field into another object
void check_tainted_store(CXCursor cursor, ASTContext *ctx) {
        pair<optional<TypeInfo>, ASTContext*> p({}, ctx);
        ctaw_childno = 0;
        clang_visitChildren(cursor, check_tainted_assgn_walker, &p);
        if (p.first && !ctx->var_types.empty()) {
                string varname = pretty_print_store(cursor);
                cout << "Typed store to " << varname << " detected" << endl;
                ctx->var_types.back()[varname] = p.first.value();
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
                ctx->constraint = IFCONDITION;
                clang_visitChildren(cursor, function_ast_walker, client_data);
                next_action = CXChildVisit_Continue;
        } else if (kind == CXCursor_SwitchStmt) {
                ctx->constraint = SWITCHSTMT;
                clang_visitChildren(cursor, function_ast_walker, client_data);
                if (ctx->in_mav_constraint) {
                        cout << "Found a MAVLink frame switch!" << endl;
                }
                next_action = CXChildVisit_Recurse;
        } else if (kind == CXCursor_VarDecl) {
                // cout << "considering var: " << get_cursor_spelling(cursor) << endl;
                check_tainted_decl(cursor, ctx);
                // TODO: use taint information
        } else if (kind == CXCursor_BinaryOperator) {
                string op = get_binary_operator(cursor);
                if (op == "=") {
                        check_tainted_store(cursor, ctx);
                        // TODO: use taint information
                }
        } else if (kind == CXCursor_CallExpr) {
                CXString cspelling = clang_getCursorSpelling(cursor);
                if (strcmp(clang_getCString(cspelling), "operator=") == 0) {
                        check_tainted_store(cursor, ctx);
                        // TODO: use taint information
                }
                clang_disposeString(cspelling);
        } else if (kind == CXCursor_ParmDecl) {
                CXType t = clang_getCursorType(cursor);
                string t_type = get_object_typename(t);
                bool is_mav_type = ctx->types_to_frame_field.find(t_type)
                        != ctx->types_to_frame_field.end()
                        || ctx->type_to_field_to_unit.find(t_type)
                        != ctx->type_to_field_to_unit.end();
                // cout << "  fields: " << endl;
                // for (const auto &p: ctx->types_to_frame_field)
                //         cout << "    " << p.first << " " << (trim(p.first) == trim(t_type)) << endl;
                // cout << endl;
                // cout << t_type << " " << get_cursor_spelling(cursor) << " " << is_mav_type << endl;
                if (is_mav_type) {
                        add_inner_vars(t_type,
                                       get_cursor_spelling(cursor),
                                       ctx->type_to_field_to_unit,
                                       ctx->var_types.back());
                }
        }

        return next_action;
}

enum CXChildVisitResult ast_walker(CXCursor cursor, CXCursor UNUSED, CXClientData client_data) {
        CXCursorKind kind = clang_getCursorKind(cursor);
        if (kind == CXCursor_FunctionDecl || kind == CXCursor_CXXMethod) {
                cout << "In: " << get_cursor_spelling(cursor) << endl;

                ASTContext *ctx = static_cast<ASTContext*>(client_data);
                ctx->had_mav_constraint = false;
                ctx->had_taint = false;

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

                ctx->var_types.pop_back();

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

        map<string, int> unitname_to_id;
        map<string, map<string, int>> type_to_field_to_unit =
                get_type_to_field_to_unit(doc, unitname_to_id);

        // (1) load database
        CXCompilationDatabase_Error err;
        CXCompilationDatabase cdatabase = clang_CompilationDatabase_fromDirectory(argv[1], &err);
        if (err != CXCompilationDatabase_NoError) {
                cerr << "error: cannot load database" << endl;
                exit(1);
        }

        // (2) get compilation commands
        CXCompileCommands cmds = clang_CompilationDatabase_getAllCompileCommands(cdatabase);

        // (3) search each file in the compilation commands for mavlink messages
        unsigned num_cmds = clang_CompileCommands_getSize(cmds);
        CXIndex index = clang_createIndex(0, 0);
        for (auto i = 0u; i < num_cmds; i++) {
                CXCompileCommand cmd = clang_CompileCommands_getCommand(cmds, i);

                // (3 a) print the filename that was compiled by this command
                CXString filename = clang_CompileCommand_getFilename(cmd);
                CXString compile_dir = clang_CompileCommand_getDirectory(cmd);

                if (strcmp(clang_getCString(filename), "../../libraries/AP_Mission/AP_Mission.cpp") != 0) {
                        clang_disposeString(filename);
                        clang_disposeString(compile_dir);
                        continue;
                }
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
                ASTContext ctx = {
                        type_to_semantic,
                        type_to_field_to_unit,
                        UNCONSTRAINED,
                        possible_frames,
                        false,
                        scope_to_tainted,
                        false,
                        false,
                        var_types,
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
