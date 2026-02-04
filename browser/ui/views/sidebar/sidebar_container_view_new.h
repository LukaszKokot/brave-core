/* Copyright (c) 2026 The Brave Authors. All rights reserved.
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at https://mozilla.org/MPL/2.0/. */

#ifndef BRAVE_BROWSER_UI_VIEWS_SIDEBAR_SIDEBAR_CONTAINER_VIEW_NEW_H_
#define BRAVE_BROWSER_UI_VIEWS_SIDEBAR_SIDEBAR_CONTAINER_VIEW_NEW_H_

#include <memory>

#include "base/memory/raw_ptr.h"
#include "base/timer/timer.h"
#include "brave/browser/ui/sidebar/sidebar.h"
#include "brave/browser/ui/sidebar/sidebar_model.h"
#include "brave/browser/ui/views/sidebar/sidebar_control_view.h"
#include "brave/components/sidebar/browser/sidebar_service.h"
#include "components/prefs/pref_member.h"
#include "ui/events/event_observer.h"
#include "ui/gfx/animation/slide_animation.h"
#include "ui/views/animation/animation_delegate_views.h"
#include "ui/views/view.h"

namespace views {
class EventMonitor;
}  // namespace views

namespace sidebar {
class SidebarBrowserTest;
}  // namespace sidebar

class BrowserWindowInterface;

// This view manages the sidebar control UI (button bar) only.
// Unlike SidebarContainerView, this does not wrap or manage the side panel.
// The side panel is managed directly by upstream BraveBrowserView.
// This class controls the visibility of the control view based on show options.
// Gated behind the sidebar::features::kSidebarV2 flag.
class SidebarContainerViewNew : public sidebar::Sidebar,
                                public SidebarControlView::Delegate,
                                public views::View,
                                public views::AnimationDelegateViews,
                                public sidebar::SidebarModel::Observer {
  METADATA_HEADER(SidebarContainerViewNew, views::View)
 public:
  explicit SidebarContainerViewNew(BrowserWindowInterface* bwi);
  ~SidebarContainerViewNew() override;

  SidebarContainerViewNew(const SidebarContainerViewNew&) = delete;
  SidebarContainerViewNew& operator=(const SidebarContainerViewNew&) = delete;

  void Init();

  bool sidebar_on_left() const { return sidebar_on_left_; }
  void SetSidebarOnLeft(bool sidebar_on_left);

  bool IsSidebarVisible() const;
  void UpdateBorder();

  // Show sidebar if the hot corner contains |point_in_screen|.
  void ShowSidebarOnMouseOver(const gfx::PointF& point_in_screen);

  // sidebar::Sidebar overrides:
  void SetSidebarShowOption(
      sidebar::SidebarService::ShowSidebarOption show_option) override;
  void UpdateSidebarItemsState() override;

  // SidebarControlView::Delegate overrides:
  void MenuClosed() override;

  // views::View overrides:
  gfx::Size CalculatePreferredSize(
      const views::SizeBounds& available_size) const override;
  void OnMouseEntered(const ui::MouseEvent& event) override;
  void OnMouseExited(const ui::MouseEvent& event) override;

  // views::AnimationDelegateViews overrides:
  void AnimationProgressed(const gfx::Animation* animation) override;
  void AnimationEnded(const gfx::Animation* animation) override;

  // sidebar::SidebarModel::Observer overrides:
  void OnItemAdded(const sidebar::SidebarItem& item,
                   size_t index,
                   bool user_gesture) override;
  void OnItemRemoved(size_t index) override;

 private:
  friend class sidebar::SidebarBrowserTest;

  class BrowserWindowEventObserver;

  void AddChildViews();
  bool ShouldUseAnimation();
  void ShowSidebar();
  void HideSidebar();
  bool IsSidePanelShowing() const;
  bool ShouldForceShowSidebar() const;
  void UpdateToolbarButtonVisibility();
  bool IsFullscreenByTab() const;

  // On some condition(ex, add item bubble is visible),
  // sidebar should not be hidden even if mouse goes out from sidebar ui.
  // If it's hidden, only bubble ui is visible. Then, weird situation happens.
  // (Sidear UI is hidden and bubble is only visible)
  // With this handling, sidebar ui is still visible after this bubble ui is
  // disappeared. To make sidebar ui hidden, this widget's
  // event is monitored. |BrowserWindowEventObserver| will get widget's
  // mouse event when mouse is exited from sidebar ui but sidebar ui is shown.
  // |BrowserWindowEventObserver| will ask to stop monitoring and ask to hide
  // sidebar ui when those conditions are cleared and current mouse is outside
  // of sidebar ui.
  void StartBrowserWindowEventMonitoring();
  void StopBrowserWindowEventMonitoring();

  raw_ptr<BrowserWindowInterface> browser_window_interface_ = nullptr;
  raw_ptr<sidebar::SidebarModel> sidebar_model_ = nullptr;
  raw_ptr<SidebarControlView> sidebar_control_view_ = nullptr;
  bool initialized_ = false;
  bool sidebar_on_left_ = true;
  base::OneShotTimer sidebar_hide_timer_;
  sidebar::SidebarService::ShowSidebarOption show_sidebar_option_ =
      sidebar::SidebarService::ShowSidebarOption::kShowAlways;
  gfx::SlideAnimation width_animation_{this};
  std::unique_ptr<BrowserWindowEventObserver> browser_window_event_observer_;
  std::unique_ptr<views::EventMonitor> browser_window_event_monitor_;
  BooleanPrefMember show_side_panel_button_;
  base::ScopedObservation<sidebar::SidebarModel,
                          sidebar::SidebarModel::Observer>
      sidebar_model_observation_{this};
};

#endif  // BRAVE_BROWSER_UI_VIEWS_SIDEBAR_SIDEBAR_CONTAINER_VIEW_NEW_H_
