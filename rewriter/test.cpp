/**
 * g++ test.cpp -o test -I /usr/lib/llvm-14/include/ -std=c++17
 * -L/usr/lib/llvm-14/lib -lLLVM -Wl,--start-group -lclangAST -lclangASTMatchers
 * -lclangAnalysis -lclangBasic -lclangDriver -lclangEdit -lclangFrontend
 * -lclangFrontendTool -lclangLex -lclangParse -lclangSema -lclangEdit
 * -lclangRewrite -lclangRewriteFrontend -lclangStaticAnalyzerFrontend
 * -lclangStaticAnalyzerCheckers -lclangStaticAnalyzerCore -lclangCrossTU
 * -lclangIndex -lclangSerialization -lclangToolingCore -lclangTooling
 * -lclangFormat -Wl,--end-group
 */
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <optional>
#include <set>
#include <sstream>
#include <string>
#include <vector>

#include "clang/AST/AST.h"
#include "clang/AST/ASTConsumer.h"
#include "clang/AST/RecursiveASTVisitor.h"
#include "clang/Frontend/ASTConsumers.h"
#include "clang/Frontend/CompilerInstance.h"
#include "clang/Frontend/FrontendActions.h"
#include "clang/Rewrite/Core/Rewriter.h"
#include "clang/Tooling/CommonOptionsParser.h"
#include "clang/Tooling/Tooling.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/raw_ostream.h"

extern "C" {
#include <fcntl.h>
#include <unistd.h>
}

using namespace clang;
using namespace clang::driver;
using namespace clang::tooling;
using namespace llvm;

#define INTEGRAL_TYPE 0
#define FLOATING_TYPE 1

static cl::OptionCategory tool_category("rewriter");

static std::string instrumentation_function;

// Files that we've already rewritten.
static std::set<std::string> rewritten_files;

// Files that we've already inserted our instrumentation preamble into.
static std::set<std::string> files_with_preamble;

// Stores the number of bytes we've already inserted into the file.
static std::map<FileID, size_t> files_to_bytes_inserted;

// Relates variable names to their integer IDs.
static std::map<std::string, unsigned> varname_to_id;

// Returns if we own a path + can write.
static bool path_writable(const std::string &path) {
  return access(path.c_str(), W_OK) != -1;
}

class ContextTracker {
 public:
  void clear() { ctx.clear(); }

  void add_to_ctx(const std::string &str) { ctx.push_back(str); }

  void pop_ctx() {
    assert(ctx.size() > 0);
    ctx.pop_back();
  }

  std::string place_in_ctx(const std::string &id) {
    std::string qualified_id, sep;
    ctx.push_back(id);
    for (const auto &p : ctx) {
      qualified_id += sep + p;
      sep = "::";
    }
    ctx.pop_back();
    return qualified_id;
  }

  size_t size() { return ctx.size(); }

  std::string dump() const {
    std::stringstream ss;
    std::string sep = "";
    for (const auto &p : ctx) ss << sep << p;
    return ss.str();
  }

 private:
  std::vector<std::string> ctx;
};

static std::string get_member_ref_qualified(ContextTracker &tracker,
                                            MemberExpr *expr) {
  std::string result = expr->getMemberNameInfo().getName().getAsString();
  for (auto child : expr->children()) {
    if (isa<MemberExpr>(child)) {
      result = get_member_ref_qualified(tracker, cast<MemberExpr>(child)) +
               "::" + result;
    } else if (isa<CXXThisExpr>(child)) {
      result = tracker.place_in_ctx(result);
      break;
    }
  }
  return result;
}

static std::string get_instrumentation_call(int type_code,
                                            const std::string &varname,
                                            const std::string &expr) {
  static int instance_no;
  static unsigned next_varname_id;
  if (varname_to_id.find(varname) == varname_to_id.end()) {
    varname_to_id[varname] = next_varname_id++;
  }

  return "_instrument_noclash(" + std::to_string(type_code) + "," +
         std::to_string(varname_to_id[varname]) + ",(" + expr + ")," +
         std::to_string(instance_no++) + ")";
}

