#include "abcd/source/capture_preservation.h"

#include <algorithm>
#include <bit>
#include <limits>
#include <map>
#include <memory_resource>
#include <new>
#include <stdexcept>
#include <type_traits>

#include "abcd/abc/class_buffer.h"
#include "abcd/abc/literal_array.h"
#include "abcd/abc/method_target.h"
#include "abcd/analysis/exception_environment.h"
#include "abcd/analysis/exception_ssa.h"

namespace abcd::source {
namespace {
enum class State : std::uint8_t { kQueued, kReadOnly, kUnknown };
struct Dependency { abc::MethodPosition parent; abc::MethodPosition child; };
struct Root {
  abc::CodeOffset pc;
  std::size_t begin;
  std::size_t end;
};
SourceFailure Resource() { return {SourceError::kResourceLimit, {}}; }
bool Exhausted(const analysis::SemanticFailure& failure) {
  if (failure.kind == analysis::SemanticError::kResourceLimit) return true;
  return std::visit([](const auto& cause) {
    using T = std::decay_t<decltype(cause)>;
    if constexpr (std::is_same_v<T, analysis::RegisterUseFailure>)
      return cause.kind == analysis::RegisterUseError::kWorkLimit;
    else if constexpr (requires { T::kWorkLimit; })
      return cause == T::kWorkLimit;
    else return false;
  }, failure.ssa_cause);
}

class CaptureScratch {
 public:
  explicit CaptureScratch(std::size_t& bytes) : bytes_(bytes) {}
  ~CaptureScratch() { bytes_ += used_; }
  void Take(std::size_t bytes) {
    if (bytes > bytes_) throw Resource();
    bytes_ -= bytes;
    used_ += bytes;
  }
 private:
  std::size_t& bytes_;
  std::size_t used_ = 0;
};

// Sparse reachable targets only. A constructor must not allocate or scan a
// full-file method table for each small closure family. PMR admits the actual
// balanced-tree node allocations; comparisons consume the same work budget.
class CaptureStateMemory final : public std::pmr::memory_resource {
 public:
  explicit CaptureStateMemory(std::size_t& bytes) : bytes_(bytes) {}
 private:
  void* do_allocate(std::size_t bytes, std::size_t alignment) override {
    if (bytes > bytes_) throw Resource();
    auto* result = std::pmr::new_delete_resource()->allocate(bytes, alignment);
    bytes_ -= bytes; return result;
  }
  void do_deallocate(void* pointer, std::size_t bytes, std::size_t alignment) override {
    std::pmr::new_delete_resource()->deallocate(pointer, bytes, alignment); bytes_ += bytes;
  }
  bool do_is_equal(const std::pmr::memory_resource& other) const noexcept override { return this == &other; }
  std::size_t& bytes_;
};
struct CaptureTargetOrder {
  std::uint64_t* work;
  bool operator()(abc::MethodPosition left, abc::MethodPosition right) const {
    if (*work == 0) throw Resource();
    --*work; return left < right;
  }
};

class CaptureProof {
 public:
  CaptureProof(const MethodConstants& constants, const abc::MethodLookup* methods,
               analysis::SemanticBudget budget)
      : constants_(constants), methods_(methods), bytes_(budget.bytes),
        work_(budget.work), initial_work_(budget.work), state_memory_(bytes_),
        states_(CaptureTargetOrder{&work_}, &state_memory_) {}

