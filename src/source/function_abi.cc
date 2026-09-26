#include "function_abi.h"

#include <limits>
#include <new>
#include <stdexcept>
#include <type_traits>

namespace abcd::source {
namespace {
using Error = FunctionAbiError;
void Require(bool condition, Error error) { if (!condition) throw error; }
class AbiBuilder {
 public:
  AbiBuilder(const analysis::ValueProgram& graph, const FunctionTree& tree,
             const abc::FrameLayout& frame, analysis::SemanticBudget budget)
      : graph_(graph), tree_(tree), frame_(frame), initial_(budget), remaining_(budget) {}
  ConstructedAbi Build(std::uint32_t length) {
    Require(graph_.revision == tree_.value_revision && graph_.cfg_revision == tree_.cfg_revision, Error::kRevision);
    Require(tree_.parameters.size() == frame_.parameters && length == frame_.parameters, Error::kParameter);
    Require(!tree_.self, Error::kSyntax);
    Array(result_.parameters, frame_.parameters);
    Array(formals_, tree_.symbols.size());
    result_.length = length;
    for (std::size_t i = 0; i < tree_.parameters.size(); ++i) {
      Step();
      const auto& parameter = tree_.parameters[i];
      Require(!parameter.initializer && !parameter.rest && parameter.symbol.value < formals_.size(), Error::kParameter);
      Require(!formals_[parameter.symbol.value] &&
          tree_.symbols[parameter.symbol.value].kind == BindingKind::kParameter, Error::kParameter);
      formals_[parameter.symbol.value] = static_cast<std::uint32_t>(i);
      result_.parameters[i].symbol = parameter.symbol;
    }
    // SSA uses carry already-simplified entry identities. Do not count removed
    // MOVs as source ABI uses or use their register destinations as symbols.
    for (const auto& use : graph_.uses) {
      Step();
      if (use.life != analysis::ValueLife::kLive) continue;
      const auto* id = std::get_if<analysis::ValueId>(&use.operand);
      if (!id) continue;
      Require(id->value < graph_.values.size(), Error::kEntry);
      const auto& definition = graph_.values[id->value];
      Require(definition.life == analysis::ValueLife::kLive, Error::kEntry);
      if (const auto* entry = std::get_if<analysis::EntryDefinition>(&definition.origin)) {
        Require(use.purpose == analysis::ReadPurpose::kOperand, Error::kEntry);
        std::visit([&](const auto& value) { Entry(value); }, *entry);
      }
    }
    for (const auto& expression : tree_.expressions) {
      Step();
      std::visit([&](const auto& value) { Expression(value); }, expression.expression);
    }
    for (const auto& parameter : result_.parameters) {
      Step();
      Require((parameter.reads.operands != 0) == (parameter.reads.references != 0) &&
          parameter.writes == 0, Error::kParameter);
    }
    Require((result_.receiver.operands != 0) == (result_.receiver.references != 0), Error::kReceiver);
    return {std::move(result_), initial_.bytes - remaining_.bytes,
            initial_.work - remaining_.work};
  }
 private:
  void Step(std::uint64_t count = 1) {
    Require(count <= remaining_.work, Error::kWorkLimit); remaining_.work -= count;
  }
  void Admit(std::size_t count, std::size_t width) {
    Require(count <= remaining_.bytes / width, Error::kResourceLimit);
    remaining_.bytes -= count * width;
  }
  template <class T> void Array(std::vector<T>& values, std::size_t count) {
    Admit(count, sizeof(T)); Step(count); values.resize(count);
    Admit(values.capacity() - count, sizeof(T));
  }
  static void Increment(std::uint64_t& count) {
    Require(count != std::numeric_limits<std::uint64_t>::max(), Error::kResourceLimit);
    ++count;
  }
  void Entry(analysis::ParameterEntry value) {
    Require(value.index < result_.parameters.size(), Error::kParameter);
    Increment(result_.parameters[value.index].reads.operands);
  }
  void Entry(analysis::ReceiverEntry) {
    Require(frame_.this_value.has_value(), Error::kReceiver);
    Increment(result_.receiver.operands);
  }
  void Entry(analysis::CallableEntry) { throw Error::kEntry; }
  void Entry(analysis::NewTargetEntry) { throw Error::kEntry; }
  void Entry(const analysis::UnknownEntry&) { throw Error::kEntry; }
  void Symbol(SymbolId id, bool write) {
    Require(id.value < formals_.size(), Error::kParameter);
    if (const auto ordinal = formals_[id.value]) {
      auto& parameter = result_.parameters[*ordinal];
      Increment(write ? parameter.writes : parameter.reads.references);
    } else {
      // No implicit self/arguments binding may appear under a local spelling.
      const auto kind = tree_.symbols[id.value].kind;
      Require(kind == BindingKind::kCompilerTemporary || kind == BindingKind::kLocal, Error::kSyntax);
    }
  }
  void Expression(SymbolExpr node) { Symbol(node.symbol, false); }
  void Expression(ThisExpr node) {
    Require(node.owner == tree_.owner && frame_.this_value.has_value(), Error::kReceiver);
    Increment(result_.receiver.references);
  }
  void Expression(const AssignmentExpr& node) {
    if (const auto* target = std::get_if<SymbolTarget>(&node.target)) Symbol(target->symbol, true);
  }
  template <class T> void Expression(const T&) {
    static_assert(std::is_same_v<T, TreeLiteral> || std::is_same_v<T, MemberExpr> ||
        std::is_same_v<T, UnaryExpr> || std::is_same_v<T, BinaryExpr> ||
        std::is_same_v<T, LogicalExpr>);
    // Child expressions are counted once by their arena entries, not recursively.
  }
  const analysis::ValueProgram& graph_;
  const FunctionTree& tree_;
  const abc::FrameLayout& frame_;
  analysis::SemanticBudget initial_;
  analysis::SemanticBudget remaining_;
  FunctionAbi result_;
  std::vector<std::optional<std::uint32_t>> formals_;
};
}  // namespace

std::variant<ConstructedAbi, FunctionAbiError> BuildFunctionAbi(
    const analysis::ValueProgram& graph, const FunctionTree& tree,
    const abc::FrameLayout& frame, std::uint32_t length,
    analysis::SemanticBudget budget) {
  try { return AbiBuilder(graph, tree, frame, budget).Build(length);
  } catch (Error error) { return error;
  } catch (const std::bad_alloc&) { return Error::kResourceLimit;
  } catch (const std::length_error&) { return Error::kResourceLimit; }
}
}  // namespace abcd::source
