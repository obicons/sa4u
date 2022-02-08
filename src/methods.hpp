#pragma once

#include <string>

extern "C" {
#include <clang-c/Index.h>
}

/*
 * Returns the fully qualified name of the method called at cursor.
 * Pre:
 *   cursor is a CXCursor_CallExpr.
 */
std::string get_fq_method(CXCursor cursor);