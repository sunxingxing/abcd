#include "tree_constants.h"

#include <algorithm>
#include <limits>
#include <new>
#include <stdexcept>

#include "abcd/corpus/abc_file.h"
#include "element_name.h"
#include "object_constants.h"

namespace abcd::source {
namespace {
enum class TextUse : std::uint8_t { kMember, kLiteral };
struct ConstantUse {
  std::variant<abc::PoolIndex, abc::EntityOffset> reference;
  analysis::OpId operation;
  TextUse use;
};
using Error = TreeConstantError;
class ConstantResolver {
 public:
  ConstantResolver(const corpus::AbcFile& file, const abc::Header& header,
      const abc::MetadataIndex& index, abc::EntityOffset method,
      const analysis::ValueProgram& graph, analysis::SemanticBudget budget)
      : file_(file), header_(header), index_(index), method_(method), graph_(graph),
        budget_(budget), bytes_(budget.bytes), work_(budget.work) {}
  TreeConstants Resolve() {
    if (file_.Size() != header_.file_size || file_.CheckUnchanged()) throw abc::MetadataError::kFileChanged;
    if (graph_.ops.size() >= std::numeric_limits<std::uint32_t>::max()) throw Error::kResourceLimit;
    Admit(1, sizeof(ConstantResolver));
    Reserve(uses_, graph_.ops.size());
    Reserve(result_.operations, graph_.ops.size());
    result_.operations.resize(graph_.ops.size());
    std::size_t indexed_names = 0;
    for (const auto& op : graph_.ops) {
      Step(); indexed_names += op.life == analysis::ValueLife::kLive &&
          std::holds_alternative<analysis::ValueIndexedWrite>(op.operation);
    }
    Reserve(result_.names, indexed_names);
    bool needs_number_type = false;
    for (std::size_t i = 0; i < graph_.ops.size(); ++i) {
      Step();
      const auto& op = graph_.ops[i];
      if (op.life != analysis::ValueLife::kLive) continue;
      const analysis::OpId id{static_cast<std::uint32_t>(i)};
      if (const auto* indexed = std::get_if<analysis::ValueIndexedWrite>(&op.operation)) {
        const auto text = std::to_string(indexed->index);
        Admit(1, sizeof(std::u16string)); Admit(text.size(), sizeof(char16_t));
        result_.operations[i] = NameId{static_cast<std::uint32_t>(result_.names.size())};
        result_.names.emplace_back(text.begin(), text.end());
      }
      else if (const auto* read = std::get_if<analysis::ValueNamedRead>(&op.operation))
        uses_.push_back({read->name, id, TextUse::kMember});
      else if (const auto* write = std::get_if<analysis::ValueNamedWrite>(&op.operation))
        uses_.push_back({write->name, id, TextUse::kMember});
      else if (const auto* write = std::get_if<analysis::ValueOwnWrite>(&op.operation))
        uses_.push_back({write->name, id, TextUse::kMember});
      else if (const auto* string = std::get_if<analysis::ValueString>(&op.operation))
        uses_.push_back({string->string, id, TextUse::kLiteral});
      else if (const auto* global = std::get_if<analysis::ValueGlobalRead>(&op.operation))
        uses_.push_back({global->name, id, TextUse::kMember});
      else {
        const auto* unary = std::get_if<analysis::ValueUnary>(&op.operation);
        const auto* branch = std::get_if<analysis::ValueBranch>(&op.operation);
        // Reserving the one language literal even for a proven Boolean is
        // harmless; no source operation or semantic premise is inferred here.
        needs_number_type |= (unary && unary->op == analysis::UnaryOperator::kTrueOrNonzeroNumber) ||
                             (branch && branch->predicate == analysis::UnaryOperator::kTrueOrNonzeroNumber);
      }
    }
    std::sort(uses_.begin(), uses_.end(), [&](const auto& a, const auto& b) {
      Step(); return std::get<abc::PoolIndex>(a.reference) < std::get<abc::PoolIndex>(b.reference);
    });
    for (std::size_t first = 0; first < uses_.size();) {
      Step();
      const auto slot = std::get<abc::PoolIndex>(uses_[first].reference);
      auto end = first + 1;
      while (end < uses_.size() && std::get<abc::PoolIndex>(uses_[end].reference) == slot) { Step(); ++end; }
      const auto resolved = abc::ResolvePoolReference(file_, header_, index_, method_, slot, abc::PoolEntityKind::kString);
      const auto* target = std::get_if<abc::PoolReference>(&resolved);
      if (!target) throw std::get<abc::MetadataError>(resolved);
      for (auto i = first; i < end; ++i) { Step(); uses_[i].reference = target->offset; }
      first = end;
    }
    std::sort(uses_.begin(), uses_.end(), [&](const auto& a, const auto& b) {
      Step();
      const auto left = std::get<abc::EntityOffset>(a.reference);
      const auto right = std::get<abc::EntityOffset>(b.reference);
      return left != right ? left < right : a.use < b.use;
    });
    std::size_t names = 0;
    std::size_t texts = needs_number_type ? 1 : 0;
    for (std::size_t i = 0; i < uses_.size(); ++i) {
      Step();
      if (i != 0 && uses_[i - 1].reference == uses_[i].reference && uses_[i - 1].use == uses_[i].use) continue;
      if (uses_[i].use == TextUse::kMember) ++names;
      else ++texts;
    }
    Reserve(result_.names, names + indexed_names);
    Reserve(result_.texts, texts);
    for (std::size_t first = 0; first < uses_.size();) {
      Step();
      const auto entity = std::get<abc::EntityOffset>(uses_[first].reference);
      auto read = abc::ReadStringEntity(file_, header_, entity, {bytes_});
      auto* text = std::get_if<std::u16string>(&read);
      if (!text) throw std::get<abc::MetadataError>(read);
      Step(text->size());
      // Keep the read payload admitted while copies for both name/literal
      // namespaces coexist. The input reader separately bounds MUTF-8 scratch.
      Admit(text->capacity(), sizeof(char16_t)); Admit(1, sizeof(char16_t));
      std::optional<NameId> name;
      std::optional<TextId> literal;
      auto end = first;
      while (end < uses_.size() && std::get<abc::EntityOffset>(uses_[end].reference) == entity) {
        Step();
        const auto& use = uses_[end++];
        if (use.use == TextUse::kMember) {
          if (!name) {
            if (IsElementName(*text)) throw Error::kElementName;
            name = NameId{static_cast<std::uint32_t>(result_.names.size())};
            Copy(*text, result_.names);
          }
          result_.operations[use.operation.value] = *name;
        } else {
          if (!literal) {
            literal = TextId{static_cast<std::uint32_t>(result_.texts.size())};
            Copy(*text, result_.texts);
          }
          result_.operations[use.operation.value] = *literal;
        }
      }
      first = end;
    }
    if (needs_number_type) {
      result_.number_type_text = TextId{static_cast<std::uint32_t>(result_.texts.size())};
      Copy(u"number", result_.texts);
    }
    const auto objects = ResolveObjectConstants(file_, header_, index_, method_, graph_, result_, {bytes_, work_});
    if (const auto* error = std::get_if<TreeConstantError>(&objects)) throw *error;
    if (const auto* error = std::get_if<abc::MetadataError>(&objects)) throw *error;
    const auto storage = std::get<ObjectConstantStorage>(objects);
    Admit(storage.bytes, 1); Step(storage.work);
    if (file_.CheckUnchanged()) throw abc::MetadataError::kFileChanged;
    result_.allocated_bytes = budget_.bytes - bytes_;
    result_.work = budget_.work - work_;
    result_.method = method_;
    result_.value_revision = graph_.revision;
    result_.cfg_revision = graph_.cfg_revision;
    return std::move(result_);
  }
 private:
  void Step(std::uint64_t count = 1) {
    if (count > work_) throw Error::kWorkLimit;
    work_ -= count;
  }
  void Admit(std::size_t count, std::size_t width) {
    if (count > bytes_ / width) throw Error::kResourceLimit;
    bytes_ -= count * width;
  }
  template <class T> void Reserve(std::vector<T>& array, std::size_t count) {
    Admit(count, sizeof(T)); array.reserve(count); Admit(array.capacity() - count, sizeof(T));
  }
  void Copy(std::u16string_view text, std::vector<std::u16string>& output) {
    Step(text.size());
    Admit(text.size(), sizeof(char16_t)); Admit(1, sizeof(char16_t));
    auto& owned = output.emplace_back(text);
    Admit(owned.capacity() - text.size(), sizeof(char16_t));
  }
  const corpus::AbcFile& file_;
  const abc::Header& header_;
  const abc::MetadataIndex& index_;
  abc::EntityOffset method_;
  const analysis::ValueProgram& graph_;
  analysis::SemanticBudget budget_;
  std::size_t bytes_;
  std::uint64_t work_;
  TreeConstants result_;
  std::vector<ConstantUse> uses_;
};
}  // namespace
TreeConstantResult ResolveTreeConstants(const corpus::AbcFile& file,
    const abc::Header& header, const abc::MetadataIndex& index,
    abc::EntityOffset method, const analysis::ValueProgram& graph,
    analysis::SemanticBudget budget) {
  try { return ConstantResolver(file, header, index, method, graph, budget).Resolve();
  } catch (Error error) { return error;
  } catch (abc::MetadataError error) { return error;
  } catch (const std::bad_alloc&) { return Error::kResourceLimit;
  } catch (const std::length_error&) { return Error::kResourceLimit; }
}
}  // namespace abcd::source
