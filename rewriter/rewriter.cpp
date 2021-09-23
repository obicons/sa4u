#include <iostream>
#include <sstream>
#include <string>

#include "clang/AST/AST.h"
#include "clang/AST/ASTConsumer.h"
#include "clang/AST/RecursiveASTVisitor.h"
#include "clang/Frontend/ASTConsumers.h"
#include "clang/Frontend/CompilerInstance.h"
#include "clang/Frontend/FrontendActions.h"
#include "clang/Rewrite/Core/Rewriter.h"
#include "clang/Tooling/CommonOptionsParser.h"
#include "clang/Tooling/Tooling.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Support/CommandLine.h"

using namespace clang;
using namespace clang::driver;
using namespace clang::tooling;
using namespace llvm;

static cl::OptionCategory tool_category("rewriter");

class RewritingVisitor : public RecursiveASTVisitor<RewritingVisitor> {
public:
  RewritingVisitor(Rewriter &R) : TheRewriter(R), compound_stmt_depth(0) {}

  bool dataTraverseStmtPost(Stmt *s) {
    if (isa<CompoundStmt>(s) && --compound_stmt_depth == 0)
      std::cout << "done with function " << function_name << std::endl;
    return true;
  }

  bool VisitStmt(Stmt *s) {
    // Only care about If statements.
    if (isa<IfStmt>(s)) {
      IfStmt *IfStatement = cast<IfStmt>(s);
      Stmt *Then = IfStatement->getThen();

      TheRewriter.InsertText(Then->getBeginLoc(), "// the 'if' part\n", true,
                             true);

      Stmt *Else = IfStatement->getElse();
      if (Else)
        TheRewriter.InsertText(Else->getBeginLoc(), "// the 'else' part\n",
                               true, true);
    } else if (isa<CompoundStmt>(s)) {
      compound_stmt_depth++;
    }

    return true;
  }

  bool VisitFunctionDecl(FunctionDecl *f) {
    // Only function definitions (with bodies), not declarations.
    if (f->hasBody()) {
      Stmt *FuncBody = f->getBody();

      function_name = f->getNameInfo().getName().getAsString();

      // Type name as string
      QualType QT = f->getReturnType();
      std::string TypeStr = QT.getAsString();

      // Function name
      DeclarationName DeclName = f->getNameInfo().getName();
      std::string FuncName = DeclName.getAsString();

      // Add comment before
      std::stringstream SSBefore;
      SSBefore << "// Begin function " << FuncName << " returning " << TypeStr
               << "\n";
      SourceLocation ST = f->getSourceRange().getBegin();
      TheRewriter.InsertText(ST, SSBefore.str(), true, true);

      // And after
      std::stringstream SSAfter;
      SSAfter << "\n// End function " << FuncName;
      ST = FuncBody->getEndLoc().getLocWithOffset(1);
      TheRewriter.InsertText(ST, SSAfter.str(), true, true);
    }

    return true;
  }

private:
  Rewriter &TheRewriter;
  unsigned compound_stmt_depth;
  std::string function_name;
};

// Implementation of the ASTConsumer interface for reading an AST produced
// by the Clang parser.
class MyASTConsumer : public ASTConsumer {
public:
  MyASTConsumer(Rewriter &R) : Visitor(R) {}

  // Override the method that gets called for each parsed top-level
  // declaration.
  bool HandleTopLevelDecl(DeclGroupRef DR) override {
    for (DeclGroupRef::iterator b = DR.begin(), e = DR.end(); b != e; ++b) {
      // Traverse the declaration using our AST visitor.
      Visitor.TraverseDecl(*b);
      //(*b)->dump();
    }
    return true;
  }

private:
  RewritingVisitor Visitor;
};

// For each source file provided to the tool, a new FrontendAction is created.
class MyFrontendAction : public ASTFrontendAction {
public:
  MyFrontendAction() {}
  void EndSourceFileAction() override {
    SourceManager &SM = TheRewriter.getSourceMgr();

    // Now emit the rewritten buffer.
    //TheRewriter.getEditBuffer(SM.getMainFileID()).write(llvm::outs());
    TheRewriter.overwriteChangedFiles();
  }

  std::unique_ptr<ASTConsumer> CreateASTConsumer(CompilerInstance &CI,
                                                 StringRef file) override {
    TheRewriter.setSourceMgr(CI.getSourceManager(), CI.getLangOpts());
    return std::make_unique<MyASTConsumer>(TheRewriter);
  }

private:
  Rewriter TheRewriter;
};

int main(int argc, const char **argv) {
  std::string msg = "cannot load compilation database";
  auto compilation_database = CompilationDatabase::loadFromDirectory(argv[1], msg);
  if (!compilation_database) {
	  llvm::errs() << "HERE cannot load compilation database\n";
	  return 1;
  }

  ClangTool Tool(*compilation_database, ArrayRef<std::string>(compilation_database->getAllFiles()));

  // ClangTool::run accepts a FrontendActionFactory, which is then used to
  // create new objects implementing the FrontendAction interface. Here we use
  // the helper newFrontendActionFactory to create a default factory that will
  // return a new MyFrontendAction object every time.
  // To further customize this, we could create our own factory class.
  return Tool.run(newFrontendActionFactory<MyFrontendAction>().get());
}