  CapturePreservations Build(const abc::MethodBody& body) {
    Reserve(roots_, body.instructions.size());
    for (const auto& instruction : body.instructions) {
      Charge();
      const auto begin = root_targets_.size();
      const auto creation = analysis::LiftFunctionCreation(instruction.decoded);
      if (const auto* function = std::get_if<analysis::FunctionCreation>(&creation)) {
        if (const auto target = Target(constants_.method, *function)) {
          RootTarget(*target);
          roots_.push_back({instruction.offset, begin, root_targets_.size()});
        }
      } else if (const auto lifted = analysis::LiftClassCreation(instruction.decoded);
                 std::holds_alternative<analysis::ClassCreation>(lifted)) {
        if (ClassTargets(constants_.method, std::get<analysis::ClassCreation>(lifted),
                         [&](auto target) { RootTarget(target); }))
          roots_.push_back({instruction.offset, begin, root_targets_.size()});
        else
          root_targets_.resize(begin);
      } else if (const auto literal = Buffer(instruction.decoded)) {
        if (PrimitiveBuffer(constants_.method, *literal))
          roots_.push_back({instruction.offset, begin, begin});
      }
    }
    for (std::size_t cursor = 0; cursor < pending_.size(); ++cursor) {
      const auto target = pending_[cursor];
      states_.at(target) = Inspect(target) ? State::kReadOnly : State::kUnknown;
    }
    // A may-write fixed point. Read-only cycles need no recursion or optimistic
    // publication; any unsupported/writing node poisons all its dependants.
    std::sort(edges_.begin(), edges_.end(), [&](auto a, auto b) {
      Charge();
      return a.child < b.child;
    });
    pending_.clear();
    for (const auto& [target, state] : states_) {
      Charge(); if (state == State::kUnknown) Append(pending_, target);
    }
    for (std::size_t cursor = 0; cursor < pending_.size(); ++cursor) {
      const auto child = pending_[cursor];
      auto edge = std::lower_bound(edges_.begin(), edges_.end(), child,
          [&](auto item, auto key) { Charge(); return item.child < key; });
      for (; edge != edges_.end() && edge->child == child; ++edge) {
        Charge();
        auto& state = states_.at(edge->parent);
        if (state == State::kUnknown) continue;
        state = State::kUnknown;
        Append(pending_, edge->parent);
      }
    }
    CapturePreservations result;
    Reserve(result.sites, roots_.size());
    for (const auto& root : roots_) {
      Charge();
      bool read_only = true;
      for (auto i = root.begin; i < root.end; ++i) {
        Charge();
        if (states_.at(root_targets_[i]) != State::kReadOnly) read_only = false;
      }
      if (read_only)
        result.sites.push_back({root.pc});
    }
    result.work_units = initial_work_ - work_;
    return result;
  }

