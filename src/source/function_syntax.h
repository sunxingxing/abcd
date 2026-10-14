#ifndef ABCD_SOURCE_FUNCTION_SYNTAX_H_
#define ABCD_SOURCE_FUNCTION_SYNTAX_H_

#include "abcd/source/source_tree.h"
#include "cell_plan.h"

namespace abcd::source {
struct ParameterSyntax {
  std::optional<NameId> name;
  NameEvidence evidence = NameEvidence::kSynthesized;
  bool rest = false;
};
struct CaptureSyntax {
  CellId cell;
  CapturedSymbol target;
  NameId name;
  NameEvidence evidence = NameEvidence::kSynthesized;
};
struct ModuleSyntax {
  ModuleSymbol target;
  NameId name;
  ModuleAccess access;
  NameEvidence evidence = NameEvidence::kSynthesized;
  bool local = false;
};
struct LocalNameSyntax {
  analysis::OpId allocation;
  analysis::LexicalSlot slot;
  NameId name;
};
struct FunctionSyntax {
  RouteInputIdentity identity;
  abc::EntityOffset method;
  FunctionOwnerId owner;
  CallableKind callable;
  Strictness strictness;
  std::vector<ParameterSyntax> parameters;
  std::vector<std::u16string> names;
  std::vector<std::u16string> texts;
  // Sorted incoming CellIds only. Names refer to the supplied name arena;
  // actual enclosing owner/scope and spelling need a separate creation check.
  std::vector<CaptureSyntax> captures;
  // Source module aliases, not runtime ABC slots. The record owner must verify
  // their spelling/access/target; this only constructs conditional lexical scope.
  std::vector<ModuleSyntax> modules;
  // Exact creation-proven runtime name. Tree construction may use it for a
  // named-function self binding only when its identifier/scope proof succeeds.
  std::optional<NameId> inferred_name;
  // Sorted allocation/slot hints; names never establish storage or lifetime.
  std::vector<LocalNameSyntax> locals;
  // parameters retains physical frame metadata; a proven invocation-rest
  // projection introduces its own source binding instead of reusing slot zero.
  ParameterForm parameter_form = ParameterForm::kFrameFormals;
  HomeObjectBinding home_object = HomeObjectBinding::kNone;
};
}  // namespace abcd::source
#endif
