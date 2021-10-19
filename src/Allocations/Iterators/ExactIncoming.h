// Copyright (c) 2017,2020-2021 VMware, Inc. All Rights Reserved.
// SPDX-License-Identifier: GPL-2.0

#pragma once
#include "../../Commands/Runner.h"
#include "../../Commands/Subcommand.h"
#include "../ContiguousImage.h"
#include "../Directory.h"
#include "../EdgePredicate.h"
#include "../Graph.h"
#include "../SetCache.h"
namespace chap {
namespace Allocations {
namespace Iterators {
template <class Offset>
class ExactIncoming {
 public:
  class Factory {
   public:
    Factory() : _setName("exactincoming") {}
    ExactIncoming* MakeIterator(Commands::Context& context,
                                const ProcessImage<Offset>& processImage,
                                const Directory<Offset>& directory,
                                const SetCache<Offset>&) {
      AllocationIndex numAllocations = directory.NumAllocations();
      Commands::Error& error = context.GetError();

      size_t numPositionals = context.GetNumPositionals();
      if (numPositionals < 3) {
        error << "No address was specified for the target allocation.\n";
        return nullptr;
      }

      Offset address;
      if (!context.ParsePositional(2, address)) {
        error << context.Positional(2) << " is not a valid address.\n";
        return nullptr;
      }
      AllocationIndex index = directory.AllocationIndexOf(address);
      if (index == numAllocations) {
        error << context.Positional(2) << " is not part of an allocation.\n";
        return nullptr;
      }
      const Graph<Offset>* allocationGraph = processImage.GetAllocationGraph();
      if (allocationGraph == 0) {
        error << "Allocation graph is not available.\n";
        return nullptr;
      }

      bool skipTaintedReferences = false;
      if (!context.ParseBooleanSwitch("skipTaintedReferences",
                                      skipTaintedReferences)) {
        return nullptr;
      }

      bool skipUnfavoredReferences = false;
      if (!context.ParseBooleanSwitch("skipUnfavoredReferences",
                                      skipUnfavoredReferences)) {
        return nullptr;
      }

      /*
       * If the target allocation does not support favored references, we can
       * just treat skipUnreferences as false because the target cannot
       * possible have any unfavored references.
       */
      if (!processImage.GetAllocationTagHolder()->SupportsFavoredReferences(
              index)) {
        skipUnfavoredReferences = false;
      }

      return new ExactIncoming(
          directory, *allocationGraph, processImage.GetVirtualAddressMap(),
          index, numAllocations,

          processImage.GetEdgeIsTainted(), skipTaintedReferences,
          processImage.GetEdgeIsFavored(), skipUnfavoredReferences);
    }

    const std::string& GetSetName() const { return _setName; }
    size_t GetNumArguments() { return 1; }
    const std::vector<std::string>& GetTaints() const { return _taints; }
    void ShowHelpMessage(Commands::Context& context) {
      Commands::Output& output = context.GetOutput();
      output
          << "Use \"exactincoming <address-in-hex>\""
             " to specify the set of all allocations that\n"
             "reference the start of the allocation that contains the specified"
             " address.\n";
    }

   private:
    const std::vector<std::string> _taints;
    const std::string _setName;
  };
  typedef typename Directory<Offset>::AllocationIndex AllocationIndex;
  typedef typename Directory<Offset>::Allocation Allocation;
  typedef typename Graph<Offset>::EdgeIndex EdgeIndex;

  ExactIncoming(const Directory<Offset>& directory, const Graph<Offset>& graph,
                const VirtualAddressMap<Offset>& addressMap,
                AllocationIndex index, AllocationIndex numAllocations,
                const EdgePredicate<Offset>* edgeIsTainted,
                bool skipTaintedReferences,
                const EdgePredicate<Offset>* edgeIsFavored,
                bool skipUnfavoredReferences)
      : _directory(directory),
        _contiguousImage(addressMap, _directory),
        _graph(graph),
        _addressMap(addressMap),
        _index(index),
        _numAllocations(numAllocations),
        _edgeIsTainted(*edgeIsTainted),
        _skipTaintedReferences(skipTaintedReferences),
        _edgeIsFavored(*edgeIsFavored),
        _skipUnfavoredReferences(skipUnfavoredReferences) {
    _target = _directory.AllocationAt(index)->Address();
    _graph.GetIncoming(index, _nextIncoming, _pastIncoming);
  }

  AllocationIndex Next() {
    for (; _nextIncoming != _pastIncoming; _nextIncoming++) {
      if (_skipTaintedReferences && _edgeIsTainted.ForIncoming(_nextIncoming)) {
        continue;
      }
      /*
       * _skipUnfavoredReferences will be clear if the target has been
       * determined
       * not to support favored references.
       */
      if (_skipUnfavoredReferences &&
          !_edgeIsFavored.ForIncoming(_nextIncoming)) {
        continue;
      }
      AllocationIndex index = _graph.GetSourceForIncoming(_nextIncoming);
      const Allocation* allocation = _directory.AllocationAt(index);
      if (allocation == nullptr) {
        abort();
      }
      if (allocation->IsUsed()) {
        _contiguousImage.SetIndex(index);
        const Offset* firstOffset = _contiguousImage.FirstOffset();
        const Offset* offsetLimit = _contiguousImage.OffsetLimit();
        for (const Offset* nextOffset = firstOffset; nextOffset != offsetLimit;
             nextOffset++) {
          if (_target == *nextOffset) {
            _nextIncoming++;
            return index;
          }
        }
      }
    }
    return _numAllocations;
  }

 private:
  const Directory<Offset>& _directory;
  ContiguousImage<Offset> _contiguousImage;
  const Graph<Offset>& _graph;
  const VirtualAddressMap<Offset>& _addressMap;
  AllocationIndex _index;
  AllocationIndex _numAllocations;
  const EdgePredicate<Offset>& _edgeIsTainted;
  bool _skipTaintedReferences;
  const EdgePredicate<Offset>& _edgeIsFavored;
  bool _skipUnfavoredReferences;
  EdgeIndex _nextIncoming;
  EdgeIndex _pastIncoming;
  Offset _target;
};
}  // namespace Iterators
}  // namespace Allocations
}  // namespace chap
