#include "phased_parameter_defaults.h"

#include <bit>
#include <cmath>
#include <map>
#include <new>
#include <stdexcept>

#include "../analysis/value_phi.h"
#include "abcd/analysis/dominators.h"
#include "parameter_plan.h"
#include "recovery_scratch.h"

namespace abcd::source {
namespace {
using namespace analysis;
using Error = PhasedParameterError;
void Need(bool yes, Error e = Error::kInput) {
  if (!yes) throw e;
}
bool SameLiteral(const PhasedDefaultLiteral& a, const PhasedDefaultLiteral& b) {
  if (a.index() != b.index()) return false;
  return std::visit(
      [&](const auto& x) {
        using T = std::decay_t<decltype(x)>;
        const auto& y = std::get<T>(b);
        if constexpr (std::is_same_v<T, SpecialValue>)
          return x == y;
        else
          return x.value == y.value;
      },
      a);
}
template <class V>
bool SameScalar(const V& a, const V& b) {
  if (a.index() != b.index()) return false;
  return std::visit(
      [&](const auto& x) {
        using T = std::decay_t<decltype(x)>;
        const auto& y = std::get<T>(b);
        if constexpr (std::is_enum_v<T>)
          return x == y;
        else if constexpr (requires { x.value; })
          return x.value == y.value;
        else
          return true;
      },
      a);
}
bool SameExpression(const ScalarExpression& a, const ScalarExpression& b) {
  if (a.index() != b.index()) return false;
  return std::visit(
      [&](const auto& x) {
        using T = std::decay_t<decltype(x)>;
        const auto& y = std::get<T>(b);
        if constexpr (std::is_same_v<T, ScalarValue>)
          return SameScalar(x, y);
        else if constexpr (std::is_same_v<T, UnaryExpression>)
          return x.op == y.op && SameScalar(x.input, y.input);
        else if constexpr (std::is_same_v<T, BinaryExpression>)
          return x.op == y.op && SameScalar(x.left, y.left) &&
                 SameScalar(x.right, y.right);
        else
          return false;
      },
      a);
}
bool SamePreludeOperation(const SemanticOperation& a,
                          const SemanticOperation& b) {
  if (a.index() != b.index()) return false;
  return std::visit(
      [&](const auto& x) {
        using T = std::decay_t<decltype(x)>;
        const auto& y = std::get<T>(b);
        if constexpr (std::is_same_v<T, NoOperation>)
          return true;
        else if constexpr (std::is_same_v<T, ScalarAssignment>)
          return SameScalar(x.destination, y.destination) &&
                 SameExpression(x.expression, y.expression) &&
                 x.completion == y.completion;
        else if constexpr (std::is_same_v<T, BranchOperation>)
          return x.predicate.op == y.predicate.op &&
                 SameScalar(x.predicate.input, y.predicate.input) &&
                 x.target == y.target;
        else if constexpr (std::is_same_v<T, JumpOperation>)
          return x.target == y.target;
        else if constexpr (std::is_same_v<T, LexicalLoad> ||
                           std::is_same_v<T, LexicalStore>) {
          bool same = x.address.depth.value == y.address.depth.value &&
                      x.address.slot.value == y.address.slot.value;
          if constexpr (std::is_same_v<T, LexicalStore>)
            same &= SameScalar(x.value, y.value);
          return same;
        } else if constexpr (std::is_same_v<T, PushEnvironment>)
          return x.slots == y.slots && x.scope == y.scope;
        else if constexpr (std::is_same_v<T, StringLoad>)
          return x.value == y.value && x.kind == y.kind;
        else
          return false;
      },
      a);
}
enum class AtomKind : std::uint8_t {
  kUnknown,
  kFormal,
  kInitialized,
  kLiteral,
  kTest,
  kInvalid
};
struct Atom {
  AtomKind kind = AtomKind::kUnknown;
  std::uint32_t value = 0;
  PhasedDefaultLiteral literal = SpecialValue::kUndefined;
  bool equal = true;
};
bool Same(const Atom& a, const Atom& b) {
  return a.kind == b.kind && a.value == b.value && a.equal == b.equal &&
         (a.kind != AtomKind::kLiteral || SameLiteral(a.literal, b.literal));
}
class Proof {
 public:
  Proof(const PhasedBody& phases, const abc::MethodBody& body,
        const abc::FrameLayout& frame, std::uint32_t length, SemanticBudget b)
      : phases_(phases),
        body_(body),
        g_(phases.Graph()),
        frame_(frame),
        length_(length),
        q_(b) {}
  PhasedParameterCertificate Run() {
    Need(length_ < frame_.parameters && !g_.phases.empty() &&
         g_.ops.size() == body_.instructions.size());
    Need(phases_.TreeInput(body_).input.frame == frame_);
    const auto original = phases_.TreeInput(body_);
    const auto checked = VerifyExceptionControl(
        body_, g_, original.input, original.regions, original.control,
        q_.Remaining(),
        original.for_of ? &original.for_of->ownership : nullptr);
    CheckResult(checked);
    q_.Step(std::get<std::uint64_t>(checked));
    c_.formals = frame_.parameters;
    c_.length = length_;
    auto parameters = BuildParameterPlan(g_, frame_.parameters, q_.Remaining());
    CheckResult(parameters);
    p_ = std::get<ParameterPlan>(std::move(parameters));
    q_.Admit(p_.allocated_bytes, 1);
    q_.Step(p_.work);
    auto ids = AnalyzeValueIdentities(g_, q_.Remaining());
    CheckResult(ids);
    ids_ = std::get<ValueIdentities>(std::move(ids));
    q_.Admit(ids_.allocated_bytes, 1);
    q_.Step(ids_.work);
    auto flow = BuildNormalFlow(body_, {q_.Remaining().bytes});
    CheckResult(flow);
    flow_ = std::get<NormalFlow>(std::move(flow));
    q_.Admit(flow_.blocks.capacity(), sizeof(FlowBlock));
    q_.Step(body_.instructions.size() *
            (1 + std::bit_width(body_.instructions.size())));
    q_.Array(atoms_, g_.values.size());
    q_.Array(known_, g_.values.size());
    q_.Array(visited_, flow_.blocks.size());
    q_.Array(prefix_, g_.ops.size());
    q_.Array(defaulted_, frame_.parameters);
    q_.Array(phi_first_, g_.blocks.size() + 1);
    for (const auto& v : g_.values) {
      q_.Step();
      if (const auto* phi = std::get_if<PhiDefinition>(&v.origin))
        ++phi_first_[phi->block.value + 1];
    }
    for (std::size_t i = 1; i < phi_first_.size(); ++i) {
      q_.Step();
      phi_first_[i] += phi_first_[i - 1];
    }
    q_.Array(phis_, phi_first_.back());
    q_.Admit(phi_first_.size(), sizeof(std::uint32_t));
    auto cursors = phi_first_;
    for (std::uint32_t i = 0; i < g_.values.size(); ++i) {
      q_.Step();
      if (const auto* phi = std::get_if<PhiDefinition>(&g_.values[i].origin))
        phis_[cursors[phi->block.value]++] = {i};
    }
    if (const auto* cells = phases_.Cells()) {
      q_.Array(cells_, cells->cells.size());
      q_.Array(cell_stores_, cells->cells.size());
      for (std::uint32_t i = 0; i < cells_.size(); ++i) cells_[i].value = i;
    }
    Postdominators();
    BlockId block{0};
    std::uint32_t next = length_;
    for (;;) {
      Need(block.value < flow_.blocks.size() && !visited_[block.value],
           Error::kOrder);
      visited_[block.value] = 1;
      const auto& b = flow_.blocks[block.value];
      std::optional<OpId> branch;
      for (std::uint32_t i = b.first_instruction;
           i < b.first_instruction + b.instruction_count; ++i) {
        q_.Step();
        if (std::holds_alternative<ValueBranch>(g_.ops[i].operation)) {
          branch = OpId{i};
          break;
        }
        if (!Execute({i}, false, nullptr)) goto finished;
      }
      if (!branch) {
        if (b.successor_count != 1) break;
        block = b.successors[0].target;
        continue;
      }
      {
        const auto condition = Operand(*branch, OperandRole::kCondition);
        const auto atom = Resolve(condition);
        if (atom.kind != AtomKind::kTest || atom.value < next) break;
        Need(atom.value < frame_.parameters &&
                 (c_.defaults.empty() ? atom.value == length_ : true),
             Error::kOrdinal);
        Need(post_.nodes[block.value].parent.has_value(), Error::kDecision);
        const auto join = *post_.nodes[block.value].parent;
        Need(join.value < flow_.blocks.size() && b.successor_count == 2,
             Error::kDecision);
        const auto* predicate =
            std::get_if<ValueBranch>(&g_.ops[branch->value].operation);
        Need(predicate &&
                 (predicate->predicate == UnaryOperator::kFalseOrZero ||
                  predicate->predicate == UnaryOperator::kTrueOrNonzeroNumber),
             Error::kDecision);
        const bool missing_taken =
            (predicate->predicate == UnaryOperator::kTrueOrNonzeroNumber) ==
            atom.equal;
        const auto missing = b.successors[missing_taken ? 0 : 1].target;
        const auto provided = b.successors[missing_taken ? 1 : 0].target;
        const auto condition_id = std::get_if<ValueId>(&condition);
        Need(condition_id, Error::kDecision);
        const auto comparison_id = ids_.canonical[condition_id->value];
        const auto* comparison =
            std::get_if<OpResult>(&g_.values[comparison_id.value].origin);
        Need(comparison, Error::kDecision);
        PhasedParameterDefault d{};
        d.ordinal = atom.value;
        Need(p_.formals[d.ordinal].entry.has_value(), Error::kOrdinal);
        d.entry = *p_.formals[d.ordinal].entry;
        d.comparison = comparison->op;
        d.decision = *branch;
        const auto& before = g_.blocks[g_.ops[branch->value].block.value];
        Need(before.edge_count == 1, Error::kDecision);
        const auto commit = g_.edges[before.first_edge.value].target;
        Need(g_.phases[commit.value].phase == ExceptionPhase::kCommit,
             Error::kDecision);
        const auto& raw = g_.blocks[commit.value];
        bool have_provided = false, have_missing = false;
        for (std::uint32_t e = 0; e < raw.edge_count; ++e) {
          q_.Step();
          const EdgeId id{raw.first_edge.value + e};
          const auto kind = g_.edges[id.value].kind;
          if (kind == FlowEdgeKind::kTaken ||
              kind == FlowEdgeKind::kFallthrough) {
            const bool taken = kind == FlowEdgeKind::kTaken;
            if (taken == missing_taken) {
              d.missing = id;
              have_missing = true;
            } else {
              d.provided = id;
              have_provided = true;
            }
          }
        }
        Need(have_provided && have_missing, Error::kDecision);
        d.join = g_.ops[flow_.blocks[join.value].first_instruction].block;
        Replay(*branch);
        Mark(*branch);
        // Arms have independent private-cell states. Sparse undo logs charge
        // each actual write, never clone an environment per parameter.
        std::pmr::map<std::uint32_t, Atom> saved{&scratch_},
            provided_cells{&scratch_};
        Arm(provided, join, false, d, saved);
        for (const auto& [cell, old] : saved) {
          q_.Step(1 + std::bit_width(saved.size()));
          provided_cells.emplace(cell, cells_[cell]);
          cells_[cell] = old;
        }
        Arm(missing, join, true, d, saved);
        Need(!d.initializer_ops.empty(), Error::kInitializer);
        std::optional<PhasedDefaultLiteral> fallback;
        for (std::uint32_t k = phi_first_[d.join.value];
             k < phi_first_[d.join.value + 1]; ++k) {
          q_.Step();
          const auto value = phis_[k];
          std::optional<Atom> first;
          bool different = false;
          bool formal = false;
          std::optional<PhasedDefaultLiteral> literal;
          bool alternatives = true, closed = true;
          ForPhi(value, [&](const Atom& a, const OperandUse& use) {
            alternatives &=
                (a.kind == AtomKind::kFormal && a.value == d.ordinal) ||
                a.kind == AtomKind::kLiteral;
            const auto* edge =
                use.incoming ? std::get_if<EdgeId>(&*use.incoming) : nullptr;
            if (!edge)
              closed = false;
            else {
              const auto& input = g_.phases[g_.edges[edge->value].source.value];
              closed &= input.operation && prefix_[input.operation->value];
            }
            if (!first)
              first = a;
            else
              different |= !Same(*first, a);
            formal |= a.kind == AtomKind::kFormal && a.value == d.ordinal;
            if (a.kind == AtomKind::kLiteral) {
              if (literal)
                Need(SameLiteral(*literal, a.literal), Error::kInitializer);
              literal = a.literal;
            }
          });
          if (different && formal && literal && alternatives && closed) {
            if (fallback)
              Need(SameLiteral(*fallback, *literal), Error::kInitializer);
            fallback = literal;
            known_[value.value] = 1;
            atoms_[value.value] = {AtomKind::kInitialized, d.ordinal};
            Append(d.initialized, value);
          } else if (different) {
            known_[value.value] = 1;
            atoms_[value.value] = {AtomKind::kInvalid, value.value};
          } else if (first) {
            known_[value.value] = 1;
            atoms_[value.value] = *first;
          }
        }
        for (const auto& [cell, old] : saved) {
          q_.Step(2 * (1 + std::bit_width(saved.size())));
          const auto present =
              provided_cells.contains(cell) ? provided_cells.at(cell) : old;
          const auto absent = cells_[cell];
          if (present.kind == AtomKind::kFormal && present.value == d.ordinal &&
              absent.kind == AtomKind::kLiteral) {
            if (fallback)
              Need(SameLiteral(*fallback, absent.literal), Error::kInitializer);
            fallback = absent.literal;
            cells_[cell] = {AtomKind::kInitialized, d.ordinal};
          } else
            Need(Same(present, absent), Error::kCapture);
        }
        if (!fallback)
          for (const auto op : d.initializer_ops) {
            q_.Step();
            if (g_.ops[op.value].result) {
              const auto a = Resolve(ValueOperand{*g_.ops[op.value].result});
              if (a.kind == AtomKind::kLiteral) {
                if (fallback)
                  Need(SameLiteral(*fallback, a.literal), Error::kInitializer);
                fallback = a.literal;
              }
            }
          }
        Need(fallback.has_value(), Error::kInitializer);
        d.literal = *fallback;
        if (const auto* number = std::get_if<Float64Bits>(&d.literal))
          Need(std::isfinite(std::bit_cast<double>(number->value)),
               Error::kInitializer);
        if (const auto* special = std::get_if<SpecialValue>(&d.literal))
          Need(*special != SpecialValue::kNaN &&
                   *special != SpecialValue::kInfinity,
               Error::kInitializer);
        // Every missing-arm value must be the same literal, with no other
        // private-state change. A changed accumulator is allowed only if its
        // SSA value is closed by the final use census below.
        for (const auto op : d.initializer_ops) {
          q_.Step();
          const auto& operation = g_.ops[op.value];
          if (operation.result) {
            const auto a = Resolve(ValueOperand{*operation.result});
            if (a.kind == AtomKind::kLiteral &&
                SameLiteral(a.literal, d.literal))
              d.initializer = op;
          }
        }
        if (std::holds_alternative<abc::PoolIndex>(d.literal)) {
          bool found = false;
          for (const auto op : d.initializer_ops) {
            q_.Step();
            if (const auto* text =
                    std::get_if<ValueString>(&g_.ops[op.value].operation);
                text && text->string == std::get<abc::PoolIndex>(d.literal)) {
              d.initializer = op;
              found = true;
              break;
            }
          }
          Need(found, Error::kInitializer);
        }
        Need(g_.ops[d.initializer.value].result.has_value(),
             Error::kInitializer);
        defaulted_[d.ordinal] = 1;
        next = d.ordinal + 1;
        Append(c_.defaults, std::move(d));
        block = join;
      }
    }
  finished:
    Need(!c_.defaults.empty(), Error::kInitializer);
    // A native initialized argument cannot replace an escaping raw argument or
    // a branch-private temporary. Use the original phase SSA, including catch
    // snapshots; never a normal-only liveness approximation.
    std::vector<std::uint8_t> tainted, initialized;
    std::vector<ValueId> queue;
    q_.Array(tainted, g_.values.size());
    q_.Array(initialized, g_.values.size());
    q_.Admit(g_.values.size(), sizeof(ValueId));
    queue.reserve(g_.values.size());
    for (const auto& d : c_.defaults)
      for (const auto v : d.initialized) {
        q_.Step();
        initialized[v.value] = 1;
      }
    const auto seed = [&](ValueId v) {
      if (!initialized[v.value] && !tainted[v.value]) {
        tainted[v.value] = 1;
        queue.push_back(v);
      }
    };
    for (const auto& d : c_.defaults) {
      seed(d.entry);
      if (g_.ops[d.comparison.value].result)
        seed(*g_.ops[d.comparison.value].result);
      for (const auto op : d.initializer_ops)
        if (g_.ops[op.value].result) seed(*g_.ops[op.value].result);
    }
    for (const auto op : c_.prefix)
      if (g_.ops[op.value].result) {
        q_.Step();
        const auto a = Resolve(ValueOperand{*g_.ops[op.value].result});
        if (a.kind == AtomKind::kFormal && defaulted_[a.value])
          seed(*g_.ops[op.value].result);
      }
    for (std::size_t head = 0; head < queue.size(); ++head) {
      q_.Step();
      for (auto at = g_.values[queue[head].value].first_use; at;) {
        q_.Step();
        const auto& use = g_.uses[at->value];
        at = use.next;
        if (use.life != ValueLife::kLive) continue;
        if (const auto* value = std::get_if<ValueId>(&use.owner))
          seed(*value);
        else {
          const auto op = std::get<OpId>(use.owner);
          Need(prefix_[op.value], Error::kEscape);
          if (g_.ops[op.value].result) seed(*g_.ops[op.value].result);
        }
      }
    }
    for (const auto a : cells_) {
      q_.Step();
      Need(!(a.kind == AtomKind::kFormal && defaulted_[a.value]),
           Error::kCapture);
    }
    // No external normal or catch entry may enter the moved prologue.
    for (const auto& e : g_.edges) {
      q_.Step();
      const auto& target = g_.phases[e.target.value];
      const auto& source = g_.phases[e.source.value];
      if (target.operation && prefix_[target.operation->value])
        Need(source.operation && prefix_[source.operation->value],
             Error::kDecision);
    }
    c_.allocated_bytes = q_.Bytes();
    c_.work = q_.Work();
    return std::move(c_);
  }

