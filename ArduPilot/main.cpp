#include <algorithm>
#include <cctype>
#include <cstring>
#include <iostream>
#include <fstream>
#include <map>
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

#define UNUSED /* UNUSED */

enum ConstraintType {
        UNCONSTRAINED,
        IFCONDITION,
        SWITCHSTMT,
};

struct TypeInfo {
        set<int> &frames;
        set<string> &units;
};

struct ASTContext {
        const map<string, string> &types_to_frame_field;
        const map<string, map<string, int>> &type_to_field_to_unit;
        ConstraintType constraint;
        set<string> &possible_frames;
        bool in_mav_constraint;
        map<unsigned, set<string>> &scope_to_tainted;
        bool had_mav_constraint;
        bool had_taint;
};

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
        CXString type_spelling = clang_getTypeSpelling(type);
        string result(clang_getCString(type_spelling));
        clang_disposeString(type_spelling);
        if (clang_isConstQualifiedType(type)) {
                const string const_prefix = "const ";
                result = result.substr(const_prefix.length(), result.length() - const_prefix.length());
        }
        // TODO: make this function less wrong
        return result;
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

                const auto& pair = ctx->types_to_frame_field.find(the_type);
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

enum CXChildVisitResult check_tainted_decl_walker(CXCursor c, CXCursor p, CXClientData cd) {
        ASTContext *ctx = static_cast<ASTContext*>(cd);
        if (clang_getCursorKind(c) == CXCursor_DeclRefExpr) {
                CXType type = clang_getCursorType(c);
                string type_str = get_object_typename(type);

                CXCursor ref_def = clang_getCursorDefinition(c);
                CXCursor ref_def_parent = clang_getCursorSemanticParent(ref_def);
                unsigned hash = clang_hashCursor(ref_def_parent);

                CXString cursor_spelling = clang_getCursorSpelling(c);
                string cursor_name(clang_getCString(cursor_spelling));
                clang_disposeString(cursor_spelling);

                const set<string> &taint_set = ctx->scope_to_tainted[hash];

                if (ctx->types_to_frame_field.find(type_str) != ctx->types_to_frame_field.end()
                    || taint_set.find(cursor_name) != taint_set.end()) {
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
        }
        return CXChildVisit_Recurse;
}

// Checks if cursor stores a mavlink message field into a variable declaration
void check_tainted_decl(CXCursor cursor, ASTContext *ctx) {
        clang_visitChildren(cursor, check_tainted_decl_walker, ctx);
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
enum CXChildVisitResult check_tainted_assgn_walker(CXCursor c, CXCursor parent, CXClientData cd) {
        if (!ctaw_childno) {
                ctaw_childno++;
                return CXChildVisit_Continue;
        }

        CXType type = clang_getCursorType(c);
        string type_str = get_object_typename(type);
        pair<bool, ASTContext*> *p = static_cast<pair<bool, ASTContext*>*>(cd);

        bool tainted_indirectly = false;
        CXCursorKind kind = clang_getCursorKind(c);
        if (kind == CXCursor_DeclRefExpr) {
                CXCursor definition = clang_getCursorDefinition(c);
                CXCursor definition_parent = clang_getCursorSemanticParent(definition);
                unsigned hash = clang_hashCursor(definition_parent);

                CXString spelling = clang_getCursorSpelling(c);
                string cursor_name = string(clang_getCString(spelling));
                clang_disposeString(spelling);

                const set<string> &tainted_set = p->second->scope_to_tainted[hash];
                tainted_indirectly = tainted_set.find(cursor_name) != tainted_set.end();
        } else if (kind == CXCursor_MemberRef) {
                CXCursor definition = clang_getCursorDefinition(c);
                CXCursor definition_parent = clang_getCursorSemanticParent(definition);
                unsigned hash = clang_hashCursor(definition_parent);

                string cursor_name = pretty_print_store(parent);
                cout << "HERE: " << cursor_name << endl;

                const set<string> &tainted_set = p->second->scope_to_tainted[hash];
                tainted_indirectly = tainted_set.find(cursor_name) != tainted_set.end();
        }

        if (tainted_indirectly
            || p->second->types_to_frame_field.find(type_str) != p->second->types_to_frame_field.end()) {
                p->first = true;
                return CXChildVisit_Break;
        }

        return CXChildVisit_Recurse;
}

// Checks if cursor stores (op =) a mavlink message field into another object
void check_tainted_store(CXCursor cursor, ASTContext *ctx) {
        pair<bool, ASTContext*> p(false, ctx);
        ctaw_childno = 0;
        clang_visitChildren(cursor, check_tainted_assgn_walker, &p);
        if (p.first) {
                clang_visitChildren(
                        cursor,
                        [](CXCursor c, CXCursor p, CXClientData cd) {
                                ASTContext *ctx = static_cast<ASTContext*>(cd);
                                if (clang_getCursorKind(c) == CXCursor_DeclRefExpr) {
                                        CXString cursor_spelling = clang_getCursorSpelling(c);
                                        cout << "TAINTED: " << clang_getCString(cursor_spelling) << endl;
                                        ctx->had_taint = true;

                                        CXCursor definition = clang_getCursorDefinition(c);
                                        CXCursor definition_parent = clang_getCursorSemanticParent(definition);

                                        unsigned hash = clang_hashCursor(definition_parent);
                                        ctx->scope_to_tainted[hash].insert(string(clang_getCString(cursor_spelling)));

                                        clang_disposeString(cursor_spelling);
                                } else {
                                        CXCursor definition = clang_getCursorDefinition(c);
                                        CXCursor definition_parent = clang_getCursorSemanticParent(definition);
                                        unsigned hash = clang_hashCursor(definition_parent);
                                        cout << "MARKING: " << pretty_print_store(p) << endl;
                                        ctx->scope_to_tainted[hash].insert(pretty_print_store(p));
                                        clang_visitChildren(c, mark_store_tainted, cd);
                                }
                                return CXChildVisit_Break;
                        },
                        ctx
                );
        }
}

enum CXChildVisitResult function_ast_walker(CXCursor cursor, CXCursor parent, CXClientData client_data) {
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
        }

        return next_action;
}

enum CXChildVisitResult ast_walker(CXCursor cursor, CXCursor parent, CXClientData client_data) {
        CXCursorKind kind = clang_getCursorKind(cursor);
        if (kind == CXCursor_FunctionDecl || kind == CXCursor_CXXMethod) {
                ASTContext *ctx = static_cast<ASTContext*>(client_data);
                ctx->had_mav_constraint = false;
                ctx->had_taint = false;

                clang_visitChildren(cursor, function_ast_walker, client_data);
                ctx->scope_to_tainted.erase(clang_hashCursor(cursor));

                if (ctx->had_taint && !ctx->had_mav_constraint) {
                        CXString cursor_spelling = clang_getCursorSpelling(cursor);
                        cout << "unconstrained MAV frame used in: "
                             << clang_getCString(cursor_spelling) << endl;
                        clang_disposeString(cursor_spelling);
                }

                return CXChildVisit_Continue;
        }
        return CXChildVisit_Recurse;
}

// returns the internal type that will be used to represent the mavlink message with name
string mavlink_msgname_to_typename(const string &msgname) {
        string result = msgname;
        transform(result.begin(), result.end(), result.begin(), [](unsigned char c){ return std::tolower(c); });
        result = "mavlink_" + result + "_t";
        return result;
}

// returns a map relating mavlink message types to their frame field
map<string, string> get_types_to_frame_field(const pugi::xml_document &doc) {
        map<string, string> result;
        for (const pugi::xml_node &msg: doc.child("mavlink").child("messages")) {
                pugi::xml_attribute node_type = msg.attribute("name");
                if (node_type) {
                        bool frame_member = false;
                        string frame_member_name;
                        for (const pugi::xml_node &param: msg) {
                                // check if there's a frame member
                                if (param.type() == pugi::node_element &&
                                    strcmp(param.name(), "field") == 0 &&
                                    strcmp(param.attribute("enum").value(), "MAV_FRAME") == 0) {
                                        frame_member_name = string(param.attribute("name").value());
                                        frame_member = true;
                                        break;
                                }
                        }

                        if (frame_member) {
                                // use the name of the message to generate the c++ struct name
                                string structname = mavlink_msgname_to_typename(node_type.value());
                                result[structname] = frame_member_name;
                        }
                }
        }

        return result;
}

// returns a map relating mavlink message types to their fields to their unit kinds
map<string, map<string, int>> get_type_to_field_to_unit(const pugi::xml_document &doc,
                                                        map<string, int> &unitname_to_id) {
        int nextid = 0;
        map<string, map<string, int>> type_to_field_to_unit;
        for (const pugi::xml_node &msg: doc.child("mavlink").child("messages")) {
                pugi::xml_attribute msg_name = msg.attribute("name");
                if (msg_name) {
                        string structname = mavlink_msgname_to_typename(msg_name.value());
                        for (const pugi::xml_node &param: msg) {
                                if (param.type() == pugi::node_element && param.attribute("units")) {
                                        string unit_type = param.attribute("units").value();

                                        // see if we do not have an ID for this unit type already.
                                        // if we don't, create one.
                                        if (unitname_to_id.find(unit_type) == unitname_to_id.end()) {
                                                unitname_to_id[unit_type] = nextid++;
                                        }

                                        string field_name(param.attribute("name").value());
                                        type_to_field_to_unit[structname][field_name] = unitname_to_id[unit_type];
                                }
                        }
                }
        }
        return type_to_field_to_unit;
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

                // if (strcmp(clang_getCString(filename), "../../ArduCopter/GCS_Mavlink.cpp") != 0) {
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
                ASTContext ctx = {
                        type_to_semantic,
                        type_to_field_to_unit,
                        UNCONSTRAINED,
                        possible_frames,
                        false,
                        scope_to_tainted,
                        false,
                        false,
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
