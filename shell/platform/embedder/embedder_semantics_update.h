// Copyright 2013 The Flutter Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef FLUTTER_SHELL_PLATFORM_EMBEDDER_EMBEDDER_SEMANTICS_UPDATE_H_
#define FLUTTER_SHELL_PLATFORM_EMBEDDER_EMBEDDER_SEMANTICS_UPDATE_H_

#include "flutter/shell/platform/embedder/embedder.h"
#include "lib/ui/semantics/custom_accessibility_action.h"
#include "lib/ui/semantics/semantics_node.h"

namespace flutter {

//------------------------------------------------------------------------------
/// @brief      Line 1
///             Line2
///
class EmbedderSemanticsUpdate {
 public:
  EmbedderSemanticsUpdate(const SemanticsNodeUpdates& nodes,
                          const CustomAccessibilityActionUpdates& actions);

  ~EmbedderSemanticsUpdate();

  FlutterSemanticsUpdate* get() { return &update_; }

 private:
  FlutterSemanticsUpdate update_;
  std::vector<FlutterSemanticsNode> nodes_;
  std::vector<FlutterSemanticsCustomAction> actions_;

  // Translates engine semantic nodes to embedder semantic nodes.
  void AddNode(const SemanticsNode& node);

  // Translates engine semantic custom actions to embedder semantic custom
  // actions.
  void AddAction(const CustomAccessibilityAction& action);

  FML_DISALLOW_COPY_AND_ASSIGN(EmbedderSemanticsUpdate);
};

class EmbedderSemanticsUpdate2 {
 public:
  EmbedderSemanticsUpdate2(const SemanticsNodeUpdates& nodes,
                           const CustomAccessibilityActionUpdates& actions);

  ~EmbedderSemanticsUpdate2();

  FlutterSemanticsUpdate2* get() { return &update_; }

 private:
  FlutterSemanticsUpdate2 update_;
  std::vector<FlutterSemanticsNode2> nodes_;
  std::vector<FlutterSemanticsNode2*> node_pointers_;
  std::vector<FlutterSemanticsCustomAction2> actions_;
  std::vector<FlutterSemanticsCustomAction2*> action_pointers_;

  std::vector<std::unique_ptr<std::vector<FlutterStringAttribute*>>>
      node_string_attributes_;
  std::vector<std::unique_ptr<FlutterStringAttribute>> string_attributes_;
  std::vector<std::unique_ptr<FlutterSpellOutStringAttribute>>
      spell_out_attributes_;
  std::vector<std::unique_ptr<FlutterLocaleStringAttribute>> locale_attributes_;

  // Translates engine semantic nodes to embedder semantic nodes.
  void AddNode(const SemanticsNode& node);

  // Translates engine semantic custom actions to embedder semantic custom
  // actions.
  void AddAction(const CustomAccessibilityAction& action);

  // Translates engine string attributes to embedder string attributes.
  std::pair<size_t, FlutterStringAttribute**> CreateStringAttributes(
      const StringAttributes& attribute);

  FML_DISALLOW_COPY_AND_ASSIGN(EmbedderSemanticsUpdate2);
};

}  // namespace flutter

#endif  // FLUTTER_SHELL_PLATFORM_EMBEDDER_EMBEDDER_SEMANTICS_UPDATE_H_
