#include "../../src/source/class_initializer_file.h"
#include "abcd/corpus/abc_file.h"
#include "abcd/source/source_route.h"
#include <cstdlib>
#include <iostream>
#include <memory>
#include <span>
#include <source_location>
namespace {
void Check(bool value, const std::source_location& where = std::source_location::current()) {
  if (!value) { std::cerr << "check failed at " << where.line() << '\n'; std::abort(); }
}
}
int main(int argc, char** argv) {
  using namespace abcd;
  using namespace abcd::source;
  Check(argc == 2);
  auto opened = corpus::AbcFile::Open(argv[1]);
  const auto& file = *std::get<std::unique_ptr<corpus::AbcFile>>(opened);
  const auto fingerprint = std::get<corpus::FileFingerprint>(file.Fingerprint());
  const auto header = std::get<abc::Header>(abc::ParseHeader(
      std::span(fingerprint.prefix).first(fingerprint.prefix_size), fingerprint.size));
  const auto identity = MakeRouteInputIdentity(fingerprint, header);
  const auto index = std::get<abc::MetadataIndex>(abc::BuildMetadataIndex(file, header));
  const auto lookup = std::get<abc::MethodLookup>(abc::MethodLookup::Build(index.methods));
  const auto roles = std::get<analysis::MethodRoleIndex>(analysis::MethodRoleIndex::Build(file, header, index, lookup));
  const auto sites = std::get<analysis::FunctionCreationSites>(analysis::FunctionCreationSites::Build(file, header, index, lookup));
  for (const auto entry : {7312U, 16391U}) {
    const auto position = lookup.Find({entry}); Check(position.has_value());
    auto caller = ReadFunctionValues(file, header, index, *position, identity, InputCaptureMode::kClosed);
    Check(std::holds_alternative<FileFunction>(caller));
    const auto& parent = std::get<FileFunction>(caller);
    Check(parent.values.initializers.calls.size() == 1);
    const auto& call = parent.values.initializers.calls.front();
    auto loaded = ReadClassInitializerFunction(file, header, index, lookup, roles, sites, parent, call);
    if (const auto* failure = std::get_if<ClassInitializerReadFailure>(&loaded)) {
      std::cerr << "initializer read failure " << failure->index();
      if (const auto* body = std::get_if<FunctionFileFailure>(failure)) {
        std::cerr << " body " << body->index();
        if (const auto* input = std::get_if<FunctionInputFailure>(body)) {
          std::cerr << " input " << input->cause.index();
        }
        if (const auto* values = std::get_if<FunctionValueFailure>(body))
          std::cerr << " stage " << static_cast<unsigned>(values->cause.reason);
      }
      std::cerr << '\n'; return 1;
    }
    const auto& initializer = std::get<ClassInitializerFunction>(loaded);
    Check(initializer.context.Callable() == CallableKind::kMethod);
    Check(initializer.function.input.MatchesOwner(initializer.context));
    Check(initializer.function.frame.parameters == 0);
    const auto* site = initializer.context.CreationSite(); Check(site);
    const auto occurrence = sites.FindOccurrence(site->target, site->creator, site->pc); Check(occurrence.has_value());
    const auto ordinary = CreationTreeContext::Resolve(file, header, index, lookup, roles, sites, *occurrence, identity);
    Check(std::get<CreationContextError>(std::get<CreationContextFailure>(ordinary).error) == CreationContextError::kKind);
    auto wrong = call; ++wrong.receiver.value;
    Check(std::holds_alternative<ClassInitializerReadFailure>(
        ReadClassInitializerFunction(file, header, index, lookup, roles, sites, parent, wrong)));
    std::size_t fields = 0, object_fields = 0;
    const auto& graph = initializer.function.values.graph;
    for (const auto& op : graph.ops) {
      if (op.life != analysis::ValueLife::kLive) continue;
      if (const auto* field = std::get_if<analysis::ValueOwnWrite>(&op.operation)) {
        Check(field->kind == analysis::OwnWriteKind::kDefineField);
        Check(op.use_count == 2);
        const auto& base = graph.uses[op.first_use.value];
        Check(base.role == analysis::OperandRole::kBase);
        const auto* value = std::get_if<analysis::ValueId>(&base.operand); Check(value);
        const auto* origin = std::get_if<analysis::EntryDefinition>(&graph.values[value->value].origin);
        if (origin && std::holds_alternative<analysis::ReceiverEntry>(*origin)) {
          ++fields;
        } else {
          const auto* produced = std::get_if<analysis::OpResult>(&graph.values[value->value].origin);
          Check(produced && produced->op.value < graph.ops.size());
          Check(std::holds_alternative<analysis::ValueObjectAllocation>(graph.ops[produced->op.value].operation));
          ++object_fields;
        }
      }
    }
    Check(fields > 0 && (entry != 7312 || fields == 3));
    std::cout << "entry=" << entry << " initializer=" << site->target.value << " own_fields=" << fields << " object_fields=" << object_fields << '\n';
  }
}
