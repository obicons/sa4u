#include "methods.hpp"
#include "util.hpp"

using namespace std;

/*
 * Returns the type of t without pointer and const qualifiers.
 */
static CXType get_plain_type(CXType t) {
    CXType t1 = t;
    while (t1.kind == CXType_Pointer) {
        t1 = clang_getPointeeType(t1);
    }
    if (clang_isConstQualifiedType(t1) || clang_isVolatileQualifiedType(t1) || clang_isRestrictQualifiedType(t1)) {
        t1 = clang_Type_getNamedType(t1);
    }
    return t1;
}

/*
 * Returns the fully qualified name of the method called at cursor.
 * Pre:
 *   cursor is a CXCursor_CallExpr that DOES NOT refer to a virtual method.
 */
static string get_fq_non_virtual_method(CXCursor cursor) {
    CXString type_cx = clang_getTypeSpelling(get_plain_type(clang_getCanonicalType(clang_Cursor_getReceiverType(cursor))));
    string type_str = clang_getCString(type_cx);
    clang_disposeString(type_cx);

    CXString method_cx = clang_getCursorSpelling(cursor);
    string method_str = clang_getCString(method_cx);
    clang_disposeString(method_cx);

    return type_str + "::" + method_str;
}

/*
 * Returns the fully qualified name of the method called at cursor.
 * Pre:
 *   cursor is a CXCursor_CallExpr.
 */
string get_fq_method(CXCursor cursor) {
    string fq_method = "";
    if (clang_Cursor_isDynamicCall(cursor)) {
        CXCursor ref = clang_getCursorReferenced(cursor);
        CXCursor *overriden_methods = nullptr;
        unsigned num_overriden = 0;
        clang_getOverriddenCursors(ref, &overriden_methods, &num_overriden);

        // This happens when there is a call to a virtual method and the receiver is the base class.
        if (num_overriden == 0) {
            fq_method = get_fq_non_virtual_method(cursor);
        } else {
            // Use the first parent class as the class.
            // TODO: handle MULTIPLE inheritance correctly.
            CXCursor overriden_method = overriden_methods[0];
            CXCursor defining_class = clang_getCursorSemanticParent(overriden_method);
            
            CXString defining_class_spelling = clang_getTypeSpelling(clang_getCursorType(defining_class));
            string defining_class_str = clang_getCString(defining_class_spelling);
            clang_disposeString(defining_class_spelling);

            CXString cursor_spelling = clang_getCursorSpelling(cursor);
            string method_name = clang_getCString(cursor_spelling);
            clang_disposeString(cursor_spelling);

            fq_method = defining_class_str + "::" + method_name;
        }

        clang_disposeOverriddenCursors(overriden_methods);
    } else {
        fq_method = get_fq_non_virtual_method(cursor);
    }
    return fq_method;
}