static void instrument_function(Rewriter &rewriter, ASTContext &ctx,
                                ContextTracker &tracker, BinaryOperator *op) {
  Expr *lhs = op->getLHS();

  assert(isa<MemberExpr>(lhs));
  assert(!lhs->refersToBitField());

  // Insert the instrumentation preamble, if necessary.
  SourceManager &sm = ctx.getSourceManager();
  if (!sm.isInMainFile(op->getExprLoc())) return;

  // We only want to instrument stores to integer, enum, and floating point
  // types.
  clang::QualType lhs_type = lhs->getType()->getCanonicalTypeUnqualified();
  if (!lhs_type.getTypePtr()) return;
  int type_code;
  if (lhs_type->isRealFloatingType())
    type_code = FLOATING_TYPE;
  else if (lhs_type->isIntegralOrEnumerationType())
    type_code = INTEGRAL_TYPE;
  else {
    return;
  }

  std::string filename(sm.getFilename(op->getBeginLoc()));
  if (files_with_preamble.find(filename) == files_with_preamble.end()) {
    files_with_preamble.insert(filename);
    FileID id = sm.getFileID(op->getExprLoc());
    SourceLocation loc = sm.getLocForStartOfFile(id);
    rewriter.InsertTextBefore(loc, instrumentation_function);
    files_to_bytes_inserted[id] += instrumentation_function.length();
  }

  // Find the location in the source code of this expression.
  std::string rhs_text;
  raw_string_ostream rhs_stream(rhs_text);
  SourceLocation rhs_begin, rhs_end;
  if (op->getRHS()->getBeginLoc().isMacroID())
    rhs_begin = sm.getFileLoc(
        sm.getImmediateExpansionRange(op->getRHS()->getBeginLoc()).getBegin());
  else
    rhs_begin = op->getRHS()->getBeginLoc();
  if (op->getRHS()->getEndLoc().isMacroID())
    rhs_end = sm.getFileLoc(
        sm.getImmediateExpansionRange(op->getRHS()->getEndLoc()).getEnd());
  else
    rhs_end = op->getRHS()->getEndLoc();
  rhs_text =
      std::string(rewriter.getRewrittenText(SourceRange(rhs_begin, rhs_end)));

  // Build the instrumentation call.
  MemberExpr *expr = cast<MemberExpr>(lhs);
  std::string lhs_qual = get_member_ref_qualified(tracker, expr);
  std::string stmt;
  raw_string_ostream stream(stmt);
  op->getLHS()->printPretty(stream, nullptr, PrintingPolicy(ctx.getLangOpts()));
  stream << "=" << rhs_text;
  std::string instrumented_assignment =
      get_instrumentation_call(type_code, lhs_qual, stmt);

  if (op->getEndLoc().isMacroID())
    rewriter.RemoveText(SourceRange(
        op->getBeginLoc(),
        sm.getFileLoc(
            sm.getImmediateExpansionRange(op->getEndLoc()).getEnd())));
  else
    rewriter.RemoveText(SourceRange(op->getBeginLoc(), op->getEndLoc()));

  rewriter.InsertText(sm.getSpellingLoc(op->getBeginLoc()),
                      instrumented_assignment);
}

class RewritingVisitor : public RecursiveASTVisitor<RewritingVisitor> {
 public:
  RewritingVisitor(Rewriter &R, ASTContext &c)
      : TheRewriter(R), ast_context(c), compound_stmt_depth(0) {}

  bool shouldTraversePostOrder() const { return true; }

  bool VisitBinaryOperator(BinaryOperator *op) {
    if (op && op->getOpcode() == BO_Assign) {
      Expr *lhs = op->getLHS();
      std::string filename(
          ast_context.getSourceManager().getFilename(op->getExprLoc()));
      if (lhs && isa<MemberExpr>(lhs) && !op->getLHS()->refersToBitField() &&
          !op->isInstantiationDependent() && path_writable(filename)) {
        instrument_function(TheRewriter, ast_context, tracker, op);
      }
    }
    return true;
  }

  void clearTracker() { tracker.clear(); }

  void add_to_tracker(const std::string &str) { tracker.add_to_ctx(str); }

 private:
  Rewriter &TheRewriter;
  unsigned compound_stmt_depth;
  std::string function_name;
  ContextTracker tracker;
  ASTContext &ast_context;
};

// Implementation of the ASTConsumer interface for reading an AST produced
// by the Clang parser.
class MyASTConsumer : public ASTConsumer {
 public:
  MyASTConsumer(Rewriter &R, ASTContext &a, const std::string &file)
      : Visitor(R, a), file(file) {}

