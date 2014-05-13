// Display how the compiler resolved lookup and overload of specific function
//
//  Usage:
//  show-call <cmake-output-dir> <file1> <file2> ...
//
//  Where <cmake-output-dir> is a CMake build directory in which a file named
//  compile_commands.json exists (enable -DCMAKE_EXPORT_COMPILE_COMMANDS in
//  CMake to get this output).
//
//  <file1> ... specify the paths of files in the CMake source tree. This path
//  is looked up in the compile command database. If the path of a file is
//  absolute, it needs to point into CMake's source tree. If the path is
//  relative, the current working directory needs to be in the CMake source
//  tree and the file must be in a subdirectory of the current working
//  directory. "./" prefixes in the relative files will be automatically
//  removed, but the rest of a relative path must be a suffix of a path in
//  the compile command line database.
//
//  For example, to use show-call on all files in a subtree of the
//  source tree, use:
//
//    /path/in/subtree $ find . -name '*.cpp'|
//        xargs show-call /path/to/build
//
//===----------------------------------------------------------------------===//

#include "clang/ASTMatchers/ASTMatchers.h"
#include "clang/ASTMatchers/ASTMatchFinder.h"
#include "clang/Basic/Diagnostic.h"
#include "clang/Basic/SourceManager.h"
#include "clang/Frontend/FrontendActions.h"
#include "clang/Lex/Lexer.h"
#include "clang/Tooling/CompilationDatabase.h"
#include "clang/Tooling/Refactoring.h"
#include "clang/Tooling/Tooling.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/MemoryBuffer.h"
#include "llvm/Support/Signals.h"

using namespace clang;
using namespace clang::ast_matchers;
using namespace clang::tooling;
using namespace llvm;

// Set up the command line options
cl::opt<std::string> BuildPath(
  cl::Positional,
  cl::desc("<build-path>"));

cl::list<std::string> SourcePaths(
  cl::Positional,
  cl::desc("<source0> [... <sourceN>]"),
  cl::OneOrMore);

cl::opt<unsigned> CallAtLine(
  "call-at-line",
  cl::desc("Only display call(s) at this line"),
  cl::init(0));

cl::opt<bool> ShowCallAST(
  "show-call-ast",
  cl::desc("Display the AST at the call location"),
  cl::init(false));

cl::opt<bool> ShowCalleeAST(
  "show-callee-ast",
  cl::desc("Display the callee declaration AST"),
  cl::init(false));

namespace {
void getSourceInfo(const SourceManager &SM, const SourceLocation &Loc,
                   StringRef &filename, unsigned &line, unsigned &col) {
  std::pair<FileID, unsigned> LocInfo = SM.getDecomposedLoc(Loc);
  FileID FID = LocInfo.first;
  unsigned FileOffset = LocInfo.second;
  filename = SM.getFilename(Loc);
  line = SM.getLineNumber(FID, FileOffset);
  col = SM.getColumnNumber(FID, FileOffset);
}

void getSourceInfo(const SourceManager &SM, const SourceLocation &Loc,
                   StringRef &filename, unsigned &line) {
  std::pair<FileID, unsigned> LocInfo = SM.getDecomposedLoc(Loc);
  FileID FID = LocInfo.first;
  unsigned FileOffset = LocInfo.second;
  filename = SM.getFilename(Loc);
  line = SM.getLineNumber(FID, FileOffset);
}

void dumpCallInfo(const char *CallKind, const SourceManager &SM, const CallExpr *call) {

  StringRef FileName;
  unsigned LineNum, ColNum;
  getSourceInfo(SM, call->getLocStart(), FileName, LineNum, ColNum);

  if (LineNum != CallAtLine && CallAtLine != 0)
    return;

  errs() << "\n=================================\n"
         << CallKind << " call"
         << " (File:" << FileName
         << " Line:" << LineNum
         << " Col:" << ColNum << ")"
         << "\n---------------------------------\n";

  if (ShowCallAST) {
    errs() << "Call site:\n";
    call->dump();
  }

  errs() << "\nCallee:\n";
  const FunctionDecl *FD = cast<FunctionDecl>(call->getCalleeDecl());
  if (ShowCalleeAST) {
    FD->dump();
  }
  FD->printQualifiedName(errs());

  SplitQualType T_split = FD->getType().split();
  errs() << " with prototype \"" << QualType::getAsString(T_split) << '"';

  if (FD->isDefaulted()) {
    errs() << " (defaulted)\n";
  } else {
    StringRef DeclFileName;
    unsigned DeclLineNum;
    getSourceInfo(SM, FD->getLocStart(), DeclFileName, DeclLineNum);
    errs() << " declared at " << DeclFileName << ':' << DeclLineNum << '\n';
  }
}

class SCCallBack : public MatchFinder::MatchCallback {
public:
  SCCallBack(Replacements *Replace) : Replace(Replace) { }

  virtual void run(const MatchFinder::MatchResult &Result) {
    SourceManager &SM = *Result.SourceManager;

    if (const CallExpr *call =
            Result.Nodes.getNodeAs<CallExpr>("call")) {

      const char *callKind = "Function";

      if (isa<CXXMemberCallExpr>(call))
        callKind = "Member";
      else if (isa<CXXOperatorCallExpr>(call))
        callKind = "Operator";

      dumpCallInfo(callKind, SM, call);

      return;
    }

    assert(false && "Unhandled match !");
  }

  void dummy() {
    (void)Replace; // This to prevent an "unused member variable" warning;
  }

private:
  Replacements *Replace;
};
} // end anonymous namespace

int main(int argc, const char **argv) {
  llvm::sys::PrintStackTraceOnErrorSignal();

  std::unique_ptr<CompilationDatabase> Compilations(
      FixedCompilationDatabase::loadFromCommandLine(argc, argv));

  cl::ParseCommandLineOptions(argc, argv);

  if (!Compilations) { // Couldn't find a compilation DB from the command line
    std::string ErrorMessage;
    Compilations.reset(!BuildPath.empty()
                           ? CompilationDatabase::autoDetectFromDirectory(
                                 BuildPath, ErrorMessage)
                           : CompilationDatabase::autoDetectFromSource(
                                 SourcePaths[0], ErrorMessage));

    //  Still no compilation DB? - bail.
    if (!Compilations)
      llvm::report_fatal_error(ErrorMessage);
  }

  RefactoringTool Tool(*Compilations, SourcePaths);
  ast_matchers::MatchFinder Finder;
  SCCallBack Callback(&Tool.getReplacements());

  Finder.addMatcher(callExpr().bind("call"), &Callback);
  //Finder.addMatcher(
  //    callExpr(callee(methodDecl(hasName("operator=")))).bind("call"),
  //    &Callback);
  //Finder.addMatcher(
  //    memberCallExpr(on(hasType(asString("N::C *"))),
  //                   callee(methodDecl(hasName("f")))).bind("call"),
  //    &Callback);

  return Tool.run(newFrontendActionFactory(&Finder).get());
}
