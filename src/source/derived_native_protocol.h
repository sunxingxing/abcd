#ifndef ABCD_SOURCE_DERIVED_NATIVE_PROTOCOL_H_
#define ABCD_SOURCE_DERIVED_NATIVE_PROTOCOL_H_
#include <algorithm>

#include "abcd/analysis/value_ir.h"
#include "abcd/source/source_tree.h"
namespace abcd::source {
// Fresh protocol membership is checked at every consumer. The original-body
// proof additionally owns identity, execution domains and effect ordering.
template <class Step>
bool CheckDerivedNativeProtocol(const analysis::ValueProgram& graph,
                                const DerivedNativeProtocol& protocol,
                                Step step) {
  const auto sorted = [&](const auto& ids) {
    for (std::size_t i = 0; i < ids.size(); ++i) {
      step();
      if (ids[i].value >= graph.ops.size() || (i && !(ids[i - 1] < ids[i])))
        return false;
    }
    return true;
  };
  if (!sorted(protocol.calls) || !sorted(protocol.returns) ||
      !sorted(protocol.reads))
    return false;
  for (const auto id : protocol.calls) {
    step();
    const auto& op = graph.ops[id.value];
    const auto* call = std::get_if<analysis::ValueSuperCall>(&op.operation);
    if (op.life != analysis::ValueLife::kLive || !call ||
        call->source != analysis::SuperArgumentSource::kExplicit ||
        call->callee != analysis::SuperCalleeSource::kInvocationFunction)
      return false;
  }
  for (const auto id : protocol.returns) {
    step();
    if (graph.ops[id.value].life != analysis::ValueLife::kLive ||
        !std::holds_alternative<analysis::ValueReturn>(
            graph.ops[id.value].operation))
      return false;
  }
  for (const auto id : protocol.reads) {
    step();
    const auto& op = graph.ops[id.value];
    const auto* check = std::get_if<analysis::ValueSuperCheck>(&op.operation);
    if (op.life != analysis::ValueLife::kLive || !check ||
        check->kind != analysis::SuperCheckKind::kRequireInitialized)
      return false;
  }
  for (std::size_t i = 0; i < protocol.bindings.size(); ++i) {
    step();
    const auto binding = protocol.bindings[i];
    if (binding.guard.value >= graph.ops.size() ||
        (i &&
         !(protocol.bindings[i - 1].guard < binding.guard ||
           (protocol.bindings[i - 1].guard == binding.guard &&
            protocol.bindings[i - 1].construction < binding.construction))) ||
        !std::binary_search(protocol.calls.begin(), protocol.calls.end(),
                            binding.construction))
      return false;
    const auto& guard = graph.ops[binding.guard.value];
    const auto* check =
        std::get_if<analysis::ValueSuperCheck>(&guard.operation);
    if (guard.life != analysis::ValueLife::kLive || !check ||
        check->kind != analysis::SuperCheckKind::kRequireUninitialized ||
        std::binary_search(protocol.reads.begin(), protocol.reads.end(),
                           binding.guard))
      return false;
  }
  std::size_t calls = 0, returns = 0;
  for (std::uint32_t i = 0; i < graph.ops.size(); ++i) {
    step();
    const auto& op = graph.ops[i];
    if (op.life != analysis::ValueLife::kLive) continue;
    if (const auto* call = std::get_if<analysis::ValueSuperCall>(&op.operation);
        call && call->source == analysis::SuperArgumentSource::kExplicit) {
      if (calls == protocol.calls.size() || protocol.calls[calls++].value != i)
        return false;
    }
    if (std::holds_alternative<analysis::ValueReturn>(op.operation)) {
      if (returns == protocol.returns.size() ||
          protocol.returns[returns++].value != i)
        return false;
    }
  }
  return calls == protocol.calls.size() && returns == protocol.returns.size();
}
}  // namespace abcd::source
#endif
