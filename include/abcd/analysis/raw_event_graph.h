#ifndef ABCD_ANALYSIS_RAW_EVENT_GRAPH_H_
#define ABCD_ANALYSIS_RAW_EVENT_GRAPH_H_

#include "abcd/analysis/detail/idf_scratch.h"
#include "abcd/analysis/exception_dispatch.h"
#include "abcd/analysis/semantic_ir.h"
#include <memory_resource>

namespace abcd::analysis {
// Original-body events, not normalized PCs or source expressions. Catch entry
// and a normal jump to the same instruction have different acc definitions.
enum class RawEventPhase : std::uint8_t { kBefore, kCommit, kCatch, kResume, kCallerExit };
enum class RawEventError : std::uint8_t { kRegion, kTarget, kCorrespondence, kResourceLimit, kWorkLimit };
struct RawEventFailure {
  RawEventError kind;
  std::optional<abc::CodeOffset> pc;
};
struct RawEventNode {
  std::array<std::uint32_t, 3> next{};
  std::uint8_t count = 0;
  RawEventPhase phase = RawEventPhase::kBefore;
  RegisterEffect registers;
  bool unknown = false;
};
// Query-local prepaid arena. Borrowers must die before the arena. Retain is
// conservative external storage accounting; releasing storage and credit are
// separate operations. This graph grants neither liveness nor control removal.
class RawEventArena {
 public:
  explicit RawEventArena(SemanticBudget budget)
      : memory_(budget.bytes), left_(budget.work), initial_(budget.work) {}
  std::pmr::memory_resource* Resource() { return &memory_; }
  std::size_t Available() const { return memory_.Available(); }
  std::size_t Peak() const { return memory_.Peak(); }
  void Retain(std::size_t bytes) { memory_.Retain(bytes); }
  // A delegated proof owns and prepays its scratch; observe that temporary
  // peak before retaining only its surviving result in this arena.
  void Observe(std::size_t bytes) { memory_.Observe(bytes); }
  // Before the first charged operation only; preserves a caller's admission bound.
  void LimitWork(std::uint64_t limit);
  void Step(std::uint64_t units = 1);
  std::uint64_t Work() const { return initial_ - left_; }
  std::uint64_t RemainingWork() const { return left_; }
 private:
  detail::IdfScratch memory_;
  std::uint64_t left_, initial_;
};
struct RawEventExtensionEffect {
  RegisterEffect registers;
  ExceptionalCompletion exceptional = ExceptionalCompletion::kImpossible;
  bool resume = false;
  bool unknown = false;
};
// Optional owner-certified raw protocol effects. Only consulted when ordinary
// semantic lifting fails, or to authorize a suspend's resume/exit edges. No
// AwaitRegion or specific coroutine protocol is part of the graph interface.
class RawEventExtension {
 public:
  virtual ~RawEventExtension() = default;
  virtual std::optional<RawEventExtensionEffect> Effect(std::size_t instruction) const = 0;
  virtual bool Resume(std::size_t instruction) const = 0;
};
// Optional semantic owner for an already-normalized body. The raw graph stays
// protocol-independent: the owner must certify its normalization and original
// correspondence. A failed/missing site never supplies invented effects.
class RawEventSemantics {
 public:
  virtual ~RawEventSemantics() = default;
  virtual InstructionLift Lift(const abc::BodyInstruction&) = 0;
  virtual bool Complete() const = 0;
};
class RawEventGraph {
 public:
  static constexpr std::uint32_t kPhases = 4;
  RawEventGraph(const RawEventGraph&) = delete;
  RawEventGraph& operator=(const RawEventGraph&) = delete;
  RawEventGraph(const abc::MethodBody& body, RawEventArena& arena)
      : body_(body), arena_(arena), nodes_(arena.Resource()) {}
  // Staging keeps callers' existing query/owner construction order and exact
  // accounting: dispatch, nodes, caller scratch, then edges. Select's retained
  // charge lasts in the arena even after its returned dispatch is destroyed.
  ExceptionDispatch Select(abc::ProfileId profile);
  void Allocate();
  void Connect(const ExceptionDispatch&, abc::ProfileId, std::uint64_t frame,
               const RawEventExtension* = nullptr,
               RawEventSemantics* = nullptr);
  std::span<const RawEventNode> Nodes() const;
  std::size_t Site(abc::CodeOffset pc);
  abc::CodeOffset Pc(std::uint32_t node) const;
 private:
  void Add(RawEventNode&, std::uint32_t target);
  const abc::MethodBody& body_;
  RawEventArena& arena_;
  std::pmr::vector<RawEventNode> nodes_;
  enum class State : std::uint8_t { kEmpty, kAllocated, kConnecting, kReady };
  State state_ = State::kEmpty;
};
}  // namespace abcd::analysis
#endif
