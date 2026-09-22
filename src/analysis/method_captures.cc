#include "abcd/analysis/method_captures.h"

#include <algorithm>
#include <new>
#include <utility>

namespace abcd::analysis {
MethodCaptures::MethodCaptures(CaptureOrigin origin,
                               EnvironmentIndex&& environments,
                               CaptureSummary&& captures,
                               std::unique_ptr<CreationBinding[]>&& creations,
                               std::size_t size, std::size_t peak)
    : origin_(origin),
      environments_(std::move(environments)),
      captures_(std::move(captures)),
      creations_(std::move(creations)),
      size_(size),
      peak_(peak) {}

std::variant<MethodCaptures, MethodCaptureFailure> MethodCaptures::Build(
    const corpus::AbcFile& file, const abc::Header& header,
    const abc::MetadataIndex& index, const abc::MethodLookup& lookup,
    CaptureOrigin origin, const abc::MethodBody& body, EnvironmentFlow&& flow,
    MethodCaptureBudget budget) {
  if (origin.profile != header.profile)
    return MethodCaptureFailure{MethodCaptureError::kContextMismatch};
  if (origin.method.value >= index.methods.size())
    return MethodCaptureFailure{MethodCaptureError::kMethodRange};
  const auto caller = index.methods[origin.method.value].offset;
  if (lookup.Find(caller) != origin.method)
    return MethodCaptureFailure{abc::MethodTargetError::kIndexMismatch};
  if (flow.before.size() != body.instructions.size())
    return MethodCaptureFailure{CreationBindingError::kInvalidFlow};
  const auto ancestry_bytes = flow.environments.AllocatedBytes();
  if (ancestry_bytes > budget.bytes)
    return MethodCaptureFailure{MethodCaptureError::kResourceLimit};
  auto summarized =
      CaptureSummary::Build(flow.bindings, {budget.bytes - ancestry_bytes});
  if (const auto* error = std::get_if<CaptureSummaryError>(&summarized))
    return MethodCaptureFailure{*error};
  auto& captures = std::get<CaptureSummary>(summarized);
  std::size_t count = 0;
  for (std::size_t i = 0; i < body.instructions.size(); ++i) {
    const auto lifted = LiftFunctionCreation(body.instructions[i].decoded);
    if (const auto* error = std::get_if<CreationError>(&lifted)) {
      if (*error != CreationError::kUnsupportedOpcode)
        return MethodCaptureFailure{*error};
    } else if (flow.before[i]) {
      ++count;
    }
  }
  const auto retained = ancestry_bytes + captures.AllocatedBytes();
  if (count > (budget.bytes - retained) / sizeof(CreationBinding))
    return MethodCaptureFailure{MethodCaptureError::kResourceLimit};
  try {
    std::unique_ptr<CreationBinding[]> creations;
    if (count) creations = std::make_unique<CreationBinding[]>(count);
    std::size_t at = 0;
    for (std::size_t i = 0; i < body.instructions.size(); ++i) {
      if (!flow.before[i] ||
          !std::holds_alternative<FunctionCreation>(
              LiftFunctionCreation(body.instructions[i].decoded)))
        continue;
      const auto bound =
          BindCreation(file, header, index, lookup, caller, body, flow, i);
      if (const auto* binding = std::get_if<CreationBinding>(&bound)) {
        creations[at++] = *binding;
        continue;
      }
      if (const auto* error = std::get_if<CreationError>(&bound))
        return MethodCaptureFailure{*error};
      if (const auto* error = std::get_if<CreationBindingError>(&bound))
        return MethodCaptureFailure{*error};
      if (const auto* error = std::get_if<abc::MethodTargetError>(&bound))
        return MethodCaptureFailure{*error};
      if (const auto* error = std::get_if<abc::MetadataError>(&bound))
        return MethodCaptureFailure{*error};
      return MethodCaptureFailure{std::get<abc::MethodAttributeError>(bound)};
    }
    const auto peak = std::max(ancestry_bytes + captures.PeakBytes(),
                               retained + count * sizeof(CreationBinding));
    // Transfer the ancestry only after every modern reachable site has bound.
    // Queries need no input body, per-instruction flow, file or metadata
    // reader.
    return MethodCaptures{origin,
                          std::move(flow.environments),
                          std::move(captures),
                          std::move(creations),
                          count,
                          peak};
  } catch (const std::bad_alloc&) {
    return MethodCaptureFailure{MethodCaptureError::kResourceLimit};
  }
}

std::variant<LexicalBinding, MethodCaptureError, EnvironmentError,
             CaptureRebaseError>
MethodCaptures::Resolve(abc::CodeOffset creation, const MethodCaptures& target,
                        CapturedSlot captured) const {
  if (origin_.file != target.origin_.file ||
      origin_.profile != target.origin_.profile)
    return MethodCaptureError::kContextMismatch;
  const auto sites = Creations();
  if (sites.empty()) return MethodCaptureError::kCreationMissing;
  const auto found =
      std::lower_bound(sites.begin(), sites.end(), creation,
                       [](const CreationBinding& site, abc::CodeOffset pc) {
                         return site.offset < pc;
                       });
  if (found == sites.end() || found->offset != creation)
    return MethodCaptureError::kCreationMissing;
  if (found->target.position != target.origin_.method)
    return MethodCaptureError::kTargetMismatch;
  if (!target.captures_.Find(captured))
    return MethodCaptureError::kCaptureMissing;
  const auto rebased = RebaseCapture(environments_, found->capture, captured);
  if (const auto* binding = std::get_if<LexicalBinding>(&rebased))
    return *binding;
  if (const auto* error = std::get_if<EnvironmentError>(&rebased))
    return *error;
  return std::get<CaptureRebaseError>(rebased);
}

std::size_t MethodCaptures::AllocatedBytes() const {
  return environments_.AllocatedBytes() + captures_.AllocatedBytes() +
         Creations().size() * sizeof(CreationBinding);
}
}  // namespace abcd::analysis