 private:
  void Charge(std::uint64_t amount = 1) {
    if (amount > work_) throw Resource();
    work_ -= amount;
  }
  template <typename T>
  void Reserve(std::vector<T>& storage, std::size_t count) {
    if (count <= storage.capacity()) return;
    if (count > bytes_ / sizeof(T)) throw Resource();
    const auto old = storage.capacity();
    Charge(storage.size()); // Existing entries move once per geometric growth.
    storage.reserve(count);
    if (storage.capacity() > bytes_ / sizeof(T)) throw Resource();
    bytes_ -= (storage.capacity() - old) * sizeof(T);
  }
  template<class T> void Append(std::vector<T>& values, T value) {
    if (values.size() == values.capacity()) {
      constexpr auto kInitialCapacity = std::size_t{4};
      const auto available = bytes_ / sizeof(T), size = values.size();
      if (size == SIZE_MAX || size + 1 > available) throw Resource();
      Reserve(values, size == 0 ? std::min(kInitialCapacity, available) :
          size > available / 2 ? available : size * 2);
    }
    values.push_back(std::move(value));
  }
  void Edge(abc::MethodPosition parent, abc::MethodPosition child) {
    if (edges_.size() == edges_.capacity()) {
      constexpr std::size_t kInitialEdges = 4;
      const auto available = bytes_ / sizeof(Dependency);
      const auto size = edges_.size();
      if (size == SIZE_MAX || size + 1 > available) throw Resource();
      Reserve(edges_, size == 0 ? std::min(kInitialEdges, available)
          : size > available / 2 ? available : size * 2);
    }
    edges_.push_back({parent, child});
  }
  std::optional<abc::MethodPosition> Target(
      abc::EntityOffset caller, const analysis::FunctionCreation& creation) {
    // Both pinned DEFINEFUNC and DEFINEMETHOD capture the current lexical
    // environment. Their different home-object inputs do not grant read-only
    // status; every reachable body is still checked for captured stores.
    switch (creation.kind) {
      case analysis::CreationKind::kFunction:
        if (creation.home_object != analysis::HomeObjectInput::kCurrentFunction) return {};
        break;
      case analysis::CreationKind::kMethod:
        if (creation.home_object != analysis::HomeObjectInput::kAccumulator) return {};
        break;
    }
    return Target(caller, creation.method);
  }
  std::optional<abc::MethodPosition> Target(abc::EntityOffset caller, abc::PoolIndex slot) {
    if (!methods_) return {};
    Charge(std::bit_width(constants_.index.regions.size()) +
           std::bit_width(methods_->Size()) + 1);
    const auto resolved = abc::ResolveMethodTarget(constants_.file, constants_.header,
        constants_.index, *methods_, caller, slot);
    const auto* target = std::get_if<abc::MethodTarget>(&resolved);
    if (const auto* error = std::get_if<abc::MetadataError>(&resolved);
        error && *error == abc::MetadataError::kResourceLimit) throw Resource();
    if (!target || target->attributes.sendable) return {};
    return Queue(target->position);
  }
  std::optional<abc::MethodPosition> Queue(abc::MethodPosition position) {
    if (position.value >= constants_.index.methods.size()) return {};
    const auto [at, inserted] = states_.try_emplace(position, State::kQueued);
    if (inserted) Append(pending_, position);
    return position;
  }
  void RootTarget(abc::MethodPosition target) {
    if (root_targets_.size() == root_targets_.capacity()) {
      constexpr std::size_t kInitialTargets = 4;
      const auto available = bytes_ / sizeof(abc::MethodPosition);
      const auto size = root_targets_.size();
      if (size == SIZE_MAX || size + 1 > available) throw Resource();
      Reserve(root_targets_, size == 0 ? std::min(kInitialTargets, available)
          : size > available / 2 ? available : size * 2);
    }
    root_targets_.push_back(target);
  }
  template <typename Accept>
  bool ClassTargets(abc::EntityOffset caller, const analysis::ClassCreation& creation,
                    Accept accept) {
    const auto constructor = Target(caller, creation.constructor);
    if (!constructor) return false;
    accept(*constructor);
    Charge(std::bit_width(constants_.index.regions.size()) + 1);
    const auto reference = abc::ResolvePoolReference(constants_.file, constants_.header,
        constants_.index, caller, creation.literal, abc::PoolEntityKind::kLiteralArray);
    const auto* entity = std::get_if<abc::PoolReference>(&reference);
    if (const auto* error = std::get_if<abc::MetadataError>(&reference);
        error && *error == abc::MetadataError::kResourceLimit) throw Resource();
    if (!entity) return false;
    const auto decoded = abc::ReadLiteralArray(constants_.file, constants_.header,
                                               entity->offset, {bytes_});
    const auto* literal = std::get_if<abc::LiteralArray>(&decoded);
    if (!literal) {
      if (std::get<abc::LiteralFailure>(decoded).kind == abc::LiteralError::kResourceLimit)
        throw Resource();
      return false;
    }
    CaptureScratch scratch(bytes_);
    scratch.Take(literal->entries.capacity() * sizeof(abc::LiteralEntry));
    Charge(literal->entries.size());
    const auto parsed = abc::DecodeClassBuffer(*literal, {bytes_});
    const auto* members = std::get_if<abc::ClassBuffer>(&parsed);
    if (!members) {
      if (std::get<abc::ClassBufferError>(parsed) == abc::ClassBufferError::kResourceLimit)
        throw Resource();
      return false;
    }
    scratch.Take(members->properties.capacity() * sizeof(abc::ClassProperty));
    for (const auto& property : members->properties) {
      Charge();
      const auto* member = std::get_if<abc::ClassMethod>(&property.value);
      if (!member) continue;
      Charge(std::bit_width(methods_->Size()) + 1);
      const auto position = methods_->Find(member->target);
      if (!position || position->value >= constants_.index.methods.size()) return false;
      const auto& info = constants_.index.methods[position->value];
      if (info.offset != member->target) return false;
      const auto attributes = abc::DecodeMethodAttributes(info.access_flags,
                                                         constants_.header.profile);
      const auto* flags = std::get_if<abc::MethodAttributes>(&attributes);
      if (!flags || flags->sendable) return false;
      const auto queued = Queue(*position);
      if (!queued) return false;
      accept(*queued);
    }
    return true;
  }
  static std::optional<abc::PoolIndex> Buffer(const abc::DecodedInstruction& instruction) {
    const auto object = analysis::LiftObjectCreation(instruction);
    if (const auto* buffer = std::get_if<analysis::BufferedObjectCreation>(&object))
      return buffer->literal;
    const auto array = analysis::LiftArrayCreation(instruction);
    if (const auto* buffer = std::get_if<analysis::BufferedArrayCreation>(&array))
      return buffer->literal;
    return {};
  }
  bool PrimitiveBuffer(abc::EntityOffset caller, abc::PoolIndex slot) {
    Charge(std::bit_width(constants_.index.regions.size()) + 1);
    const auto reference = abc::ResolvePoolReference(constants_.file, constants_.header,
        constants_.index, caller, slot, abc::PoolEntityKind::kLiteralArray);
    const auto* entity = std::get_if<abc::PoolReference>(&reference);
    if (const auto* error = std::get_if<abc::MetadataError>(&reference);
        error && *error == abc::MetadataError::kResourceLimit) throw Resource();
    if (!entity) return false;
    const auto decoded = abc::ReadLiteralArray(constants_.file, constants_.header,
                                               entity->offset, {bytes_});
    const auto* literal = std::get_if<abc::LiteralArray>(&decoded);
    if (!literal) {
      if (std::get<abc::LiteralFailure>(decoded).kind == abc::LiteralError::kResourceLimit)
        throw Resource();
      return false;
    }
    for (const auto& entry : literal->entries) {
      Charge();
      switch (entry.tag) {
        case abc::LiteralTag::kBoolean:
        case abc::LiteralTag::kInteger:
        case abc::LiteralTag::kDouble:
        case abc::LiteralTag::kString:
        case abc::LiteralTag::kNull:
          break;
        default: return false;
      }
    }
    return true;
  }
  bool Inspect(abc::MethodPosition target) {
    const auto& info = constants_.index.methods[target.value];
    if (!info.code) return false;
    const auto frame = std::uint64_t(info.code->registers) + info.code->arguments;
    const auto decoded = abc::DecodeMethodBody(constants_.file, constants_.header.profile,
                                               *info.code, {bytes_});
    const auto* body = std::get_if<abc::MethodBody>(&decoded);
    if (!body) {
      if (std::get<abc::BodyFailure>(decoded).kind == abc::BodyError::kResourceLimit)
        throw Resource();
      return false;
    }
    CaptureScratch scratch(bytes_);
    scratch.Take(body->instructions.capacity() * sizeof(abc::BodyInstruction));
    scratch.Take(body->tries.capacity() * sizeof(abc::TryRegion));
    scratch.Take(body->handlers.capacity() * sizeof(abc::CatchHandler));
    if (!body->tries.empty()) return InspectException(target, *body, frame);
    const auto lifted = analysis::BuildSemanticMethod(*body, constants_.header.profile,
                                                      frame, {bytes_, work_});
    const auto* method = std::get_if<analysis::SemanticMethod>(&lifted);
    if (!method) {
      if (Exhausted(std::get<analysis::SemanticFailure>(lifted)))
        throw Resource();
      return false;
    }
    scratch.Take(method->allocated_bytes);
    Charge(method->ssa.work.units);
    const auto environments = analysis::AnalyzeEnvironments(*body, method->flow, frame, {bytes_});
    const auto* bound = std::get_if<analysis::EnvironmentFlow>(&environments);
    if (!bound) {
      if (std::get<analysis::EnvironmentFlowFailure>(environments).kind ==
          analysis::EnvironmentFlowError::kResourceLimit) throw Resource();
      return false;
    }
    scratch.Take(bound->allocated_bytes);
    Charge(bound->instruction_visits);
    return InspectCaptures(target, method->instructions, *bound, method->ssa,
        [](analysis::InstructionId site) { return std::optional{site}; });
  }
  bool InspectException(abc::MethodPosition target, const abc::MethodBody& body,
                        std::uint64_t frame) {
    const auto built = analysis::BuildExceptionFlow(body, constants_.header.profile,
                                                   frame, {bytes_, work_});
    const auto* flow = std::get_if<analysis::ExceptionFlow>(&built);
    if (!flow) {
      std::visit([](const auto& error) {
        using T = std::decay_t<decltype(error)>;
        if constexpr (std::is_same_v<T, analysis::SemanticFailure>) {
          if (Exhausted(error)) throw Resource();
        } else if constexpr (std::is_same_v<T, analysis::FlowFailure>) {
          if (error.kind == analysis::FlowError::kResourceLimit) throw Resource();
        } else if constexpr (std::is_same_v<T, analysis::DispatchFailure>) {
          if (error.kind == analysis::DispatchError::kResourceLimit) throw Resource();
        }
      }, built);
      return false;
    }
    CaptureScratch scratch(bytes_);
    scratch.Take(flow->allocated_bytes);
    Charge(flow->work_units);
    analysis::IdfQuota quota{work_};
    const auto renamed = analysis::ExceptionSsa::Build(*flow, frame, quota, {bytes_});
    Charge(quota.Consumed());
    const auto* ssa = std::get_if<analysis::ExceptionSsa>(&renamed);
    if (!ssa) {
      std::visit([](const auto& error) {
        using T = std::decay_t<decltype(error)>;
        if constexpr (std::is_same_v<T, analysis::RegisterUseFailure>) {
          if (error.kind == analysis::RegisterUseError::kResourceLimit ||
              error.kind == analysis::RegisterUseError::kWorkLimit) throw Resource();
        } else if constexpr (!std::is_same_v<T, analysis::ExceptionSsa>) {
          if (error == T::kResourceLimit) throw Resource();
          if constexpr (requires { T::kWorkLimit; })
            if (error == T::kWorkLimit) throw Resource();
        }
      }, renamed);
      return false;
    }
    scratch.Take(ssa->AllocatedBytes());
    const auto analyzed = analysis::AnalyzeExceptionEnvironments(*flow, {bytes_});
    const auto* environments = std::get_if<analysis::EnvironmentFlow>(&analyzed);
    if (!environments) {
      if (std::get<analysis::EnvironmentFlowFailure>(analyzed).kind ==
          analysis::EnvironmentFlowError::kResourceLimit) throw Resource();
      return false;
    }
    scratch.Take(environments->allocated_bytes);
    Charge(environments->instruction_visits);
    // SSA/environment indices belong to phase nodes, not ABC instructions.
    // Catch defines a pending exception, never a NEWLEXENV result; bindings
    // occur at Before and allocation results at Commit. Keep every catch edge.
    return InspectCaptures(target, flow->instructions, *environments, *ssa,
        [&](analysis::InstructionId site) -> std::optional<analysis::InstructionId> {
          const auto& node = flow->nodes.at(site.value);
          if (node.phase == analysis::ExceptionPhase::kCatch) return {};
          return node.instruction;
        });
  }
  template <typename Ssa, typename OriginalSite>
  bool InspectCaptures(abc::MethodPosition target,
      std::span<const analysis::SemanticInstruction> instructions,
      const analysis::EnvironmentFlow& environments, const Ssa& ssa,
      OriginalSite original_site) {
    const auto& info = constants_.index.methods[target.value];
    for (const auto& binding : environments.bindings) {
      Charge();
      const auto site = original_site(analysis::InstructionId{binding.instruction});
      if (!site) return false;
      if (std::holds_alternative<analysis::CapturedDepth>(binding.binding.environment) &&
          std::holds_alternative<analysis::LexicalStore>(
              instructions[site->value].operation)) return false;
    }
    const auto exposed = [&](analysis::SsaValueId id) {
      if (id.value >= ssa.Values().size()) return true;
      const auto* origin = std::get_if<analysis::InstructionValue>(&ssa.Values()[id.value].origin);
      if (!origin) return false;
      const auto site = original_site(origin->instruction);
      return site && std::holds_alternative<analysis::PushEnvironment>(
          instructions[site->value].operation);
    };
    for (const auto& read : ssa.AllReads()) {
      Charge();
      if (exposed(read.value)) return false;
    }
    for (std::size_t i = 0; i < ssa.Values().size(); ++i) {
      Charge();
      if (!std::holds_alternative<analysis::PhiValue>(ssa.Values()[i].origin)) continue;
      for (const auto& input : ssa.Inputs({static_cast<std::uint32_t>(i)})) {
        Charge();
        if (exposed(input.value)) return false;
      }
    }
    for (const auto& site : instructions) {
      Charge();
      if (std::holds_alternative<analysis::CurrentEnvironment>(site.operation)) return false;
      if (const auto* creation = std::get_if<analysis::ClassCreation>(&site.operation)) {
        if (!ClassTargets(info.offset, *creation,
                          [&](auto child) { Edge(target, child); })) return false;
      }
      if (const auto* child = std::get_if<analysis::FunctionCreation>(&site.operation)) {
        const auto resolved = Target(info.offset, *child);
        if (!resolved) return false;
        Edge(target, *resolved);
      } else if (const auto* buffer = std::get_if<analysis::BufferedObjectCreation>(&site.operation)) {
        if (!PrimitiveBuffer(info.offset, buffer->literal)) return false;
      } else if (const auto* buffer = std::get_if<analysis::BufferedArrayCreation>(&site.operation)) {
        if (!PrimitiveBuffer(info.offset, buffer->literal)) return false;
      }
    }
    return true;
  }
  const MethodConstants& constants_;
  const abc::MethodLookup* methods_;
  std::size_t bytes_;
  std::uint64_t work_;
  std::uint64_t initial_work_;
  CaptureStateMemory state_memory_;
  std::pmr::map<abc::MethodPosition, State, CaptureTargetOrder> states_;
  std::vector<abc::MethodPosition> pending_;
  std::vector<Dependency> edges_;
  std::vector<Root> roots_;
  std::vector<abc::MethodPosition> root_targets_;
};
}  // namespace

std::variant<CapturePreservations, SourceFailure> ProveCapturePreservation(
    const abc::MethodBody& body, const MethodConstants& constants,
    const abc::MethodLookup* methods, analysis::SemanticBudget budget) {
  try {
    if (constants.index.methods.size() > UINT32_MAX) return Resource();
    return CaptureProof(constants, methods, budget).Build(body);
  } catch (const SourceFailure& failure) {
    return failure;
  } catch (const std::bad_alloc&) {
    return Resource();
  } catch (const std::length_error&) {
    return Resource();
  }
}
}  // namespace abcd::source
