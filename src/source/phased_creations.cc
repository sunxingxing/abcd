#include "phased_creations.h"

#include <bit>
#include <new>
#include <stdexcept>
#include <type_traits>

namespace abcd::source {
std::variant<PhasedCreations, PhasedCreationFailure> BindPhasedCreations(
    const corpus::AbcFile& file, const abc::Header& header,
    const abc::MetadataIndex& index, const abc::MethodLookup& lookup,
    const analysis::MethodRoleIndex& roles, const analysis::FunctionCreationSites& sites,
    const PhasedBody& parent, const abc::MethodBody& body, analysis::SemanticBudget budget) {
  using Error = PhasedCreationError;
  std::optional<analysis::OpId> current;
  try {
    auto left = budget;
    const auto require = [&](bool ok, Error error) {
      if (!ok) throw PhasedCreationFailure{current, error};
    };
    const auto step = [&](std::uint64_t work = 1) {
      require(work <= left.work, Error::kWorkLimit); left.work -= work;
    };
    const auto admit = [&](std::size_t count, std::size_t width) {
      require(count <= left.bytes / width, Error::kResourceLimit); left.bytes -= count * width;
    };
    require(header.profile == parent.Identity().profile && header.version == parent.Identity().abc_version &&
        header.file_size == parent.Identity().file_size, Error::kIdentity);
    const auto position = lookup.Find(parent.Target());
    require(position && position->value < index.methods.size() &&
        index.methods[position->value].offset == parent.Target(), Error::kIdentity);
    const auto& graph = parent.Graph();
    require(graph.input.sites.size() == body.instructions.size(), Error::kOrigin);
    std::size_t count = 0;
    for (const auto& op : graph.ops) {
      step(); count += op.life == analysis::ValueLife::kLive &&
          std::holds_alternative<analysis::ValueFunctionCreation>(op.operation);
    }
    PhasedCreations result;
    admit(1, sizeof(result)); admit(count, sizeof(PhasedCreation));
    result.sites.reserve(count); admit(result.sites.capacity() - count, sizeof(PhasedCreation));
    const auto input = parent.TreeInput(body);
    for (std::size_t i = 0; i < graph.ops.size(); ++i) {
      step(); const auto& op = graph.ops[i];
      const auto* creation = std::get_if<analysis::ValueFunctionCreation>(&op.operation);
      if (op.life != analysis::ValueLife::kLive || !creation) continue;
      current = analysis::OpId{static_cast<std::uint32_t>(i)};
      const auto& origin = graph.origins[op.origin.value];
      const auto site = origin.site.value;
      require(site < body.instructions.size() && graph.input.sites[site] == current &&
          body.instructions[site].offset == origin.pc, Error::kOrigin);
      // Same bounded regional/metadata lookup cost used by the normal family
      // reader. Do not scan all occurrences of a shared method per creation.
      constexpr std::size_t kMethodLookups = 2;
      step(std::bit_width(index.regions.size()) + kMethodLookups * std::bit_width(index.methods.size()) + 1);
      const auto& phase = graph.phases[op.block.value];
      require(phase.phase == analysis::ExceptionPhase::kBefore && phase.operation == current &&
          phase.instruction == origin.site, Error::kOrigin);
      auto bound = analysis::BindExceptionCreation(file, header, index, lookup, parent.Target(), body,
          input.input.flow, input.input.environments, phase.input);
      if (!std::holds_alternative<analysis::CreationBinding>(bound))
        std::visit([&](const auto& error) {
          if constexpr (!std::is_same_v<std::decay_t<decltype(error)>, analysis::CreationBinding>)
            throw PhasedCreationFailure{current, error};
        }, bound);
      auto binding = std::get<analysis::CreationBinding>(std::move(bound));
      const auto& lifted = binding.creation;
      require(binding.offset == origin.pc && lifted.kind == creation->kind &&
          lifted.method == creation->method && lifted.length.value == creation->length &&
          lifted.home_object == creation->home_object && lifted.cache_slot == creation->cache_slot,
          Error::kOrigin);
      require(binding.target.position.value < index.methods.size(), Error::kIdentity);
      const auto target = index.methods[binding.target.position.value].offset;
      step(std::bit_width(sites.Size()) + 1);
      const auto occurrence = sites.FindOccurrence(target, parent.Target(), origin.pc);
      require(occurrence.has_value(), Error::kOccurrence);
      auto resolved = CreationTreeContext::Resolve(file, header, index, lookup, roles,
          sites, *occurrence, parent.Identity(), left);
      if (const auto* error = std::get_if<CreationContextFailure>(&resolved))
        throw PhasedCreationFailure{current, *error};
      auto context = std::get<CreationTreeContext>(std::move(resolved));
      admit(context.AllocatedBytes(), 1); step(context.Work());
      result.sites.push_back({{*current, {site}, std::move(binding)}, std::move(context)});
    }
    if (file.CheckUnchanged()) throw PhasedCreationFailure{current, abc::MetadataError::kFileChanged};
    result.allocated_bytes = budget.bytes - left.bytes; result.work = budget.work - left.work;
    return result;
  } catch (const PhasedCreationFailure& error) { return error;
  } catch (const std::bad_alloc&) { return PhasedCreationFailure{current, Error::kResourceLimit};
  } catch (const std::length_error&) { return PhasedCreationFailure{current, Error::kResourceLimit}; }
}
}  // namespace abcd::source
