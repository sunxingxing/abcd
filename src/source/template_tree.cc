#include "template_tree.h"

#include "evaluation_plan.h"
#include "expression_children.h"
#include "original_flow.h"
#include "recovery_scratch.h"
#include "template_literal.h"
namespace abcd::source {
namespace {
constexpr auto kProof = TreeStructureError::kOwnership;
template <class T>
void Copy(std::vector<T>& to, const std::vector<T>& from, TreeCheckBudget& b) {
  b.Array(to, from.size());
  std::copy(from.begin(), from.end(), to.begin());
}
void CopyProof(TemplateConstructions& to, const TemplateConstructions& from,
               TreeCheckBudget& b) {
  Copy(to.owners, from.owners, b);
  Copy(to.hidden, from.hidden, b);
  b.Array(to.sites, from.sites.size());
  for (std::size_t i = 0; i < to.sites.size(); ++i) {
    b.Step();
    const auto& x = from.sites[i];
    auto& y = to.sites[i];
    y.operation = x.operation;
    y.call = x.call;
    y.argument = x.argument;
    Copy(y.quasis, x.quasis, b);
    Copy(y.events, x.events, b);
    Copy(y.exceptions, x.exceptions, b);
  }
}
void CopyText(std::u16string& to, const std::u16string& from,
              TreeCheckBudget& b) {
  b.Admit(from.size() + 1, sizeof(char16_t));
  b.Step(from.size());
  to = from;
  b.Admit(to.capacity() > from.size() ? to.capacity() - from.size() : 0,
          sizeof(char16_t));
}
}  // namespace
void RetainTemplateInputs(const analysis::ValueProgram& g,
                          const EvaluationPlan& p, const FunctionTree& tree,
                          ExpressionConstants constants, OriginalFlow& original,
                          TreeCheckBudget& b) {
  if (!p.templates) return;
  RequireTree(VerifyTemplateConstructions(g, p.aliases, tree.identity.contract,
                                          *p.templates, b),
              kProof);
  auto retained = std::allocate_shared<OriginalTemplates>(
      RecoveryAllocator<OriginalTemplates>{b});
  auto& input = retained->input;
  input.revision = g.revision;
  input.cfg_revision = g.cfg_revision;
  input.arguments = g.arguments;
  Copy(input.blocks, g.blocks, b);
  Copy(input.edges, g.edges, b);
  Copy(input.ops, g.ops, b);
  Copy(input.values, g.values, b);
  Copy(input.uses, g.uses, b);
  Copy(input.origins, g.origins, b);
  Copy(input.exceptional_exits, g.exceptional_exits, b);
  Copy(input.phases, g.phases, b);
  Copy(input.input.sites, g.input.sites, b);
  Copy(input.input.values, g.input.values, b);
  Copy(retained->aliases, p.aliases, b);
  CopyProof(retained->proof, *p.templates, b);
  b.Array(retained->texts, p.templates->sites.size());
  for (std::size_t i = 0; i < p.templates->sites.size(); ++i) {
    b.Step();
    const auto& site = p.templates->sites[i];
    auto& text = retained->texts[i];
    b.Array(text.quasis, site.quasis.size());
    for (std::size_t q = 0; q < site.quasis.size(); ++q) {
      b.Step();
      const auto& quasi = site.quasis[q];
      const auto* raw =
          std::get_if<TextId>(&constants.operations[quasi.raw.value]);
      RequireTree(raw && raw->value < tree.texts.size(), kProof);
      CopyText(text.quasis[q].raw, tree.texts[raw->value], b);
      if (quasi.cooked) {
        const auto* cooked =
            std::get_if<TextId>(&constants.operations[quasi.cooked->value]);
        RequireTree(cooked && cooked->value < tree.texts.size(), kProof);
        text.quasis[q].cooked.emplace();
        CopyText(*text.quasis[q].cooked, tree.texts[cooked->value], b);
      }
    }
  }
  original.templates = std::move(retained);
}
void VerifyTemplateCalls(const FunctionTree& tree, TreeCheckBudget& b) {
  bool found = false;
  for (const auto& n : tree.expressions) {
    b.Step();
    found |= std::holds_alternative<TemplateObjectExpr>(n.expression);
  }
  if (!found) return;
  std::vector<std::uint32_t> uses;
  b.Array(uses, tree.expressions.size());
  for (const auto& n : tree.expressions) {
    b.Step();
    const auto* call = std::get_if<CallExpr>(&n.expression);
    if (!call || call->array_arguments || !call->arguments.count) continue;
    RequireTree(call->arguments.first < tree.arguments.size(), kProof);
    const auto first = tree.arguments[call->arguments.first];
    RequireTree(first.value < tree.expressions.size(), kProof);
    const auto* site = std::get_if<TemplateObjectExpr>(
        &tree.expressions[first.value].expression);
    if (!site) continue;
    RequireTree(site->quasis && site->quasis->size() == call->arguments.count &&
                    !uses[first.value]++,
                kProof);
    VerifyTemplateLiteral(*site, tree.texts, tree.identity.contract, b);
  }
  for (std::size_t i = 0; i < tree.expressions.size(); ++i) {
    b.Step();
    if (std::holds_alternative<TemplateObjectExpr>(
            tree.expressions[i].expression))
      RequireTree(uses[i] == 1, kProof);
  }
}
void VerifyOriginalTemplates(const FunctionTree& tree,
                             const OriginalFlow& original, TreeCheckBudget& b) {
  if (!original.templates) return;
  const auto& retained = *original.templates;
  const auto& g = retained.input;
  RequireTree(
      g.revision == original.value_revision &&
          g.cfg_revision == original.cfg_revision &&
          g.ops.size() == original.operations.size() &&
          g.uses.size() == original.uses.size() &&
          g.blocks.size() == original.blocks.size() &&
          g.edges.size() == original.edges.size() &&
          g.exceptional_exits.size() == original.exceptional_exits.size(),
      kProof);
  for (std::size_t i = 0; i < g.blocks.size(); ++i) {
    b.Step();
    const auto& x = g.blocks[i];
    const auto& y = original.blocks[i];
    RequireTree(x.first_op == y.first_op && x.op_count == y.op_count &&
                    x.first_edge == y.first_edge &&
                    x.edge_count == y.edge_count,
                kProof);
  }
  for (std::size_t i = 0; i < g.uses.size(); ++i) {
    b.Step();
    const auto& x = g.uses[i];
    const auto& y = original.uses[i];
    RequireTree(x.owner == y.owner && x.role == y.role &&
                    x.purpose == y.purpose && x.life == y.life &&
                    x.operand.index() == y.operand.index(),
                kProof);
    const bool same = std::visit(
        [&](const auto& value) {
          using T = std::decay_t<decltype(value)>;
          if constexpr (std::is_same_v<T, analysis::SpecialValue>)
            return value == std::get<T>(y.operand);
          else
            return value.value == std::get<T>(y.operand).value;
        },
        x.operand);
    RequireTree(same, kProof);
  }
  for (std::size_t i = 0; i < g.ops.size(); ++i) {
    b.Step();
    const auto& x = g.ops[i];
    const auto& y = original.operations[i];
    RequireTree(x.block == y.block && x.first_use == y.first_use &&
                    x.use_count == y.use_count && x.result == y.result &&
                    x.normal == y.normal && x.before_throw == y.before_throw &&
                    x.exceptional == y.exceptional && x.life == y.life &&
                    x.origin == y.origin,
                kProof);
  }
  for (std::size_t i = 0; i < g.edges.size(); ++i) {
    b.Step();
    const auto& x = g.edges[i];
    const auto& y = original.edges[i];
    RequireTree(x.source == y.source && x.target == y.target &&
                    x.kind == y.kind && x.successor == y.successor,
                kProof);
  }
  for (std::size_t i = 0; i < g.exceptional_exits.size(); ++i) {
    b.Step();
    const auto& x = g.exceptional_exits[i];
    const auto& y = original.exceptional_exits[i];
    RequireTree(x.op == y.op && x.origin == y.origin &&
                    x.before_throw == y.before_throw && x.handler == y.handler,
                kProof);
  }
  RequireTree(
      VerifyTemplateConstructions(g, retained.aliases, tree.identity.contract,
                                  retained.proof, b),
      kProof);
  VerifyTemplateCalls(tree, b);
  RequireTree(retained.texts.size() == retained.proof.sites.size(), kProof);
  for (std::size_t i = 0; i < retained.proof.sites.size(); ++i) {
    b.Step();
    const auto& site = retained.proof.sites[i];
    const auto object =
        original.operations[site.operation.value].intrinsic_expression;
    const auto call = original.operations[site.call.value].intrinsic_expression;
    RequireTree(object && call && object->value < tree.expressions.size() &&
                    call->value < tree.expressions.size(),
                kProof);
    const auto* actual = std::get_if<TemplateObjectExpr>(
        &tree.expressions[object->value].expression);
    const auto* invoke =
        std::get_if<CallExpr>(&tree.expressions[call->value].expression);
    RequireTree(actual && actual->quasis && invoke &&
                    !invoke->array_arguments &&
                    invoke->arguments.count == site.quasis.size() &&
                    invoke->arguments.first < tree.arguments.size() &&
                    tree.arguments[invoke->arguments.first] == *object,
                kProof);
    const auto& text = retained.texts[i];
    RequireTree(text.quasis.size() == actual->quasis->size(), kProof);
    for (std::size_t q = 0; q < text.quasis.size(); ++q) {
      b.Step();
      const auto& value = (*actual->quasis)[q];
      RequireTree(
          value.raw.value < tree.texts.size() &&
              text.quasis[q].raw == tree.texts[value.raw.value] &&
              value.cooked.has_value() == text.quasis[q].cooked.has_value(),
          kProof);
      if (value.cooked) {
        RequireTree(
            value.cooked->value < tree.texts.size() &&
                *text.quasis[q].cooked == tree.texts[value.cooked->value],
            kProof);
        b.Step(text.quasis[q].cooked->size());
      }
      b.Step(text.quasis[q].raw.size());
    }
  }
}
}  // namespace abcd::source
