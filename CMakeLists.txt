set(LLVM_LINK_COMPONENTS
  Support
  )

add_clang_executable(show-call
  show-call.cpp
  )

target_link_libraries(show-call
  clangAST
  clangASTMatchers
  clangBasic
  clangFrontend
  clangTooling
  )
