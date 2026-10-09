#ifndef ABCD_SOURCE_DECLARATION_RETIREMENT_H_
#define ABCD_SOURCE_DECLARATION_RETIREMENT_H_
#include "binding_initializer.h"
#include <span>
namespace abcd::source {
inline void RetireDeclaration(StmtId statement,std::span<std::uint8_t> omitted,
    std::span<std::optional<DeclarationValue>> values,
    std::span<std::optional<DeclarationCompletion>> completions) {
  // Retirement drops lexical completion only. Outlined RHS evaluation keeps
  // its original occurrence and handlers, in both a trial and its commit.
  if(const auto completion=completions[statement.value]) {
    if(completion->evaluation) {
      values[statement.value]=DeclarationValue{*completion->evaluation,{}};
      omitted[statement.value]=0;
    }else omitted[statement.value]=1;
    completions[statement.value].reset();
  }else if(!values[statement.value])omitted[statement.value]=1;
}
}
#endif
