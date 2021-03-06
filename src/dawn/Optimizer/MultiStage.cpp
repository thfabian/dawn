//===--------------------------------------------------------------------------------*- C++ -*-===//
//                          _
//                         | |
//                       __| | __ ___      ___ ___
//                      / _` |/ _` \ \ /\ / / '_  |
//                     | (_| | (_| |\ V  V /| | | |
//                      \__,_|\__,_| \_/\_/ |_| |_| - Compiler Toolchain
//
//
//  This file is distributed under the MIT License (MIT).
//  See LICENSE.txt for details.
//
//===------------------------------------------------------------------------------------------===//

#include "dawn/Optimizer/MultiStage.h"
#include "dawn/Optimizer/Accesses.h"
#include "dawn/Optimizer/DependencyGraphAccesses.h"
#include "dawn/Optimizer/ReadBeforeWriteConflict.h"
#include "dawn/Optimizer/Renaming.h"
#include "dawn/Optimizer/Stage.h"
#include "dawn/Optimizer/StencilInstantiation.h"
#include "dawn/Support/STLExtras.h"

namespace dawn {

MultiStage::MultiStage(StencilInstantiation* stencilInstantiation, LoopOrderKind loopOrder)
    : stencilInstantiation_(stencilInstantiation), loopOrder_(loopOrder) {}

std::vector<std::shared_ptr<MultiStage>>
MultiStage::split(std::deque<MultiStage::SplitIndex>& splitterIndices,
                  LoopOrderKind lastLoopOrder) {

  std::vector<std::shared_ptr<MultiStage>> newMultiStages;

  int curStageIndex = 0;
  auto curStageIt = stages_.begin();
  std::deque<int> curStageSplitterIndices;

  newMultiStages.push_back(std::make_shared<MultiStage>(stencilInstantiation_, lastLoopOrder));

  for(std::size_t i = 0; i < splitterIndices.size(); ++i) {
    SplitIndex& splitIndex = splitterIndices[i];

    if(splitIndex.StageIndex == curStageIndex) {

      curStageSplitterIndices.push_back(splitIndex.StmtIndex);
      newMultiStages.push_back(
          std::make_shared<MultiStage>(stencilInstantiation_, splitIndex.LowerLoopOrder));
      lastLoopOrder = splitIndex.LowerLoopOrder;
    }

    if(i == (splitterIndices.size() - 1) || splitIndex.StageIndex != curStageIndex) {
      if(!curStageSplitterIndices.empty()) {

        // Split the current stage (we assume the graphs are assigned in the stage splitter pass)
        auto newStages = (**curStageIt).split(curStageSplitterIndices, nullptr);

        // Move the new stages to the new MultiStage
        auto newMultiStageRIt = newMultiStages.rbegin();
        for(auto newStagesRIt = newStages.rbegin(); newStagesRIt != newStages.rend();
            ++newStagesRIt, ++newMultiStageRIt)
          (*newMultiStageRIt)->getStages().push_back(std::move(*newStagesRIt));

        curStageSplitterIndices.clear();
      } else {
        // No split in this stage, just move it to the current multi-stage
        newMultiStages.back()->getStages().push_back(std::move(*curStageIt));
      }

      if(i != (splitterIndices.size() - 1))
        newMultiStages.push_back(
            std::make_shared<MultiStage>(stencilInstantiation_, lastLoopOrder));

      // Handle the next stage
      curStageIndex++;
      curStageIt++;
    }
  }

  return newMultiStages;
}

std::shared_ptr<DependencyGraphAccesses>
MultiStage::getDependencyGraphOfInterval(const Interval& interval) const {
  auto dependencyGraph = std::make_shared<DependencyGraphAccesses>(stencilInstantiation_);
  std::for_each(stages_.begin(), stages_.end(), [&](const std::shared_ptr<Stage>& stagePtr) {
    if(interval.overlaps(stagePtr->getEnclosingExtendedInterval()))
      std::for_each(stagePtr->getDoMethods().begin(), stagePtr->getDoMethods().end(),
                    [&](const std::unique_ptr<DoMethod>& DoMethodPtr) {
                      dependencyGraph->merge(DoMethodPtr->getDependencyGraph().get());
                    });
  });
  return dependencyGraph;
}

std::shared_ptr<DependencyGraphAccesses> MultiStage::getDependencyGraphOfAxis() const {
  auto dependencyGraph = std::make_shared<DependencyGraphAccesses>(stencilInstantiation_);
  std::for_each(stages_.begin(), stages_.end(), [&](const std::shared_ptr<Stage>& stagePtr) {
    std::for_each(stagePtr->getDoMethods().begin(), stagePtr->getDoMethods().end(),
                  [&](const std::unique_ptr<DoMethod>& DoMethodPtr) {
                    dependencyGraph->merge(DoMethodPtr->getDependencyGraph().get());
                  });
  });
  return dependencyGraph;
}

Cache& MultiStage::setCache(Cache::CacheTypeKind type, Cache::CacheIOPolicy policy, int AccessID) {
  return caches_.emplace(AccessID, Cache(type, policy, AccessID)).first->second;
}

std::unordered_set<Interval> MultiStage::getIntervals() const {
  std::unordered_set<Interval> intervals;
  for(const auto& stagePtr : stages_)
    for(const auto& doMethodPtr : stagePtr->getDoMethods())
      intervals.insert(doMethodPtr->getInterval());
  return intervals;
}

Interval MultiStage::getEnclosingInterval() const {
  DAWN_ASSERT(!stages_.empty());
  Interval interval = (*stages_.begin())->getEnclosingInterval();

  for(auto it = std::next(stages_.begin()), end = stages_.end(); it != end; ++it)
    interval.merge((*stages_.begin())->getEnclosingInterval());

  return interval;
}

std::unordered_map<int, Field> MultiStage::getFields() const {
  std::unordered_map<int, Field> fields;

  for(const auto& stagePtr : stages_) {
    for(const Field& field : stagePtr->getFields()) {
      auto it = fields.find(field.AccessID);
      if(it != fields.end()) {
        // Adjust the Intend
        if(it->second.Intend == Field::IK_Input && field.Intend == Field::IK_Output)
          it->second.Intend = Field::IK_InputOutput;
        else if(it->second.Intend == Field::IK_Output && field.Intend == Field::IK_Input)
          it->second.Intend = Field::IK_InputOutput;

        // Merge the Extent
        it->second.Extent.merge(field.Extent);
      } else
        fields.emplace(field.AccessID, field);
    }
  }

  return fields;
}

void MultiStage::renameAllOccurrences(int oldAccessID, int newAccessID) {
  for(auto stageIt = getStages().begin(); stageIt != getStages().end(); ++stageIt) {
    Stage& stage = (**stageIt);
    for(auto& doMethodPtr : stage.getDoMethods()) {
      DoMethod& doMethod = *doMethodPtr;
      renameAccessIDInStmts(stencilInstantiation_, oldAccessID, newAccessID,
                            doMethod.getStatementAccessesPairs());
      renameAccessIDInAccesses(stencilInstantiation_, oldAccessID, newAccessID,
                               doMethod.getStatementAccessesPairs());
    }

    stage.update();
  }
}

} // namespace dawn
