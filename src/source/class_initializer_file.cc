#include "class_initializer_file.h"
#include <algorithm>
namespace abcd::source {
std::variant<FunctionContext, ClassInitializerReadFailure> ResolveClassInitializerContext(
    const corpus::AbcFile& file, const abc::Header& header, const abc::MetadataIndex& index,
    const abc::MethodLookup& lookup, const analysis::MethodRoleIndex& roles,
    const analysis::FunctionCreationSites& sites, const FileFunction& parent,
    const ClassInitializerCall& call, analysis::SemanticBudget budget) {
  using Error = ClassInitializerError;
  const auto& values = parent.values;
  const auto& graph = values.graph;
  if (values.initializers.revision != graph.revision || call.method.value >= graph.ops.size())
    return ClassInitializerReadFailure{Error::kInput};
  const auto& calls = values.initializers.calls;
  const auto found = std::lower_bound(calls.begin(), calls.end(), call.method,
      [](const auto& item, auto method) { return item.method < method; });
  if (found == calls.end() || *found != call) return ClassInitializerReadFailure{Error::kInput};
  const auto& op = graph.ops[call.method.value];
  const auto* creation = std::get_if<analysis::ValueFunctionCreation>(&op.operation);
  if (!creation || op.origin.value >= graph.origins.size()) return ClassInitializerReadFailure{Error::kInput};
  const auto resolved = abc::ResolvePoolReference(file, header, index, values.method,
      creation->method, abc::PoolEntityKind::kMethod);
  const auto* target = std::get_if<abc::PoolReference>(&resolved);
  if (!target) return ClassInitializerReadFailure{std::get<abc::MetadataError>(resolved)};
  const auto occurrence = sites.FindOccurrence(target->offset, values.method, graph.origins[op.origin.value].pc);
  if (!occurrence) return ClassInitializerReadFailure{Error::kInput};
  const auto* site = sites.At(*occurrence);
  if (site->creation.kind != creation->kind || site->creation.method != creation->method ||
      site->creation.length.value != creation->length || site->creation.home_object != creation->home_object ||
      site->creation.cache_slot != creation->cache_slot) return ClassInitializerReadFailure{Error::kInput};
  auto resolved_context = CreationTreeContext::Resolve(file, header, index, lookup, roles, sites,
      *occurrence, values.identity, budget, CreationContextMode::kMethodBody);
  if (const auto* failure = std::get_if<CreationContextFailure>(&resolved_context))
    return ClassInitializerReadFailure{*failure};
  return FunctionContext{std::get<CreationTreeContext>(std::move(resolved_context))};
}
std::variant<ClassInitializerFunction, ClassInitializerReadFailure> ReadClassInitializerFunction(
    const corpus::AbcFile& file, const abc::Header& header, const abc::MetadataIndex& index,
    const abc::MethodLookup& lookup, const analysis::MethodRoleIndex& roles,
    const analysis::FunctionCreationSites& sites, const FileFunction& parent,
    const ClassInitializerCall& call, analysis::SemanticBudget budget, const RecordModuleContext* modules) {
  using Error=ClassInitializerError;
  auto resolved=ResolveClassInitializerContext(file,header,index,lookup,roles,sites,parent,call,budget);
  if(const auto* failure=std::get_if<ClassInitializerReadFailure>(&resolved))return *failure;
  auto context=std::get<FunctionContext>(std::move(resolved));
  const auto& values=parent.values;
  if (context.AllocatedBytes() > budget.bytes) return ClassInitializerReadFailure{Error::kResourceLimit};
  budget.bytes -= context.AllocatedBytes();
  if (context.Work() > budget.work) return ClassInitializerReadFailure{Error::kWorkLimit};
  budget.work -= context.Work();
  auto loaded = ReadFunctionValues(file, header, index, context.Position(), values.identity,
      InputCaptureMode::kClosed, budget, modules, &context);
  if (const auto* failure = std::get_if<FunctionFileFailure>(&loaded)) return ClassInitializerReadFailure{*failure};
  return ClassInitializerFunction{std::move(context), std::get<FileFunction>(std::move(loaded))};
}
}