  // Override the method that gets called for each parsed top-level
  // declaration.
  bool HandleTopLevelDecl(DeclGroupRef DR) override {
    for (DeclGroupRef::iterator b = DR.begin(), e = DR.end(); b != e; ++b) {
      // Traverse the declaration using our AST visitor.
      Visitor.clearTracker();

      auto decl = *b;
      // if (decl->isTemplated()) {
      //   continue;
      // } else
      if (isa<CXXMethodDecl>(decl)) {
        CXXMethodDecl *d = cast<CXXMethodDecl>(decl);
        if (d->getTemplateSpecializationInfo() ||
            d->getTemplateInstantiationPattern())
          continue;
        Visitor.add_to_tracker(d->getParent()->getNameAsString());
      }

      Visitor.TraverseDecl(*b);
      //(*b)->dump();
    }
    return true;
  }

 private:
  RewritingVisitor Visitor;
  std::string file;
};

// For each source file provided to the tool, a new FrontendAction is created.
class MyFrontendAction : public ASTFrontendAction {
 public:
  MyFrontendAction() {}
  void EndSourceFileAction() override {
    SourceManager &SM = TheRewriter.getSourceMgr();

    if (rewritten_files.find(the_file) == rewritten_files.end()) {
      // Now emit the rewritten buffer.
      // TheRewriter.getEditBuffer(SM.getMainFileID()).write(llvm::outs());
      TheRewriter.overwriteChangedFiles();
      rewritten_files.insert(the_file);
    }
  }

  std::unique_ptr<ASTConsumer> CreateASTConsumer(CompilerInstance &CI,
                                                 StringRef file) override {
    TheRewriter.setSourceMgr(CI.getSourceManager(), CI.getLangOpts());
    SourceManager &sm = TheRewriter.getSourceMgr();

    the_file = file;
    std::cout << "In: " << the_file << std::endl;

    if (the_file == "./../libraries/AP_Baro/AP_Baro_UAVCAN.cpp") return nullptr;

    // Insert instrumentation function
    // TheRewriter.InsertText(sm.getLocForStartOfFile(sm.getMainFileID()),
    // instrumentation_function);
    return std::make_unique<MyASTConsumer>(TheRewriter, CI.getASTContext(),
                                           the_file);
  }

 private:
  Rewriter TheRewriter;
  std::string the_file;
};

static std::optional<std::string> slurp_file(const std::string &path) {
  std::ifstream in;
  in.open(path, std::ios_base::in);
  if (!in) return {};

  std::string result, next;
  while (std::getline(in, next)) result += next + "\n";
  return result;
}

class NoOpDiagnosticConsumer : public DiagnosticConsumer {
 public:
  // NoOpDiagnosticConsumer() { }
  // virtual ~NoOpDiagnosticConsumer() {}

  // virtual void BeginSourceFile(const LangOptions &LangOpts,
  //                              const Preprocessor *PP = nullptr) {};

  // virtual void EndSourceFile() {}
  // virtual void finish() {}
  // virtual bool IncludeInDiagnosticCounts() const { return false; };
  // virtual void HandleDiagnostic(DiagnosticsEngine::Level DiagLevel,
  //                               const Diagnostic &Info);
};

static std::string homedir() {
    return getenv("HOME");
}

int main(int argc, const char **argv) {
  if (argc != 3) {
    std::cerr
        << "usage: " << argv[0]
        << " [compilation database path] [path to instrumentation source code]"
        << std::endl;
    return 1;
  }

  std::optional<std::string> instrumentation_source = slurp_file(argv[2]);
  if (!instrumentation_source) {
    std::cerr << "cannot open: " << argv[2] << std::endl;
    return 1;
  }
  instrumentation_function = instrumentation_source.value() + "\n";

  std::string msg = "cannot load compilation database";
  auto compilation_database =
      CompilationDatabase::loadFromDirectory(argv[1], msg);
  if (!compilation_database) {
    llvm::errs() << "HERE cannot load compilation database\n";
    return 1;
  }

  ClangTool Tool(*compilation_database,
                 ArrayRef<std::string>(compilation_database->getAllFiles()));
  Tool.setPrintErrorMessage(false);

  NoOpDiagnosticConsumer diagnosis_consumer;
  Tool.setDiagnosticConsumer(&diagnosis_consumer);

  // ClangTool::run accepts a FrontendActionFactory, which is then used to
  // create new objects implementing the FrontendAction interface. Here we use
  // the helper newFrontendActionFactory to create a default factory that will
  // return a new MyFrontendAction object every time.
  // To further customize this, we could create our own factory class.
  int ret = Tool.run(newFrontendActionFactory<MyFrontendAction>().get());

  std::ofstream varname(homedir() + "/variable_names.csv");
  varname << "name,id" << std::endl;
  for (const auto &it : varname_to_id) {
    varname << it.first << "," << it.second << std::endl;
  }
  varname.close();

  return ret;
}