 private:
  template <class T, class E>
  void CheckResult(const std::variant<T, E>& r) {
    if (const auto* e = std::get_if<E>(&r)) {
      if constexpr (std::is_enum_v<E>) {
        if constexpr (requires { E::kResourceLimit; })
          if (*e == E::kResourceLimit) throw Error::kResourceLimit;
        if constexpr (requires { E::kWorkLimit; })
          if (*e == E::kWorkLimit) throw Error::kWorkLimit;
      } else if constexpr (requires { e->kind; }) {
        if (e->kind == FlowError::kResourceLimit) throw Error::kResourceLimit;
      }
      throw Error::kInput;
    }
  }
  template <class T, class U>
  void Append(std::vector<T>& v, U&& x) {
    if (v.size() == v.capacity()) {
      const auto n = std::max<std::size_t>(1, v.capacity() * 2);
      q_.Admit(n, sizeof(T));
      q_.Step(v.size());
      v.reserve(n);
      q_.Admit(v.capacity() - n, sizeof(T));
    }
    v.push_back(std::forward<U>(x));
  }
  ValueOperand Operand(OpId op, OperandRole role) {
    const auto& o = g_.ops[op.value];
    for (std::uint32_t i = 0; i < o.use_count; ++i) {
      q_.Step();
      const auto& u = g_.uses[o.first_use.value + i];
      if (u.role == role) return u.operand;
    }
    throw Error::kInput;
  }
  Atom Resolve(const ValueOperand& operand) {
    if (const auto* s = std::get_if<SpecialValue>(&operand))
      return {AtomKind::kLiteral, 0, *s};
    if (const auto* n = std::get_if<analysis::IntegerLiteral>(&operand))
      return {AtomKind::kLiteral, 0, *n};
    if (const auto* n = std::get_if<Float64Bits>(&operand))
      return {AtomKind::kLiteral, 0, *n};
    const auto id = ids_.canonical[std::get<ValueId>(operand).value];
    if (known_[id.value]) return atoms_[id.value];
    const auto& v = g_.values[id.value];
    Atom result{AtomKind::kUnknown, id.value};
    if (const auto* entry = std::get_if<EntryDefinition>(&v.origin)) {
      if (const auto* p = std::get_if<ParameterEntry>(entry))
        result = {AtomKind::kFormal, p->index};
    } else if (const auto* op = std::get_if<OpResult>(&v.origin)) {
      const auto& o = g_.ops[op->op.value];
      if (std::holds_alternative<ValueCopy>(o.operation) && o.use_count == 1)
        result = Resolve(g_.uses[o.first_use.value].operand);
      else if (const auto* s = std::get_if<ValueString>(&o.operation);
               s && s->kind == PoolTextKind::kString)
        result = {AtomKind::kLiteral, 0, s->string};
    }
    known_[id.value] = 1;
    atoms_[id.value] = result;
    return result;
  }
  void Set(OpId op, Atom a) {
    if (g_.ops[op.value].result) {
      const auto id = ids_.canonical[g_.ops[op.value].result->value];
      known_[id.value] = 1;
      atoms_[id.value] = a;
    }
  }
  void Replay(OpId op) {
    q_.Step();
    const auto& site = body_.instructions[op.value];
    const auto& expected =
        phases_.TreeInput(body_).input.flow.instructions[op.value];
    Need(site.offset == expected.pc, Error::kCorrespondence);
    const auto lifted = LiftSemanticInstruction(
        site, phases_.Identity().profile, frame_.registers, &frame_);
    const auto* instruction = std::get_if<SemanticInstruction>(&lifted);
    Need(instruction &&
             SamePreludeOperation(instruction->operation, expected.operation),
         Error::kCorrespondence);
  }
  void Mark(OpId op) {
    Need(!prefix_[op.value], Error::kOrder);
    prefix_[op.value] = 1;
    Append(c_.prefix, op);
  }
  std::optional<CellId> CellAt(OpId op) const {
    const auto* p = phases_.Cells();
    return p && op.value < p->operations.size() ? p->operations[op.value]
                                                : std::nullopt;
  }
  bool Execute(OpId op, bool initializer, PhasedParameterDefault* d,
               std::pmr::map<std::uint32_t, Atom>* undo = nullptr) {
    const auto& o = g_.ops[op.value];
    q_.Step();
    if (o.life != ValueLife::kLive) return true;
    const bool push = std::holds_alternative<ValueEnvironmentPush>(o.operation);
    const bool load = std::holds_alternative<ValueLexicalLoad>(o.operation),
               store = std::holds_alternative<ValueLexicalStore>(o.operation);
    const bool pure = o.normal == LanguageEffect::kNone &&
                      o.before_throw == LanguageEffect::kNone &&
                      o.exceptional == ExceptionalCompletion::kImpossible;
    if (!(pure || push || load || store)) return false;
    Replay(op);
    if (push) {
      if (initializer) return false;
      Set(op, {});
    } else if ((load || store) &&
               o.exceptional != ExceptionalCompletion::kImpossible)
      return false;
    else if (load || store) {
      const auto cell = CellAt(op);
      Need(cell.has_value(), Error::kCapture);
      const auto& storage = phases_.Cells()->cells[cell->value].storage;
      Need(std::holds_alternative<AllocatedCell>(storage), Error::kCapture);
      const auto allocation = std::get<AllocatedCell>(storage).allocation;
      Need(prefix_[allocation.value], Error::kCapture);
      if (load)
        Set(op, cells_[cell->value]);
      else {
        const auto a = Resolve(Operand(op, OperandRole::kStoredValue));
        q_.Step(2 * (1 + std::bit_width(cells_.size())));
        if (undo && !undo->contains(cell->value)) {
          undo->emplace(cell->value, cells_[cell->value]);
        }
        cells_[cell->value] = a;
        if (a.kind == AtomKind::kFormal) cell_stores_[cell->value] = op;
        if (initializer) {
          Need(d && a.kind == AtomKind::kLiteral, Error::kInitializer);
          const auto& before = g_.blocks[o.block.value];
          std::optional<WorkBlockId> commit;
          for (std::uint32_t e = 0; e < before.edge_count; ++e) {
            q_.Step();
            const auto target = g_.edges[before.first_edge.value + e].target;
            if (g_.phases[target.value].phase == ExceptionPhase::kCommit)
              commit = target;
          }
          Need(commit.has_value(), Error::kCapture);
          Need(cell_stores_[cell->value].has_value(), Error::kCapture);
          Append(d->captures,
                 PhasedParameterCommit{op, *cell_stores_[cell->value], *cell,
                                       o.block, *commit});
        }
      }
    } else if (const auto* binary = std::get_if<ValueBinary>(&o.operation)) {
      if (initializer) return false;
      if (binary->op != BinaryOperator::kStrictEqual &&
          binary->op != BinaryOperator::kStrictNotEqual)
        return false;
      auto left = Resolve(Operand(op, OperandRole::kLeft)),
           right = Resolve(Operand(op, OperandRole::kRight));
      if (right.kind == AtomKind::kFormal) std::swap(left, right);
      if (left.kind != AtomKind::kFormal || right.kind != AtomKind::kLiteral ||
          !std::holds_alternative<SpecialValue>(right.literal) ||
          std::get<SpecialValue>(right.literal) != SpecialValue::kUndefined)
        return false;
      Set(op, {AtomKind::kTest, left.value, SpecialValue::kUndefined,
               binary->op == BinaryOperator::kStrictEqual});
    } else if (std::holds_alternative<ValueCopy>(o.operation) ||
               std::holds_alternative<ValueString>(o.operation)) {
      if (o.result) {
        const auto a = Resolve(ValueOperand{*o.result});
        if (initializer && a.kind != AtomKind::kLiteral) return false;
      }
    } else if (!std::holds_alternative<ValueNop>(o.operation) &&
               !std::holds_alternative<ValueJump>(o.operation))
      return false;
    Mark(op);
    if (initializer) {
      Need(d, Error::kInitializer);
      Append(d->initializer_ops, op);
    }
    return true;
  }
  void Arm(BlockId at, BlockId join, bool missing, PhasedParameterDefault& d,
           std::pmr::map<std::uint32_t, Atom>& undo) {
    while (at != join) {
      q_.Step();
      Need(at.value < flow_.blocks.size() && !visited_[at.value],
           Error::kDecision);
      visited_[at.value] = 1;
      const auto& b = flow_.blocks[at.value];
      for (std::uint32_t i = b.first_instruction;
           i < b.first_instruction + b.instruction_count; ++i)
        Need(Execute({i}, missing, &d, &undo), Error::kInitializer);
      Need(b.successor_count == 1, Error::kDecision);
      at = b.successors[0].target;
    }
  }
  template <class F>
  void ForPhi(ValueId value, F f) {
    for (const auto u : phi_inputs_[value.value]) {
      q_.Step();
      f(Resolve(g_.uses[u.value].operand), g_.uses[u.value]);
    }
  }
  void Postdominators() {
    const auto n = flow_.blocks.size();
    q_.Array(offsets_, n + 2);
    std::size_t edges = 0;
    for (const auto& b : flow_.blocks) {
      q_.Step();
      if (!b.successor_count) {
        ++offsets_[n + 1];
        ++edges;
      } else
        for (std::uint32_t e = 0; e < b.successor_count; ++e) {
          ++offsets_[b.successors[e].target.value + 1];
          ++edges;
        }
    }
    for (std::size_t i = 1; i < offsets_.size(); ++i) {
      q_.Step();
      offsets_[i] += offsets_[i - 1];
    }
    q_.Array(successors_, edges);
    q_.Admit(offsets_.size(), sizeof(std::uint32_t));
    auto cursors = offsets_;
    for (std::uint32_t b = 0; b < n; ++b) {
      q_.Step();
      const auto& block = flow_.blocks[b];
      if (!block.successor_count)
        successors_[cursors[n]++] = {b};
      else
        for (std::uint32_t e = 0; e < block.successor_count; ++e)
          successors_[cursors[block.successors[e].target.value]++] = {b};
    }
    auto built = BuildDominators({offsets_, successors_},
                                 {static_cast<std::uint32_t>(n)},
                                 {q_.Remaining().bytes, q_.Remaining().work});
    CheckResult(built);
    post_ = std::get<DominatorTree>(std::move(built));
    q_.Admit(post_.work.allocated_bytes, 1);
    q_.Step(post_.work.steps);
    // Sparse phi input lists. Every use is visited once, not once per default.
    q_.Array(phi_inputs_, g_.values.size());
    for (std::uint32_t u = 0; u < g_.uses.size(); ++u) {
      q_.Step();
      if (const auto* v = std::get_if<ValueId>(&g_.uses[u].owner))
        Append(phi_inputs_[v->value], UseId{u});
    }
  }
  const PhasedBody& phases_;
  const abc::MethodBody& body_;
  const ValueProgram& g_;
  const abc::FrameLayout& frame_;
  std::uint32_t length_;
  TreeCheckBudget q_;
  RecoveryScratch scratch_{q_};
  ParameterPlan p_;
  ValueIdentities ids_;
  NormalFlow flow_;
  DominatorTree post_;
  PhasedParameterCertificate c_;
  std::vector<Atom> atoms_, cells_;
  std::vector<std::optional<OpId>> cell_stores_;
  std::vector<std::uint8_t> known_, visited_, prefix_, defaulted_;
  std::vector<std::uint32_t> offsets_, phi_first_;
  std::vector<BlockId> successors_;
  std::vector<ValueId> phis_;
  std::vector<std::vector<UseId>> phi_inputs_;
};
}  // namespace
std::variant<PhasedParameterCertificate, PhasedParameterError>
ProvePhasedParameterDefaults(const PhasedBody& p, const abc::MethodBody& b,
                             const abc::FrameLayout& f, std::uint32_t l,
                             SemanticBudget q) {
  try {
    return Proof(p, b, f, l, q).Run();
  } catch (Error e) {
    return e;
  } catch (TreeStructureError e) {
    return e == TreeStructureError::kWorkLimit ? Error::kWorkLimit
                                               : Error::kResourceLimit;
  } catch (const std::bad_alloc&) {
    return Error::kResourceLimit;
  } catch (const std::length_error&) {
    return Error::kResourceLimit;
  }
}
std::variant<std::uint64_t, PhasedParameterError> VerifyPhasedParameterDefaults(
    const PhasedBody& p, const abc::MethodBody& b, const abc::FrameLayout& f,
    const PhasedParameterCertificate& c, SemanticBudget q) {
  auto fresh = ProvePhasedParameterDefaults(p, b, f, c.length, q);
  if (const auto* e = std::get_if<Error>(&fresh)) return *e;
  const auto& v = std::get<PhasedParameterCertificate>(fresh);
  auto work = v.work;
  const auto charge = [&](std::size_t n) {
    if (n > q.work - work) return false;
    work += n;
    return true;
  };
  if (!charge(v.prefix.size()) || !charge(c.prefix.size()) ||
      !charge(v.defaults.size()) || !charge(c.defaults.size()))
    return Error::kWorkLimit;
  for (const auto& d : v.defaults)
    if (!charge(d.initialized.size()) || !charge(d.initializer_ops.size()) ||
        !charge(d.captures.size()))
      return Error::kWorkLimit;
  for (const auto& d : c.defaults)
    if (!charge(d.initialized.size()) || !charge(d.initializer_ops.size()) ||
        !charge(d.captures.size()))
      return Error::kWorkLimit;
  if (v.formals != c.formals || v.defaults.size() != c.defaults.size() ||
      v.prefix != c.prefix)
    return Error::kCorrespondence;
  for (std::size_t i = 0; i < v.defaults.size(); ++i) {
    const auto& a = v.defaults[i];
    const auto& d = c.defaults[i];
    if (a.ordinal != d.ordinal) return Error::kOrdinal;
    if (a.entry != d.entry || a.initialized != d.initialized)
      return Error::kCorrespondence;
    if (a.comparison != d.comparison || a.decision != d.decision ||
        a.provided != d.provided || a.missing != d.missing || a.join != d.join)
      return Error::kDecision;
    if (a.initializer != d.initializer ||
        a.initializer_ops != d.initializer_ops ||
        !SameLiteral(a.literal, d.literal))
      return Error::kInitializer;
    if (a.captures.size() != d.captures.size()) return Error::kCapture;
    for (std::size_t k = 0; k < a.captures.size(); ++k) {
      const auto& x = a.captures[k];
      const auto& y = d.captures[k];
      if (x.store != y.store || x.initial_store != y.initial_store ||
          x.cell != y.cell || x.before != y.before || x.commit != y.commit)
        return Error::kCapture;
    }
  }
  return work;
}
std::variant<std::shared_ptr<const PhasedParameterDefaults>,
             PhasedParameterError>
PhasedParameterDefaults::Build(PhasedBody p, abc::MethodBody b,
                               const abc::FrameLayout& f, std::uint32_t l,
                               SemanticBudget q) {
  auto proof = ProvePhasedParameterDefaults(p, b, f, l, q);
  if (const auto* e = std::get_if<Error>(&proof)) return *e;
  auto c = std::get<PhasedParameterCertificate>(std::move(proof));
  if (sizeof(PhasedParameterDefaults) > q.bytes - c.allocated_bytes)
    return Error::kResourceLimit;
  if (c.defaults.size() > q.work - c.work) return Error::kWorkLimit;
  try {
    TreeCheckBudget allocation(q);
    allocation.Admit(c.allocated_bytes, 1);
    allocation.Admit(1, sizeof(PhasedParameterDefaults));
    auto owner = std::shared_ptr<PhasedParameterDefaults>(
        new PhasedParameterDefaults(std::move(p), std::move(b), f,
                                    std::move(c)),
        std::default_delete<PhasedParameterDefaults>{},
        RecoveryAllocator<PhasedParameterDefaults>{allocation});
    const auto count = owner->evidence_.defaults.size();
    allocation.Admit(count, sizeof(SignatureString));
    owner->strings_.reserve(count);
    allocation.Admit(owner->strings_.capacity() - count,
                     sizeof(SignatureString));
    for (const auto& d : owner->evidence_.defaults)
      if (const auto* pool = std::get_if<abc::PoolIndex>(&d.literal))
        owner->strings_.push_back({d.initializer, *pool});
    owner->evidence_.work += count;
    owner->evidence_.allocated_bytes = allocation.Bytes();
    return std::shared_ptr<const PhasedParameterDefaults>(std::move(owner));
  } catch (Error error) {
    return error;
  } catch (const TreeStructureError&) {
    return Error::kResourceLimit;
  } catch (const std::bad_alloc&) {
    return Error::kResourceLimit;
  }
}
std::variant<PhasedParameterResidual, PhasedParameterError>
PhasedParameterDefaults::Residual(SemanticBudget q) const {
  auto check =
      VerifyPhasedParameterDefaults(original_, body_, frame_, evidence_, q);
  if (const auto* e = std::get_if<Error>(&check)) return *e;
  const auto bytes = body_.instructions.size() * sizeof(abc::BodyInstruction) +
                     body_.tries.size() * sizeof(abc::TryRegion) +
                     body_.handlers.size() * sizeof(abc::CatchHandler);
  if (bytes > q.bytes) return Error::kResourceLimit;
  const auto work = std::get<std::uint64_t>(check);
  const auto copying = body_.instructions.size() + body_.tries.size() +
                       body_.handlers.size() + evidence_.defaults.size();
  if (copying > q.work - work) return Error::kWorkLimit;
  try {
    auto result = body_;
    const auto& g = original_.Graph();
    for (const auto& d : evidence_.defaults) {
      auto& site = result.instructions[d.decision.value];
      abc::Opcode jump;
      switch (site.decoded.opcode) {
        case abc::Opcode::kJEQZ_IMM8:
        case abc::Opcode::kJNEZ_IMM8:
          jump = abc::Opcode::kJMP_IMM8;
          break;
        case abc::Opcode::kJEQZ_IMM16:
        case abc::Opcode::kJNEZ_IMM16:
          jump = abc::Opcode::kJMP_IMM16;
          break;
        case abc::Opcode::kJEQZ_IMM32:
        case abc::Opcode::kJNEZ_IMM32:
          jump = abc::Opcode::kJMP_IMM32;
          break;
        default:
          return Error::kDecision;
      }
      const auto target = g.edges[d.provided.value].target;
      const auto instruction = g.phases[target.value].instruction;
      const auto pc = body_.instructions[instruction.value].offset;
      const auto delta = std::int64_t(pc.value) - site.offset.value;
      site.decoded.opcode = jump;
      site.decoded.accumulator_read = false;
      site.decoded.accumulator_write = false;
      site.decoded.conditional = false;
      site.decoded.jump = true;
      site.decoded.branch_delta = delta;
      site.branch_target = pc;
      const auto width = site.decoded.operands[0].width_bits;
      if (width == 0 || width > 64) return Error::kDecision;
      const auto mask =
          width == 64 ? UINT64_MAX : (std::uint64_t{1} << width) - 1;
      site.decoded.operands[0].raw_bits =
          static_cast<std::uint64_t>(delta) & mask;
    }
    const auto retained =
        result.instructions.capacity() * sizeof(abc::BodyInstruction) +
        result.tries.capacity() * sizeof(abc::TryRegion) +
        result.handlers.capacity() * sizeof(abc::CatchHandler);
    if (retained > q.bytes) return Error::kResourceLimit;
    return PhasedParameterResidual{std::move(result), retained, work + copying};
  } catch (const std::bad_alloc&) {
    return Error::kResourceLimit;
  }
}
}  // namespace abcd::source
