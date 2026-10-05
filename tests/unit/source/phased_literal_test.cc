#include "../../../src/analysis/phased_value_import.h"
#include "../../../src/analysis/value_edits.h"
#include "../../../src/analysis/value_flow.h"
#include "../../../src/analysis/value_phi.h"
#include "../../../src/source/evaluation_plan.h"
#include "../../../src/source/effect_order.h"
#include "../../../src/source/exception_tree.h"
#include "../../../src/source/tree_semantics.h"
#include "../../../src/source/logical_regions.h"
#include "../../../src/source/tree_checks.h"
#include "analysis/ssa_fixture.h"
#include "abcd/source/tree_printer.h"
#include "abcd/analysis/exception_locals.h"

#include <algorithm>
#include <iostream>
#include <sstream>

namespace {
using namespace abcd;
using namespace abcd::analysis;
using namespace abcd::source;
enum class Shape {
  kAllPath, kAllPathArray, kAllPathBypass, kAllPathBarrier, kAllPathLoop, kAllPathCatchObserve,
  kStagedRead, kStagedReadBarrier, kStagedSelectionBarrier, kStagedSelectionUnary, kStagedSelectionAlias, kStagedLateSnapshot,
  kArmSelection, kArmSelectionAlias, kArmSelectionBarrier, kArmSelectionDispatch,
  kSelectionPredicate, kSelectionPredicateBarrier, kSelectionPredicateDispatch, kSelectionPredicateUnary, kSelectionPredicateChain, kPredicateRead, kPredicateReadBarrier, kPredicateReadBypass, kPredicateReadAlias, kPredicateReadLoop,
  kEarlyPhi, kLatePhi,
  kStagedCall, kStagedComputed, kStagedSelection, kStaged, kStagedObserve, kStagedCatchObserve, kReturn, kCatch, kObserve, kLate, kBranch, kCatchObserve, kNested, kNestedCatch,
  kNestedCatchObserve, kNestedDispatch, kNestedShared, kNestedSnapshot, kUnary, kUnaryCatch,
  kUnaryShared, kUnaryUnknown, kNestedDeadCopy, kNestedDeadArray, kGlobalCatch, kGlobalDispatch, kLexicalCatch, kBinaryCatch
};
std::uint64_t Run(abc::ProfileId profile, Shape shape = Shape::kReturn,
                  std::uint32_t copies = 0, std::uint32_t fields = 1,
                  abc::Opcode define = abc::Opcode::kSTOWNBYNAME_IMM8_ID16_V8,
                  abc::Opcode unary = abc::Opcode::kNEG_IMM8, bool print = false,
                  bool forward_phi = true, std::uint32_t selections = 2) {
  const auto op = [](abc::Opcode code) { return static_cast<std::uint8_t>(code); };
  const bool all_path = shape == Shape::kAllPath || shape == Shape::kAllPathArray ||
      shape == Shape::kAllPathBypass || shape == Shape::kAllPathBarrier || shape == Shape::kAllPathLoop || shape == Shape::kAllPathCatchObserve;
  const bool array = shape == Shape::kAllPathArray;
  const bool all_path_negative = shape == Shape::kAllPathBypass || shape == Shape::kAllPathBarrier || shape == Shape::kAllPathLoop || shape == Shape::kAllPathCatchObserve;
  const bool early_phi = shape == Shape::kEarlyPhi;
  const bool late_phi = shape == Shape::kLatePhi;
  const bool phi = early_phi || late_phi;
  constexpr std::uint8_t kObject = 0, kPhi = 1;
  const bool arm_selection = shape == Shape::kArmSelection || shape == Shape::kArmSelectionAlias ||
      shape == Shape::kArmSelectionBarrier || shape == Shape::kArmSelectionDispatch;
  const bool arm_alias = shape == Shape::kArmSelectionAlias;
  const bool selected_unary = shape == Shape::kStagedSelectionUnary;
  const bool selection_barrier = shape == Shape::kStagedSelectionBarrier;
  const bool selected_alias = shape == Shape::kStagedSelectionAlias;
  const bool late_snapshot = shape == Shape::kStagedLateSnapshot;
  const bool selection_predicate = shape == Shape::kSelectionPredicate || shape == Shape::kSelectionPredicateBarrier ||
      shape == Shape::kSelectionPredicateDispatch || shape == Shape::kSelectionPredicateUnary || shape == Shape::kSelectionPredicateChain;
  const bool predicate_bypass = shape == Shape::kPredicateReadBypass;
  const bool predicate_alias = shape == Shape::kPredicateReadAlias;
  const bool predicate_loop = shape == Shape::kPredicateReadLoop;
  const bool predicate_register = predicate_bypass || predicate_alias || predicate_loop;
  const bool predicate_read = shape == Shape::kPredicateRead || shape == Shape::kPredicateReadBarrier || predicate_register;
  const std::uint8_t kParameter = phi || selection_barrier || selected_alias || late_snapshot || predicate_register || selection_predicate || arm_alias ? 5 : 4;
  std::vector<std::uint8_t> bytes{op(abc::Opcode::kCREATEEMPTYOBJECT), op(abc::Opcode::kSTA_V8), kObject};
  if (array) bytes = {op(abc::Opcode::kCREATEEMPTYARRAY_IMM8), 0,
      op(abc::Opcode::kSTA_V8), kObject};
  const bool read = shape == Shape::kStagedRead || shape == Shape::kStagedReadBarrier;
  const bool staged = arm_selection || selection_predicate || predicate_read || read || selection_barrier || selected_unary || selected_alias || late_snapshot || shape == Shape::kStagedCall || shape == Shape::kStagedComputed || shape == Shape::kStagedSelection || shape == Shape::kStaged || shape == Shape::kStagedObserve || shape == Shape::kStagedCatchObserve;
  if (staged) bytes = {op(abc::Opcode::kCREATEOBJECTWITHBUFFER_IMM8_ID16), 0, 0, 0,
      op(abc::Opcode::kSTA_V8), kObject};
  const bool binary = shape == Shape::kBinaryCatch;
  const bool lexical = shape == Shape::kLexicalCatch;
  std::uint32_t lexical_try_begin = 0;
  if (lexical) {
    const std::vector<std::uint8_t> prelude{op(abc::Opcode::kNEWLEXENV_IMM8), 1,
        op(abc::Opcode::kLDA_V8), static_cast<std::uint8_t>(kParameter + 1), op(abc::Opcode::kSTLEXVAR_IMM4_IMM4), 0};
    lexical_try_begin = static_cast<std::uint32_t>(prelude.size());
    bytes.insert(bytes.begin(), prelude.begin(), prelude.end());
  }
  const bool dead_copy = shape == Shape::kNestedDeadCopy || shape == Shape::kNestedDeadArray;
  const bool global = shape == Shape::kGlobalCatch || shape == Shape::kGlobalDispatch;
  const bool nested = shape == Shape::kNested || shape == Shape::kNestedCatch ||
      shape == Shape::kNestedCatchObserve || shape == Shape::kNestedDispatch || shape == Shape::kNestedShared || shape == Shape::kNestedSnapshot ||
      dead_copy || global || lexical;
  const bool arithmetic = shape == Shape::kUnary || shape == Shape::kUnaryCatch ||
      shape == Shape::kUnaryShared || shape == Shape::kUnaryUnknown;
  const bool numeric_update = unary == abc::Opcode::kTONUMERIC_IMM8 ||
      unary == abc::Opcode::kINC_IMM8 || unary == abc::Opcode::kDEC_IMM8;
  const bool caught = all_path || phi || staged || shape == Shape::kCatch || shape == Shape::kCatchObserve ||
      shape == Shape::kNestedCatch || shape == Shape::kNestedCatchObserve || shape == Shape::kNestedDispatch || shape == Shape::kNestedSnapshot || shape == Shape::kUnaryCatch ||
      dead_copy || global || lexical || binary;
  constexpr std::uint8_t kSaved = 2, kSnapshotInput = 6, kSnapshotPrelude = 4;
  if (shape == Shape::kNestedSnapshot) bytes.insert(bytes.begin(),
      {op(abc::Opcode::kLDA_V8), kSnapshotInput, op(abc::Opcode::kSTA_V8), kSaved});
  const auto initialized_pc = static_cast<std::uint32_t>(bytes.size());
  if (selected_alias) bytes.insert(bytes.end(), {op(abc::Opcode::kMOV_V8_V8), kPhi, kObject});
  if (late_snapshot) bytes.insert(bytes.end(), {op(abc::Opcode::kLDA_V8), kParameter,
      op(abc::Opcode::kTYPEOF_IMM8), 0, op(abc::Opcode::kSTA_V8), kPhi});
  if (shape == Shape::kNestedSnapshot) bytes.insert(bytes.end(),
      {op(abc::Opcode::kLDTRUE), op(abc::Opcode::kSTA_V8), kSaved});
  for (std::uint32_t i = 0; i < (dead_copy ? 0 : copies); ++i)
    bytes.insert(bytes.end(), {op(abc::Opcode::kMOV_V8_V8), kObject, kObject});
  if (shape == Shape::kObserve || shape == Shape::kStagedObserve) bytes.insert(bytes.end(), {op(abc::Opcode::kLDA_V8), kObject,
      op(abc::Opcode::kLDOBJBYNAME_IMM8_ID16), 0, 0, 0});
  std::optional<std::uint32_t> selection_try_begin;
  const auto selection = [&] {
    if (arm_selection) {
      // Every false edge and the final true edge supply the same original
      // phi. Scale nesting without introducing an intermediate SSA value.
      std::vector<std::size_t> exits;
      const auto jump = [&](abc::Opcode code) {
        const auto at = bytes.size(); bytes.insert(bytes.end(), {op(code), 0, 0, 0, 0}); return at;
      };
      const auto target = [&](std::size_t at, std::size_t destination) {
        const auto offset = static_cast<std::uint32_t>(destination - at);
        for (unsigned byte = 0; byte < sizeof(offset); ++byte)
          bytes[at + 1 + byte] = static_cast<std::uint8_t>(offset >> (byte * 8));
      };
      for (std::uint32_t level = 0; level < selections; ++level) {
        bytes.insert(bytes.end(), {op(abc::Opcode::kLDA_V8), kParameter});
        if (level) {
          bytes.insert(bytes.end(), {op(abc::Opcode::kLDOBJBYNAME_IMM8_ID16), 0, 1, 0});
          if (shape == Shape::kArmSelectionBarrier) bytes.push_back(op(abc::Opcode::kDEBUGGER));
          if (arm_alias) {
            bytes.insert(bytes.end(), {op(abc::Opcode::kSTA_V8), kPhi});
            for (std::uint32_t copy = 0; copy < copies; ++copy)
              bytes.insert(bytes.end(), {op(abc::Opcode::kMOV_V8_V8), kPhi, kPhi, op(abc::Opcode::kJMP_IMM8), 2});
            bytes.insert(bytes.end(), {op(abc::Opcode::kLDA_V8), kPhi});
          }
        }
        bytes.push_back(op(abc::Opcode::kISTRUE));
        const auto selected = jump(abc::Opcode::kJNEZ_IMM32);
        bytes.push_back(op(abc::Opcode::kLDNULL)); exits.push_back(jump(abc::Opcode::kJMP_IMM32));
        target(selected, bytes.size());
      }
      bytes.insert(bytes.end(), {op(abc::Opcode::kLDAI_IMM32), 7, 0, 0, 0});
      for (const auto exit : exits) target(exit, bytes.size());
      // A common join enters try after the first family's getter/decision.
      // Its stored-value consumer has a different original catch signature.
      if (shape == Shape::kArmSelectionDispatch && !selection_try_begin)
        selection_try_begin = static_cast<std::uint32_t>(bytes.size());
      return;
    }
    if (selection_predicate) {
      bytes.insert(bytes.end(), {op(abc::Opcode::kLDA_V8), kParameter,
          op(abc::Opcode::kISTRUE), op(abc::Opcode::kJNEZ_IMM8), 5,
          op(abc::Opcode::kLDNULL), op(abc::Opcode::kJMP_IMM8), 8,
          op(abc::Opcode::kLDA_V8), kParameter, op(abc::Opcode::kLDOBJBYNAME_IMM8_ID16), 0, 1, 0,
          op(abc::Opcode::kSTA_V8), kPhi});
      for (std::uint32_t copy = 0; copy < copies; ++copy)
        bytes.insert(bytes.end(), {op(abc::Opcode::kMOV_V8_V8), kPhi, kPhi,
            op(abc::Opcode::kJMP_IMM8), 2});
      if (shape == Shape::kSelectionPredicateDispatch && !selection_try_begin)
        selection_try_begin = static_cast<std::uint32_t>(bytes.size());
      if (shape == Shape::kSelectionPredicateUnary) bytes.insert(bytes.end(), {
          op(abc::Opcode::kLDA_V8), kPhi, op(abc::Opcode::kTYPEOF_IMM8), 0,
          op(abc::Opcode::kSTA_V8), kPhi});
      if (shape == Shape::kSelectionPredicateBarrier) bytes.push_back(op(abc::Opcode::kDEBUGGER));
      const auto depth = shape == Shape::kSelectionPredicateChain ? selections : 2U;
      test::Check(depth >= 2);
      for (std::uint32_t level = 1; level < depth; ++level) {
        bytes.insert(bytes.end(), {op(abc::Opcode::kLDNULL), op(abc::Opcode::kSTRICTEQ_IMM8_V8), 0, kPhi,
            op(abc::Opcode::kJEQZ_IMM8), 9, op(abc::Opcode::kLDAI_IMM32), 7, 0, 0, 0,
            op(abc::Opcode::kJMP_IMM8), 3, op(abc::Opcode::kLDNULL)});
        if (level + 1 != depth) bytes.insert(bytes.end(), {op(abc::Opcode::kSTA_V8), kPhi});
      }
      return;
    }
    if (predicate_read) {
      bytes.insert(bytes.end(), {op(abc::Opcode::kLDA_V8), kParameter,
          op(abc::Opcode::kLDOBJBYNAME_IMM8_ID16), 0, 1, 0});
      for (std::uint32_t copy = 0; copy < copies; ++copy)
        bytes.insert(bytes.end(), {op(abc::Opcode::kMOV_V8_V8), kObject, kObject,
            op(abc::Opcode::kJMP_IMM8), 2});
      if (shape == Shape::kPredicateReadBarrier) bytes.push_back(op(abc::Opcode::kDEBUGGER));
      if (predicate_alias) bytes.insert(bytes.end(), {op(abc::Opcode::kSTA_V8), kPhi,
          op(abc::Opcode::kMOV_V8_V8), kPhi, kPhi, op(abc::Opcode::kLDA_V8), kPhi});
      if (predicate_loop) bytes.insert(bytes.end(), {op(abc::Opcode::kSTA_V8), kPhi,
          op(abc::Opcode::kLDA_V8), kParameter, op(abc::Opcode::kISTRUE),
          op(abc::Opcode::kJNEZ_IMM8), 4, op(abc::Opcode::kJMP_IMM8), static_cast<std::uint8_t>(-5),
          op(abc::Opcode::kLDA_V8), kPhi});
      if (predicate_bypass) bytes.insert(bytes.end(), {op(abc::Opcode::kSTA_V8), kPhi,
          op(abc::Opcode::kLDA_V8), kParameter, op(abc::Opcode::kISTRUE),
          op(abc::Opcode::kJNEZ_IMM8), 4, op(abc::Opcode::kLDNULL), op(abc::Opcode::kRETURN),
          op(abc::Opcode::kLDA_V8), kPhi});
      bytes.insert(bytes.end(), {op(abc::Opcode::kJEQZ_IMM8), 9,
          op(abc::Opcode::kLDAI_IMM32), 7, 0, 0, 0,
          op(abc::Opcode::kJMP_IMM8), 3, op(abc::Opcode::kLDNULL)});
      return;
    }
    if (shape == Shape::kStagedComputed || shape == Shape::kStagedCall) {
      bytes.insert(bytes.end(), {op(abc::Opcode::kLDA_V8), kParameter,
          op(abc::Opcode::kISTRUE), op(abc::Opcode::kJNEZ_IMM8), 5,
          op(abc::Opcode::kLDNULL), op(abc::Opcode::kJMP_IMM8), static_cast<std::uint8_t>(shape == Shape::kStagedCall ? 11 : 8),
          op(abc::Opcode::kLDA_V8), kParameter, op(abc::Opcode::kLDOBJBYNAME_IMM8_ID16), 0, 1, 0});
      if (shape == Shape::kStagedCall) bytes.insert(bytes.end(), {op(abc::Opcode::kCALLTHIS0_IMM8_V8), 0, kParameter});
      return;
    }
    bytes.insert(bytes.end(), {
        op(abc::Opcode::kLDA_V8), kParameter, op(abc::Opcode::kJEQZ_IMM8), 9,
        op(abc::Opcode::kLDAI_IMM32), 7, 0, 0, 0,
        op(abc::Opcode::kJMP_IMM8), 3, op(abc::Opcode::kLDNULL)});
  };
  if (all_path) {
    selection();
    for (std::uint32_t c = 0; c < copies; ++c)
      bytes.insert(bytes.end(), {op(abc::Opcode::kMOV_V8_V8), kObject, kObject,
          op(abc::Opcode::kJMP_IMM8), 2});
  }
  std::optional<std::size_t> bypass;
  if (shape == Shape::kAllPathBypass) {
    bytes.insert(bytes.end(), {op(abc::Opcode::kLDA_V8), kParameter,
        op(abc::Opcode::kJEQZ_IMM8), 0});
    bypass = bytes.size() - 1;
  }
  if (shape == Shape::kAllPathBarrier) bytes.insert(bytes.end(), {
      op(abc::Opcode::kLDA_V8), kParameter, op(abc::Opcode::kLDOBJBYNAME_IMM8_ID16), 0, 1, 0});
  if (shape == Shape::kAllPathLoop) bytes.insert(bytes.end(), {
      op(abc::Opcode::kLDA_V8), kParameter, op(abc::Opcode::kJEQZ_IMM8), 0});
  if (phi) {
    if (early_phi) bytes.clear();
    selection();
    bytes.insert(bytes.end(), {op(abc::Opcode::kSTA_V8), kPhi});
    // Copies and block splits transport the same SSA input; none defines the
    // recovery rule. Keep the allocation after the joined phi only in early.
    for (std::uint32_t i = 0; i < copies; ++i)
      bytes.insert(bytes.end(), {op(abc::Opcode::kMOV_V8_V8), kPhi, kPhi,
          op(abc::Opcode::kJMP_IMM8), 2});
    if (early_phi) bytes.insert(bytes.end(), {op(abc::Opcode::kCREATEEMPTYOBJECT),
        op(abc::Opcode::kSTA_V8), kObject});
  }
  if (read) {
    // A prior selected initializer keeps this object staged. The later
    // read/write suffix has its own unchanged effect and catch contract.
    selection();
    bytes.insert(bytes.end(), {op(define), 0, 0, 0, kObject,
        op(abc::Opcode::kLDA_V8), kParameter,
        op(abc::Opcode::kLDOBJBYNAME_IMM8_ID16), 0, 1, 0});
    if (shape == Shape::kStagedReadBarrier) bytes.push_back(op(abc::Opcode::kDEBUGGER));
  }
  if (staged && !arm_selection && !selection_predicate && !predicate_read && !read && !selection_barrier && !selected_unary && !selected_alias && !late_snapshot && shape != Shape::kStagedSelection && shape != Shape::kStagedComputed && shape != Shape::kStagedCall) bytes.insert(bytes.end(), {
      op(abc::Opcode::kLDA_V8), kParameter, op(abc::Opcode::kJEQZ_IMM8), 10,
      op(abc::Opcode::kLDA_V8), kParameter, op(abc::Opcode::kLDOBJBYNAME_IMM8_ID16), 0, 1, 0,
      op(abc::Opcode::kJMP_IMM8), 3, op(abc::Opcode::kLDNULL)});
  if (shape == Shape::kBranch) bytes.insert(bytes.end(), {op(abc::Opcode::kJEQZ_IMM8), 2});
  if (!nested && !arithmetic && !staged && !phi) bytes.insert(bytes.end(), {op(abc::Opcode::kLDA_V8), kParameter});
  if (shape == Shape::kLate) bytes.insert(bytes.end(), {op(abc::Opcode::kLDOBJBYNAME_IMM8_ID16), 0, 0, 0});
  for (std::uint32_t i = 0; i < fields; ++i) {
    if (phi) bytes.insert(bytes.end(), {op(abc::Opcode::kLDA_V8), kPhi});
    if (arm_selection || selection_predicate || predicate_read || selection_barrier || selected_unary || selected_alias || (late_snapshot && i == 0) || shape == Shape::kStagedSelection || shape == Shape::kStagedComputed || shape == Shape::kStagedCall) {
      selection();
      if (selection_barrier) bytes.insert(bytes.end(), {op(abc::Opcode::kSTA_V8), kPhi,
          op(abc::Opcode::kLDA_V8), kParameter, op(abc::Opcode::kLDOBJBYNAME_IMM8_ID16), 0, 2, 0,
          op(abc::Opcode::kLDA_V8), kPhi});
      // Preserve the selected accumulator while changing register transport
      // and splitting its route to the consumer. Layout cannot define ownership.
      for (std::uint32_t c = 0; c < copies; ++c)
        bytes.insert(bytes.end(), {op(abc::Opcode::kMOV_V8_V8), kObject, kObject,
            op(abc::Opcode::kJMP_IMM8), 2});
    }
    if (late_snapshot && i != 0) bytes.insert(bytes.end(), {op(abc::Opcode::kLDA_V8), kPhi});
    if (selected_unary) bytes.insert(bytes.end(), {op(abc::Opcode::kTYPEOF_IMM8), 0});
    constexpr std::uint8_t kInner = 1;
    if (nested) bytes.insert(bytes.end(), {op(abc::Opcode::kCREATEEMPTYOBJECT), op(abc::Opcode::kSTA_V8), kInner});
    if (dead_copy) {
      if (shape == Shape::kNestedDeadArray)
        bytes.insert(bytes.end(), {op(abc::Opcode::kCREATEEMPTYARRAY_IMM8), 0, op(abc::Opcode::kSTA_V8), kSaved});
      else bytes.insert(bytes.end(), {op(abc::Opcode::kMOV_V8_V8), kSaved, kInner});
      for (std::uint32_t c = 0; c < copies; ++c)
        bytes.insert(bytes.end(), {op(abc::Opcode::kMOV_V8_V8), kSaved, kSaved});
    }
    if (lexical) {
      constexpr auto kGuard = static_cast<std::uint16_t>(abc::Opcode::kTHROW_UNDEFINEDIFHOLEWITHNAME_PREF_ID16);
      constexpr unsigned kByteBits = 8;
      bytes.insert(bytes.end(), {op(abc::Opcode::kLDLEXVAR_IMM4_IMM4), 0,
          static_cast<std::uint8_t>(kGuard), static_cast<std::uint8_t>(kGuard >> kByteBits), 0, 0});
    } else if (global) bytes.insert(bytes.end(), {op(abc::Opcode::kTRYLDGLOBALBYNAME_IMM8_ID16), 0, 1, 0,
        op(abc::Opcode::kLDOBJBYNAME_IMM8_ID16), 0, 2, 0});
    else if (nested && shape != Shape::kNestedDeadArray) bytes.insert(bytes.end(), {op(abc::Opcode::kLDA_V8),
        static_cast<std::uint8_t>(shape == Shape::kNestedSnapshot ? kSnapshotInput :
            dead_copy ? kParameter + 2 : kParameter + 1)});
    if (binary) bytes.insert(bytes.end(), {op(abc::Opcode::kLDA_STR_ID16), 0, 0,
        op(abc::Opcode::kADD2_IMM8_V8), 0, kParameter});
    if (arithmetic) {
      if (shape != Shape::kUnaryUnknown &&
          (unary == abc::Opcode::kNEG_IMM8 || unary == abc::Opcode::kTONUMBER_IMM8 || numeric_update))
        bytes.insert(bytes.end(), {op(abc::Opcode::kLDAI_IMM32), 1, 0, 0, 0});
      else bytes.insert(bytes.end(), {op(abc::Opcode::kLDA_V8), kParameter});
      bytes.push_back(op(unary));
      if (unary != abc::Opcode::kISTRUE && unary != abc::Opcode::kISFALSE) bytes.push_back(0);
    }
    if (array) {
      constexpr unsigned kByteBits = 8;
      bytes.insert(bytes.end(), {op(abc::Opcode::kSTOWNBYINDEX_IMM8_V8_IMM16), 0, kObject,
          static_cast<std::uint8_t>(i), static_cast<std::uint8_t>(i >> kByteBits)});
      continue;
    }
    bytes.insert(bytes.end(), {op(define), 0});
    if (define == abc::Opcode::kSTOWNBYNAME_IMM16_ID16_V8) bytes.push_back(0);
    bytes.insert(bytes.end(), {0, 0, nested ? kInner : selected_alias ? kPhi : kObject});
    if (shape == Shape::kUnaryShared) bytes.insert(bytes.end(),
        {op(define), 0, 0, 0, kObject});
    if (nested) bytes.insert(bytes.end(), {op(abc::Opcode::kLDA_V8), kInner,
        op(abc::Opcode::kSTOWNBYNAME_IMM8_ID16_V8), 0, 0, 0, kObject});
  }
  if (shape == Shape::kNestedShared) bytes.insert(bytes.end(), {op(abc::Opcode::kLDA_V8), 1,
      op(abc::Opcode::kSTOWNBYNAME_IMM8_ID16_V8), 0, 0, 0, kObject});
  if (bypass) {
    const auto displacement = bytes.size() - (*bypass - 1);
    test::Check(displacement <= INT8_MAX);
    bytes[*bypass] = static_cast<std::uint8_t>(displacement);
  }
  bytes.insert(bytes.end(), {op(abc::Opcode::kLDA_V8), kObject, op(abc::Opcode::kRETURN)});
  const auto catch_pc = static_cast<std::uint32_t>(bytes.size());
  if (shape == Shape::kAllPathCatchObserve || shape == Shape::kStagedCatchObserve || shape == Shape::kCatchObserve || shape == Shape::kNestedCatchObserve) bytes.insert(bytes.end(), {op(abc::Opcode::kLDA_V8), kObject});
  if (shape == Shape::kNestedSnapshot) bytes.insert(bytes.end(), {op(abc::Opcode::kLDA_V8), kSaved});
  if (caught) bytes.push_back(op(abc::Opcode::kRETURN));
  auto body = test::Body(bytes);
  if (caught) {
    abc::TryRegion region; region.offset = {shape == Shape::kAllPathCatchObserve || shape == Shape::kStagedCatchObserve || shape == Shape::kCatchObserve || shape == Shape::kNestedCatchObserve || shape == Shape::kNestedDispatch || shape == Shape::kGlobalDispatch ? initialized_pc : 0};
    if (shape == Shape::kNestedSnapshot) region.offset = {kSnapshotPrelude};
    if (lexical) region.offset = {lexical_try_begin};
    if (selection_try_begin) region.offset = {*selection_try_begin};
    region.length = catch_pc - region.offset.value; region.handler_count = 1;
    abc::CatchHandler handler; handler.offset = {catch_pc}; handler.length = bytes.size() - catch_pc;
    body.tries.push_back(region); body.handlers.push_back(handler);
  }
  const auto frame = std::get<abc::FrameLayout>(abc::BuildFrameLayout(
      shape == Shape::kNestedSnapshot || dead_copy ? 3 : nested || phi || selection_barrier || selected_alias || late_snapshot || predicate_register || selection_predicate || arm_alias ? 2 : 1, 4, {true, true, false, true}));
  const auto flow = std::get<ExceptionFlow>(BuildExceptionFlow(body, profile, frame.registers));
  IdfQuota ssa_work{kDefaultSemanticWork};
  const auto ssa = std::get<ExceptionSsa>(ExceptionSsa::Build(flow, frame.registers, ssa_work));
  std::vector<EntryValueFact> entries;
  for (std::size_t i = 0; i < ssa.Values().size(); ++i) {
    const auto& value = ssa.Values()[i];
    const auto* reg = std::get_if<abc::RegisterId>(&value.slot);
    if (reg && reg->value >= frame.locals && std::holds_alternative<MethodEntry>(value.origin))
      entries.push_back({{static_cast<std::uint32_t>(i)}, ValueKinds::Language()});
  }
  const auto env = std::get<EnvironmentFlow>(AnalyzeExceptionEnvironments(flow));
  std::vector<ExceptionLoadFact> loads;
  if (lexical) loads = std::get<ExceptionLocals>(ProveExceptionLocals(flow, env)).loads;
  IdfQuota value_work{kDefaultSemanticWork};
  const auto values = std::get<ExceptionValueFlow>(ExceptionValueFlow::Build(flow, ssa, entries,
      value_work, {}, PendingExceptionValues::kLanguage, loads));
  const auto inputs = VerifyExceptionInputs(flow, ssa, env, values);
  if (shape == Shape::kUnaryUnknown && unary != abc::Opcode::kTONUMERIC_IMM8 && unary != abc::Opcode::kTONUMBER_IMM8) {
    // NEG over arbitrary language values can produce an uncertified VM NaN.
    // Preserve the input proof refusal rather than admitting native syntax.
    test::Check(std::holds_alternative<ExceptionInputFailure>(inputs));
    return 0;
  }
  const auto proof = std::get<ExceptionInputProof>(inputs);
  const PhasedValueInput input{flow, ssa, env, values, proof, frame};
  auto graph = std::get<ValueProgram>(ImportPhasedValues(input));
  if (phi) {
    // Exercise the same input after legal SSA copy forwarding as well as
    // physical block splitting. This is a test-only candidate, not a change
    // to production aliasing of mutable phi/catch storage.
    const auto availability = std::get<PhasedValueFlowProof>(VerifyPhasedValueFlow(graph));
    for (const auto& operation : graph.ops) {
      if (!std::holds_alternative<ValueOwnWrite>(operation.operation)) continue;
      const UseId stored{operation.first_use.value + 1};
      auto value = std::get<ValueId>(graph.uses[stored.value].operand);
      while (const auto* result = std::get_if<OpResult>(&graph.values[value.value].origin)) {
        const auto& copy = graph.ops[result->op.value];
        test::Check(std::holds_alternative<ValueCopy>(copy.operation) && CanErasePureOp(copy));
        value = std::get<ValueId>(graph.uses[copy.first_use.value].operand);
      }
      const auto* joined = std::get_if<PhiDefinition>(&graph.values[value.value].origin);
      const auto allocation = std::find_if(graph.ops.begin(), graph.ops.end(), [](const auto& op) {
        return std::holds_alternative<ValueObjectAllocation>(op.operation);
      });
      test::Check(joined && allocation != graph.ops.end() && joined->block != allocation->block);
      test::Check(ValueAvailableAt(graph, availability.dominators, value,
          allocation->block, allocation->order) == early_phi);
      if (early_phi && forward_phi) test::Check(!ReplaceUse(graph, stored, value));
    }
  }
  if (all_path) {
    const auto effects = std::get<EffectOrder>(BuildEffectOrder(graph));
    const auto allocation = std::find_if(graph.ops.begin(), graph.ops.end(), [](const auto& op) {
      return std::holds_alternative<ValueObjectAllocation>(op.operation) ||
          std::holds_alternative<ValueArrayAllocation>(op.operation);
    });
    test::Check(allocation != graph.ops.end());
    const OpId allocated{static_cast<std::uint32_t>(allocation - graph.ops.begin())};
    auto previous = allocated;
    for (std::size_t i = 0; i < graph.ops.size(); ++i) {
      if (!std::holds_alternative<ValueOwnWrite>(graph.ops[i].operation) &&
          !std::holds_alternative<ValueIndexedWrite>(graph.ops[i].operation)) continue;
      const OpId id{static_cast<std::uint32_t>(i)};
      if (shape == Shape::kAllPathBypass || shape == Shape::kAllPathBarrier || shape == Shape::kAllPathLoop)
        test::Check(effects.after[previous.value] != id);
      else {
        test::Check(effects.after[previous.value] == id);
        test::Check(effects.before[i] == EffectPredecessor{previous});
      }
      test::Check(effects.completions[i] && graph.phases[effects.completions[i]->value].phase == ExceptionPhase::kCommit);
      for (std::size_t b = 0; b < graph.phases.size(); ++b)
        if (graph.phases[b].phase == ExceptionPhase::kCatch)
          test::Check(!effects.Before(graph, id, {static_cast<std::uint32_t>(b)}, 0));
      previous = id;
    }
    test::Check(effects.normal_return[previous.value]);
  }
  const auto result = BuildPhasedEvaluationPlan(graph);
  if (predicate_loop) {
    // The existing literal path refuses this looping body. Independently check
    // the decoded normal graph: successful getter completion does not guarantee
    // reaching the later decision. Anchoring a pure decision must not hide it.
    const auto identities = std::get<ValueIdentities>(AnalyzeValueIdentities(graph));
    std::optional<OpId> read, decision;
    for (std::size_t i = 0; i < graph.ops.size(); ++i) {
      const auto& op = graph.ops[i];
      if (!read && std::holds_alternative<ValueNamedRead>(op.operation)) read = OpId{static_cast<std::uint32_t>(i)};
      if (!read || !std::holds_alternative<ValueBranch>(op.operation)) continue;
      const auto* value = std::get_if<ValueId>(&graph.uses[op.first_use.value].operand);
      if (value && identities.canonical[value->value] == identities.canonical[graph.ops[read->value].result->value]) {
        decision = OpId{static_cast<std::uint32_t>(i)}; break;
      }
    }
    test::Check(read && decision);
    std::vector<ExecutionAnchor> anchors(graph.ops.size());
    anchors[read->value] = anchors[decision->value] = ExecutionAnchor::kRequired;
    const auto order = std::get<EffectOrder>(BuildEffectOrder(graph, {}, {}, anchors));
    test::Check(order.after[read->value] != decision);
    test::Check(std::get<EvaluationError>(result) == EvaluationError::kUnsupportedOperation);
    return 0;
  }
  if (all_path_negative || late_phi || shape == Shape::kStagedObserve || shape == Shape::kStagedCatchObserve || shape == Shape::kObserve || shape == Shape::kBranch || shape == Shape::kCatchObserve ||
      shape == Shape::kNestedCatchObserve || shape == Shape::kNestedDispatch || shape == Shape::kNestedShared || shape == Shape::kNestedSnapshot ||
      shape == Shape::kUnaryShared || shape == Shape::kGlobalDispatch) {
    test::Check(std::get<EvaluationError>(result) == EvaluationError::kUnsupportedOperation);
    return 0;
  }
  test::Check(std::holds_alternative<EvaluationPlan>(result));
  const auto& evaluation = std::get<EvaluationPlan>(result);
  if (shape == Shape::kArmSelection || arm_alias || shape == Shape::kSelectionPredicate || shape == Shape::kSelectionPredicateUnary || shape == Shape::kSelectionPredicateChain || shape == Shape::kStagedSelection || selected_alias || shape == Shape::kPredicateRead || predicate_alias) {
    test::Check(evaluation.logical_objects.size() == graph.ops.size());
    test::Check(evaluation.logical_objects[evaluation.literals->objects.front().allocation.value].has_value());
  }
  if (shape == Shape::kArmSelectionBarrier || shape == Shape::kArmSelectionDispatch || shape == Shape::kSelectionPredicateDispatch || shape == Shape::kSelectionPredicateBarrier || late_snapshot || predicate_bypass || predicate_loop || shape == Shape::kPredicateReadBarrier || selection_barrier || (shape == Shape::kStaged && fields > 1) || shape == Shape::kStagedReadBarrier)
    test::Check(evaluation.logical_objects.empty());
  if (selected_unary || shape == Shape::kStagedSelection || shape == Shape::kStagedComputed || shape == Shape::kStagedCall) {
    test::Check(std::count_if(evaluation.operations.begin(), evaluation.operations.end(),
        [](const OpEvaluation& op) { return op.kind == EvaluationKind::kLogicalValue; }) == fields);
  }
  if (selection_barrier || (shape == Shape::kStaged && fields > 1))
    test::Check(evaluation.phase_selections.empty());
  test::Check(std::all_of(evaluation.phase_selections.begin(), evaluation.phase_selections.end(),
      [](const auto& selected) { return !std::holds_alternative<PhaseCallReference>(selected.owner); }));
  if (!evaluation.phase_selections.empty()) {
    auto forged = evaluation;
    forged.phase_selections.front().input = UseId{0};
    test::Check(std::holds_alternative<EvaluationError>(VerifyPhasedEvaluationPlan(graph, forged)));
    forged = evaluation; forged.phase_selections.front().execution = OpId{0};
    test::Check(std::holds_alternative<EvaluationError>(VerifyPhasedEvaluationPlan(graph, forged)));
    forged = evaluation; forged.phase_selections.front().owner = OpId{0};
    test::Check(std::holds_alternative<EvaluationError>(VerifyPhasedEvaluationPlan(graph, forged)));
    if (!evaluation.phase_children.empty()) {
      for (std::size_t i = 0; i < evaluation.phase_children.size(); ++i) {
        if (!evaluation.phase_children[i]) continue;
        forged = evaluation; forged.phase_children[i].reset();
        test::Check(std::holds_alternative<EvaluationError>(VerifyPhasedEvaluationPlan(graph, forged)));
        break;
      }
    }
    if (!evaluation.phase_selections.front().prefix.empty()) {
      forged = evaluation; forged.phase_selections.front().prefix.clear();
      test::Check(std::holds_alternative<EvaluationError>(VerifyPhasedEvaluationPlan(graph, forged)));
      forged = evaluation;
      forged.phase_prefixes[evaluation.phase_selections.front().prefix.front().value].reset();
      test::Check(std::holds_alternative<EvaluationError>(VerifyPhasedEvaluationPlan(graph, forged)));
    }
  }
  if (read) for (std::size_t i = 0; i < graph.ops.size(); ++i) {
    if (!std::holds_alternative<ValueNamedRead>(graph.ops[i].operation)) continue;
    const bool fused = fields == 1 && shape != Shape::kStagedReadBarrier;
    test::Check((evaluation.operations[i].kind == EvaluationKind::kInline) == fused);
    if (fused) {
      const auto use = *evaluation.operations[i].consumer;
      test::Check(graph.uses[use.value].role == OperandRole::kStoredValue);
      auto forged = evaluation; forged.operations[i].consumer = UseId{0};
      test::Check(std::holds_alternative<EvaluationError>(VerifyPhasedEvaluationPlan(graph, forged)));
    }
  }
  if (selection_predicate && shape != Shape::kSelectionPredicateDispatch) {
    test::Check(evaluation.phase_selections.size() == fields *
        (shape == Shape::kSelectionPredicateBarrier ? 1U : shape == Shape::kSelectionPredicateChain ? selections : 2U));
    if (shape != Shape::kSelectionPredicateBarrier) for (const auto& selected : evaluation.phase_selections) {
      test::Check(std::holds_alternative<ValueOwnWrite>(graph.ops[selected.execution.value].operation));
    }
  }
  if (shape == Shape::kStaged) {
    test::Check(std::any_of(evaluation.operations.begin(), evaluation.operations.end(),
        [](const OpEvaluation& op) { return op.kind == EvaluationKind::kPhiInput; }));
    for (std::size_t i = 0; i < evaluation.operations.size(); ++i) {
      if (evaluation.operations[i].kind != EvaluationKind::kPhiInput) continue;
      // Phi expression ownership preserves original effects and exception
      // sites. The complete tree verifier and staged Node oracle below check
      // those observations; the producer need not be pure.
      auto forged = evaluation;
      forged.operations[i].consumer = UseId{0};
      test::Check(std::holds_alternative<EvaluationError>(VerifyPhasedEvaluationPlan(graph, forged)));
    }
  }
  test::Check(evaluation.literals && evaluation.literals->objects.size() == (nested ? fields + 1 : 1));
  test::Check(std::holds_alternative<std::uint64_t>(VerifyPhasedEvaluationPlan(graph, evaluation)));
  const auto& literal = *evaluation.literals;
  if (!evaluation.logical_objects.empty()) {
    auto forged = evaluation;
    const auto allocation = literal.objects.front().allocation;
    forged.logical_objects[allocation.value] = allocation;
    test::Check(std::holds_alternative<EvaluationError>(VerifyPhasedEvaluationPlan(graph, forged)));
    forged = evaluation; forged.logical_objects.clear();
    test::Check(std::holds_alternative<EvaluationError>(VerifyPhasedEvaluationPlan(graph, forged)));
  }
  if (all_path) test::Check(literal.objects.front().form == LiteralForm::kExpression);
  if (selected_unary || shape == Shape::kStagedSelection || shape == Shape::kStagedComputed || shape == Shape::kStagedCall) {
    TreeCheckBudget candidate_budget({});
    const auto candidates = FindPhasedPhiSelections(graph, evaluation, candidate_budget);
    test::Check(candidates.size() == fields);
    for (const auto& candidate : candidates) {
      test::Check(!candidate.keep_path && !candidate.evaluate_path);
      test::Check(candidate.keep_input && candidate.evaluate_input &&
          graph.edges[candidate.keep_input->value].target == candidate.join &&
          graph.edges[candidate.evaluate_input->value].target == candidate.join);
    }
  }
  if (lexical) {
    test::Check(evaluation.cells.has_value());
    bool checked = false;
    for (std::size_t i = 0; i < graph.ops.size(); ++i) {
      if (!std::holds_alternative<ValueLexicalLoad>(graph.ops[i].operation)) continue;
      checked = true;
      auto invalid = evaluation;
      invalid.cells->initialization.reads[i] = NotCellRead{};
      test::Check(std::holds_alternative<LiteralError>(VerifyPhasedLiteralPlan(graph, literal, invalid)));
      test::Check(std::holds_alternative<EvaluationError>(VerifyPhasedEvaluationPlan(graph, invalid)));
      break;
    }
    test::Check(checked);
    auto invalid = evaluation;
    ++invalid.cells->initialization.cfg_revision.value;
    test::Check(std::holds_alternative<LiteralError>(VerifyPhasedLiteralPlan(graph, literal, invalid)));
    invalid = evaluation; invalid.cells.reset();
    test::Check(std::holds_alternative<LiteralError>(VerifyPhasedLiteralPlan(graph, literal, invalid)));
  }
  test::Check(literal.definitions.size() == (nested ? fields * 2 : fields + (read ? 1 : 0)));
  test::Check(std::holds_alternative<std::uint64_t>(VerifyPhasedLiteralPlan(graph, literal, evaluation)));
  test::Check(std::holds_alternative<LiteralError>(VerifyLiteralPlan(graph, literal)));
  auto changed = literal; ++changed.revision.value;
  test::Check(std::get<LiteralError>(VerifyPhasedLiteralPlan(graph, changed, evaluation)) == LiteralError::kRevision);
  changed = literal; --changed.objects[0].count;
  test::Check(std::get<LiteralError>(VerifyPhasedLiteralPlan(graph, changed, evaluation)) == LiteralError::kCoverage);
  if (nested) {
    changed = literal;
    const auto at = std::find_if(changed.nested.begin(), changed.nested.end(), [](const auto& use) { return use.has_value(); });
    test::Check(at != changed.nested.end());
    --(*at)->value;
    test::Check(std::holds_alternative<LiteralError>(VerifyPhasedLiteralPlan(graph, changed, evaluation)));
    changed = literal; std::swap(changed.definitions.front(), changed.definitions.back());
    test::Check(std::holds_alternative<LiteralError>(VerifyPhasedLiteralPlan(graph, changed, evaluation)));
  }
  auto aliases = evaluation; aliases.aliases.back().value = graph.values.size();
  test::Check(std::get<LiteralError>(VerifyPhasedLiteralPlan(graph, literal, aliases)) == LiteralError::kFlow);
  test::Check(std::holds_alternative<LiteralPlan>(BuildPhasedLiteralPlan(graph, evaluation,
      {SemanticBudget{}.bytes, literal.work})));
  test::Check(std::get<LiteralError>(BuildPhasedLiteralPlan(graph, evaluation,
      {SemanticBudget{}.bytes, literal.work - 1})) == LiteralError::kWorkLimit);
  test::Check(std::get<LiteralError>(BuildPhasedLiteralPlan(graph, evaluation, {0})) == LiteralError::kResourceLimit);
  if (dead_copy) {
    // A forged dead label cannot hide an own-store effect or a copy whose
    // result still reaches the outer property, even with an identity alias map.
    auto invalid = evaluation;
    invalid.operations[literal.definitions.front().value] = {EvaluationKind::kElidedPure};
    test::Check(std::get<LiteralError>(VerifyPhasedLiteralPlan(graph, literal, invalid)) == LiteralError::kFlow);
    const auto definition = literal.definitions.front();
    const auto rhs = std::get<ValueId>(graph.uses[graph.ops[definition.value].first_use.value + 1].operand);
    const auto producer = std::get<OpResult>(graph.values[rhs.value].origin).op;
    test::Check(std::holds_alternative<ValueCopy>(graph.ops[producer.value].operation));
    invalid = evaluation; invalid.operations[producer.value] = {EvaluationKind::kElidedPure};
    invalid.aliases[rhs.value] = rhs;
    test::Check(std::get<LiteralError>(VerifyPhasedLiteralPlan(graph, literal, invalid)) == LiteralError::kFlow);
  }
  // The forwarded candidate tests literal/evaluation proof independence. The
  // encoded-source route retains the original phi materialization: raw import
  // correspondence is still a separate obligation, not granted by ReplaceUse.
  if (all_path || (early_phi && !forward_phi) || staged || shape == Shape::kCatch || shape == Shape::kNestedCatch || shape == Shape::kUnaryCatch || dead_copy || shape == Shape::kGlobalCatch || lexical || binary ||
      (shape == Shape::kUnaryUnknown && (unary == abc::Opcode::kTONUMERIC_IMM8 || unary == abc::Opcode::kTONUMBER_IMM8))) {
    const auto regions = std::get<ExceptionPlan>(BuildExceptionPlan(body, graph, input));
    const auto control = std::get<ExceptionControl>(BuildExceptionControl(body, graph, input, regions));
    if (fields == 1 && (shape == Shape::kStagedSelection || shape == Shape::kStagedComputed || shape == Shape::kStagedCall)) {
      TreeCheckBudget region_budget({});
      const auto selected = FindPhasedSelections(graph, control.skeleton, evaluation, region_budget);
      test::Check(selected.size() == 1 && selected.front().keep_path && selected.front().evaluate_path);
      auto wrong = control.skeleton;
      bool changed = false;
      for (auto& node : wrong.nodes) if (node.block == selected.front().header && node.arms) {
        node.arms->taken = node.arms->fallthrough; changed = true; break;
      }
      test::Check(changed);
      TreeCheckBudget wrong_budget({});
      test::Check(FindPhasedSelections(graph, wrong, evaluation, wrong_budget).empty());
    }
    FunctionSyntax syntax{}; syntax.callable = CallableKind::kFunction; syntax.strictness = Strictness::kStrict;
    syntax.parameters.resize(1); syntax.names = {u"__proto__", u"Lookup", u"value", u"after"};
    syntax.texts = {u"suffix"};
    std::vector<ExpressionConstant> constants(graph.ops.size());
    for (const auto id : literal.definitions) {
      if (!array) { constants[id.value] = NameId{0}; continue; }
      const auto index = std::get<ValueIndexedWrite>(graph.ops[id.value].operation).index;
      const auto key = std::to_string(index);
      constants[id.value] = NameId{static_cast<std::uint32_t>(syntax.names.size())};
      syntax.names.emplace_back(key.begin(), key.end());
    }
    for (std::size_t i = 0; i < graph.ops.size(); ++i) {
      if (std::holds_alternative<ValueString>(graph.ops[i].operation)) constants[i] = TextId{0};
      if (std::holds_alternative<ValueGlobalRead>(graph.ops[i].operation)) constants[i] = NameId{1};
      if ((global || staged) && std::holds_alternative<ValueNamedRead>(graph.ops[i].operation)) constants[i] = NameId{selection_barrier ? 3U : 2U};
    }
    std::vector<ConstantProperty> properties;
    if (staged) {
      test::Check(literal.objects.front().form == LiteralForm::kStatements);
      constants[literal.objects.front().allocation.value] = AggregateConstant{0, 1};
      properties.push_back({NameId{0}, NullLiteral{}});
    }
    const ExpressionConstants resolved{constants, syntax.names.size(), syntax.texts.size(), {}, properties};
    const ExceptionTreeInput context{body, input, regions, control};
    // This exercises repeated independent tree/phase checks as well as the
    // initializer proof. Keep its explicit allowance separate from the exact
    // initializer-work tests above; production defaults are unchanged.
    constexpr SemanticBudget kTreeBudget{};
    const auto built = BuildExceptionFunctionDraft(graph, context, evaluation, syntax, resolved, kTreeBudget);
    test::Check(std::holds_alternative<ExceptionFunctionDraft>(built));
    const auto& tree = std::get<ExceptionFunctionDraft>(built).tree;
    if (shape == Shape::kArmSelection || arm_alias || selected_unary || shape == Shape::kStagedSelection || shape == Shape::kStagedComputed || shape == Shape::kStagedCall) {
      test::Check(std::count_if(tree.expressions.begin(), tree.expressions.end(),
          [](const auto& node) { return std::holds_alternative<ConditionalExpr>(node.expression); }) == fields * (arm_selection ? selections : 1U));
      auto wrong = tree;
      for (auto& node : wrong.expressions)
        if (auto* selection = std::get_if<ConditionalExpr>(&node.expression))
          std::swap(selection->consequent, selection->alternate);
      test::Check(std::holds_alternative<TreeSemanticError>(VerifyExceptionTreeSemantics(
          graph, context, evaluation, wrong, resolved, kTreeBudget)));
      if (shape == Shape::kArmSelection) {
        wrong = tree; bool changed = false;
        for (auto& expression : wrong.expressions) {
          if (!std::holds_alternative<SymbolExpr>(expression.expression) ||
              !std::holds_alternative<CombinedOrigin>(wrong.origins[expression.origin.value])) continue;
          expression.origin = TreeOriginId{0}; changed = true; break;
        }
        test::Check(changed);
        test::Check(std::holds_alternative<TreeSemanticError>(VerifyExceptionTreeSemantics(
            graph, context, evaluation, wrong, resolved, kTreeBudget)));
      }
    }
    const bool composed = !evaluation.logical_objects.empty() &&
        evaluation.logical_objects[literal.objects.front().allocation.value].has_value();
    test::Check(tree.properties.size() == (staged && !composed ? 1 : nested ? fields * 2 : fields + (read ? 1 : 0)));
    test::Check(std::holds_alternative<TreeSemantics>(VerifyExceptionTreeSemantics(
        graph, context, evaluation, tree, resolved, kTreeBudget)));
    if (print) {
      const GlobalScope globals{};
      std::ostringstream output;
      test::Check(std::holds_alternative<PrintedFunction>(PrintFunctionTree(tree, output, {}, {}, &globals)));
      std::cout << output.str();
    }
  }
  return arm_selection || selection_predicate || all_path || selected_unary || shape == Shape::kStagedSelection || shape == Shape::kStagedComputed || shape == Shape::kStagedCall ? evaluation.work : literal.work;
}
}  // namespace
int main(int argc, char** argv) {
  if (argc == 2 && std::string_view(argv[1]) == "--early-phi-source") {
    Run(abc::ProfileId::kDynamic1206, Shape::kEarlyPhi, 2, 2,
        abc::Opcode::kSTOWNBYNAME_IMM8_ID16_V8, abc::Opcode::kNEG_IMM8, true, false);
    return 0;
  }
  if (argc == 2) {
    const auto option = std::string_view(argv[1]);
    const bool update = option == "--increment-source" || option == "--decrement-source" || option == "--numeric-source";
    if (option == "--all-path-source" || option == "--all-path-array-source") {
      Run(abc::ProfileId::kDynamic1206, option == "--all-path-array-source" ? Shape::kAllPathArray : Shape::kAllPath,
          201, 2, abc::Opcode::kSTOWNBYNAME_IMM8_ID16_V8, abc::Opcode::kNEG_IMM8, true);
      return 0;
    }
    if (option == "--object-pair-call-source") {
      Run(abc::ProfileId::kDynamic1206, Shape::kStagedCall, 201, 2,
          abc::Opcode::kDEFINEFIELDBYNAME_IMM8_ID16_V8, abc::Opcode::kNEG_IMM8, true);
      return 0;
    }
    if (option == "--selection-predicate-source" || option == "--selection-predicate-unary-source" ||
        option == "--selection-predicate-dispatch-source" || option == "--selection-predicate-chain-source") {
      Run(abc::ProfileId::kDynamic1206, option == "--selection-predicate-unary-source" ? Shape::kSelectionPredicateUnary :
          option == "--selection-predicate-dispatch-source" ? Shape::kSelectionPredicateDispatch :
          option == "--selection-predicate-chain-source" ? Shape::kSelectionPredicateChain : Shape::kSelectionPredicate, 201, 2,
          abc::Opcode::kDEFINEFIELDBYNAME_IMM8_ID16_V8, abc::Opcode::kNEG_IMM8, true, true, 4);
      return 0;
    }
    if (option == "--arm-selection-source" || option == "--arm-selection-chain-source" || option == "--arm-selection-alias-source" ||
        option == "--arm-selection-barrier-source" || option == "--arm-selection-dispatch-source") {
      Run(abc::ProfileId::kDynamic1206, option == "--arm-selection-alias-source" ? Shape::kArmSelectionAlias :
          option == "--arm-selection-barrier-source" ? Shape::kArmSelectionBarrier :
          option == "--arm-selection-dispatch-source" ? Shape::kArmSelectionDispatch : Shape::kArmSelection, 201, 2,
          abc::Opcode::kDEFINEFIELDBYNAME_IMM8_ID16_V8, abc::Opcode::kNEG_IMM8, true, true,
          option == "--arm-selection-chain-source" ? 4U : 2U);
      return 0;
    }
    if (option == "--predicate-read-source") {
      Run(abc::ProfileId::kDynamic1206, Shape::kPredicateRead, 201, 2,
          abc::Opcode::kDEFINEFIELDBYNAME_IMM8_ID16_V8, abc::Opcode::kNEG_IMM8, true);
      return 0;
    }
    if (option == "--read-source") {
      Run(abc::ProfileId::kDynamic1206, Shape::kStagedRead, 201, 1,
          abc::Opcode::kDEFINEFIELDBYNAME_IMM8_ID16_V8, abc::Opcode::kNEG_IMM8, true);
      return 0;
    }
    if (option == "--selection-unary-source") {
      Run(abc::ProfileId::kDynamic1206, Shape::kStagedSelectionUnary, 201, 1,
          abc::Opcode::kDEFINEFIELDBYNAME_IMM8_ID16_V8, abc::Opcode::kNEG_IMM8, true);
      return 0;
    }
    if (option == "--selection-barrier-source") {
      Run(abc::ProfileId::kDynamic1206, Shape::kStagedSelectionBarrier, 201, 1,
          abc::Opcode::kDEFINEFIELDBYNAME_IMM8_ID16_V8, abc::Opcode::kNEG_IMM8, true);
      return 0;
    }
    if (option == "--computed-source" || option == "--computed-call-source") {
      Run(abc::ProfileId::kDynamic1206, option == "--computed-call-source" ? Shape::kStagedCall : Shape::kStagedComputed, 201, 1,
          abc::Opcode::kDEFINEFIELDBYNAME_IMM8_ID16_V8, abc::Opcode::kNEG_IMM8, true);
      return 0;
    }
    if (option == "--staged-source" || option == "--conditional-source") {
      Run(abc::ProfileId::kDynamic1206, option == "--conditional-source" ? Shape::kStagedSelection : Shape::kStaged, option == "--conditional-source" ? 201 : 0, 2,
          abc::Opcode::kDEFINEFIELDBYNAME_IMM8_ID16_V8, abc::Opcode::kNEG_IMM8, true);
      return 0;
    }
    test::Check(option == "--global-source" || option == "--lexical-source" || update);
    const auto unary = option == "--increment-source" ? abc::Opcode::kINC_IMM8 :
        option == "--decrement-source" ? abc::Opcode::kDEC_IMM8 :
        option == "--numeric-source" ? abc::Opcode::kTONUMERIC_IMM8 : abc::Opcode::kNEG_IMM8;
    Run(abc::ProfileId::kDynamic1206, update ? Shape::kUnaryCatch :
        option == "--lexical-source" ? Shape::kLexicalCatch : Shape::kGlobalCatch, 0, 2,
        abc::Opcode::kSTOWNBYNAME_IMM8_ID16_V8, unary, true);
    return 0;
  }
  for (const auto profile : {abcd::abc::ProfileId::kDynamic1202, abcd::abc::ProfileId::kDynamic1206,
                            abcd::abc::ProfileId::kDynamic1301}) {
    for (const auto copies : {1U, 201U}) {
      Run(profile, Shape::kArmSelection, copies, 2);
      Run(profile, Shape::kArmSelectionAlias, copies, 2);
      Run(profile, Shape::kArmSelectionBarrier, copies, 2);
      Run(profile, Shape::kArmSelectionDispatch, copies, 2);
      Run(profile, Shape::kSelectionPredicate, copies, 2);
      Run(profile, Shape::kSelectionPredicateUnary, copies, 2);
      Run(profile, Shape::kSelectionPredicateBarrier, copies, 2);
      Run(profile, Shape::kSelectionPredicateDispatch, copies, 2);
      Run(profile, Shape::kPredicateRead, copies, 2);
      Run(profile, Shape::kPredicateReadBarrier, copies, 2);
      Run(profile, Shape::kPredicateReadBypass, copies, 2);
      Run(profile, Shape::kPredicateReadAlias, copies, 2);
      Run(profile, Shape::kPredicateReadLoop, copies, 2);
      Run(profile, Shape::kStagedSelectionAlias, copies, 2);
      Run(profile, Shape::kStagedLateSnapshot, copies, 2);
    }
    std::uint64_t previous_arm_depth = 0;
    for (const auto depth : {2U, 3U, 200U, 201U, 402U}) {
      const auto work = Run(profile, Shape::kArmSelection, 1, 1,
          abc::Opcode::kSTOWNBYNAME_IMM8_ID16_V8, abc::Opcode::kNEG_IMM8, false, true, depth);
      if (depth == 402) test::Check(work < previous_arm_depth * 3);
      previous_arm_depth = work;
    }
    std::uint64_t previous_depth = 0;
    for (const auto depth : {2U, 3U, 200U, 201U, 402U}) {
      const auto work = Run(profile, Shape::kSelectionPredicateChain, 1, 1,
          abc::Opcode::kSTOWNBYNAME_IMM8_ID16_V8, abc::Opcode::kNEG_IMM8, false, true, depth);
      if (depth == 402) test::Check(work < previous_depth * 3);
      previous_depth = work;
    }
    std::uint64_t previous_selection_predicate = 0;
    for (const auto copies : {200U, 201U, 402U}) {
      const auto work = Run(profile, Shape::kSelectionPredicate, copies, 2);
      if (copies == 402) test::Check(work < previous_selection_predicate * 3);
      previous_selection_predicate = work;
    }
    std::uint64_t previous_predicate = 0;
    for (const auto copies : {200U, 201U, 402U}) {
      const auto work = Run(profile, Shape::kPredicateRead, copies, 2);
      if (copies == 402) test::Check(work < previous_predicate * 3);
      previous_predicate = work;
    }
    for (const auto all : {Shape::kAllPath, Shape::kAllPathArray}) {
      std::uint64_t previous = 0;
      for (const auto count : {1U, 2U, 200U, 201U, 402U}) {
        const auto work = Run(profile, all, count, 2);
        if (count == 402) test::Check(work < 3 * previous);
        previous = work;
      }
      previous = 0;
      for (const auto count : {1U, 2U, 200U, 201U, 402U}) {
        const auto work = Run(profile, all, 1, count);
        if (count == 402) test::Check(work < 3 * previous);
        previous = work;
      }
    }
    for (const auto copies : {1U, 201U}) {
      Run(profile, Shape::kAllPathBypass, copies);
      Run(profile, Shape::kAllPathBarrier, copies);
      Run(profile, Shape::kAllPathLoop, copies);
      Run(profile, Shape::kAllPathCatchObserve, copies);
    }
    for (const auto count : {0U, 1U, 200U, 201U, 402U}) {
      Run(profile, Shape::kEarlyPhi, count, 2);
      Run(profile, Shape::kEarlyPhi, count, 2, abc::Opcode::kSTOWNBYNAME_IMM8_ID16_V8,
          abc::Opcode::kNEG_IMM8, false, false);
      Run(profile, Shape::kLatePhi, count, 2);
    }
    for (const auto shape : {Shape::kStaged, Shape::kStagedObserve, Shape::kStagedCatchObserve, Shape::kReturn, Shape::kCatch, Shape::kObserve, Shape::kLate, Shape::kBranch,
                             Shape::kCatchObserve, Shape::kNested, Shape::kNestedCatch, Shape::kNestedCatchObserve,
                             Shape::kNestedSnapshot, Shape::kNestedDispatch, Shape::kNestedShared,
                             Shape::kUnary, Shape::kUnaryCatch, Shape::kUnaryShared, Shape::kUnaryUnknown,
                             Shape::kNestedDeadCopy, Shape::kNestedDeadArray, Shape::kGlobalCatch, Shape::kGlobalDispatch, Shape::kLexicalCatch, Shape::kBinaryCatch}) Run(profile, shape);
    for (const auto unary : {abcd::abc::Opcode::kNOT_IMM8, abcd::abc::Opcode::kTYPEOF_IMM8,
        abcd::abc::Opcode::kTONUMBER_IMM8, abcd::abc::Opcode::kISTRUE, abcd::abc::Opcode::kISFALSE,
        abcd::abc::Opcode::kTONUMERIC_IMM8, abcd::abc::Opcode::kINC_IMM8, abcd::abc::Opcode::kDEC_IMM8})
      Run(profile, Shape::kUnaryCatch, 0, 1, abcd::abc::Opcode::kSTOWNBYNAME_IMM8_ID16_V8, unary);
    for (const auto unary : {abc::Opcode::kTONUMERIC_IMM8, abc::Opcode::kINC_IMM8, abc::Opcode::kDEC_IMM8}) {
      Run(profile, Shape::kUnaryUnknown, 0, 1, abc::Opcode::kSTOWNBYNAME_IMM8_ID16_V8, unary);
      Run(profile, Shape::kUnaryShared, 0, 1, abc::Opcode::kSTOWNBYNAME_IMM8_ID16_V8, unary);
      std::uint64_t previous = 0;
      for (const auto fields : {1U, 2U, 200U, 201U, 402U}) {
        const auto work = Run(profile, Shape::kUnaryCatch, 0, fields, abc::Opcode::kSTOWNBYNAME_IMM8_ID16_V8, unary);
        if (fields == 402) test::Check(work < 3 * previous);
        previous = work;
      }
    }
    for (const auto opcode : {abcd::abc::Opcode::kSTOWNBYNAME_IMM16_ID16_V8,
        abcd::abc::Opcode::kDEFINEFIELDBYNAME_IMM8_ID16_V8, abcd::abc::Opcode::kDEFINEPROPERTYBYNAME_IMM8_ID16_V8})
      Run(profile, Shape::kCatch, 0, 1, opcode);
    for (const auto copies : {1U, 200U, 201U, 402U, 804U}) Run(profile, Shape::kCatch, copies);
    for (const auto fields : {2U, 200U, 201U, 400U, 800U})
      Run(profile, Shape::kNestedCatch, 0, fields);
    for (const auto fields : {2U, 200U, 201U, 400U, 800U})
      Run(profile, Shape::kUnaryCatch, 0, fields);
    std::uint64_t previous_copies = 0, previous_arrays = 0;
    for (const auto count : {2U, 200U, 201U, 400U, 800U}) {
      const auto copy_work = Run(profile, Shape::kNestedDeadCopy, count);
      Run(profile, Shape::kNestedDeadArray, count);
      const auto array_work = Run(profile, Shape::kNestedDeadArray, 1, count);
      if (count > 201) {
        test::Check(copy_work < 3 * previous_copies);
        test::Check(array_work < 3 * previous_arrays);
      }
      previous_copies = copy_work; previous_arrays = array_work;
    }
    for (const auto count : {1U, 2U, 200U, 201U, 402U}) {
      Run(profile, Shape::kBinaryCatch, 0, count);
      Run(profile, Shape::kStaged, 0, count);
    }
    for (const auto selected : {Shape::kStagedSelection, Shape::kStagedComputed, Shape::kStagedCall}) {
      std::uint64_t previous = 0;
      for (const auto fields : {1U, 2U, 200U, 201U, 402U}) {
        const auto work = Run(profile, selected, 0, fields);
        if (fields == 402) test::Check(work < 3 * previous);
        previous = work;
      }
    }
    for (const auto selected : {Shape::kStagedSelection, Shape::kStagedComputed, Shape::kStagedCall, Shape::kStagedSelectionUnary}) {
      std::uint64_t previous = 0;
      for (const auto copies : {1U, 2U, 200U, 201U, 402U}) {
        const auto work = Run(profile, selected, copies);
        if (copies == 402) test::Check(work < 3 * previous);
        previous = work;
      }
    }
    std::uint64_t read_work = 0;
    for (const auto copies : {1U, 2U, 200U, 201U, 402U}) {
      const auto work = Run(profile, Shape::kStagedRead, copies);
      if (copies == 402) test::Check(work < 3 * read_work);
      read_work = work;
      Run(profile, Shape::kStagedReadBarrier, copies);
      Run(profile, Shape::kStagedSelectionBarrier, copies);
      Run(profile, Shape::kStagedRead, copies, 2);
    }
    std::uint64_t lexical_work = 0;
    for (const auto count : {1U, 2U, 200U, 201U, 402U}) {
      const auto work = Run(profile, Shape::kLexicalCatch, 0, count);
      if (count == 402) test::Check(work < 3 * lexical_work);
      lexical_work = work;
    }
    std::uint64_t previous = 0;
    for (const auto fields : {200U, 201U, 402U, 804U}) {
      const auto work = Run(profile, Shape::kCatch, 0, fields);
      if (fields > 201) abcd::test::Check(work < 3 * previous);
      previous = work;
    }
  }
}
