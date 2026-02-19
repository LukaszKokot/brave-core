/* Copyright (c) 2026 The Brave Authors. All rights reserved.
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at https://mozilla.org/MPL/2.0/. */

#include "brave/browser/ui/views/sidebar/sidebar_container_view_new.h"

#include <algorithm>
#include <utility>

#include "base/check.h"
#include "base/functional/bind.h"
#include "base/logging.h"
#include "base/time/time.h"
#include "brave/browser/ui/brave_browser.h"
#include "brave/browser/ui/color/brave_color_id.h"
#include "brave/browser/ui/sidebar/features.h"
#include "brave/browser/ui/sidebar/sidebar_controller.h"
#include "brave/browser/ui/sidebar/sidebar_service_factory.h"
#include "brave/browser/ui/views/frame/brave_browser_view.h"
#include "brave/browser/ui/views/sidebar/sidebar_control_view.h"
#include "brave/browser/ui/views/toolbar/brave_toolbar_view.h"
#include "brave/browser/ui/views/toolbar/side_panel_button.h"
#include "brave/components/constants/pref_names.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/browser_window.h"
#include "chrome/browser/ui/browser_window/public/browser_window_features.h"
#include "chrome/browser/ui/browser_window/public/browser_window_interface.h"
#include "chrome/browser/ui/color/chrome_color_id.h"
#include "chrome/browser/ui/exclusive_access/exclusive_access_manager.h"
#include "chrome/browser/ui/exclusive_access/fullscreen_controller.h"
#include "chrome/browser/ui/views/frame/browser_view.h"
#include "chrome/browser/ui/views/side_panel/side_panel_ui.h"
#include "chrome/browser/ui/views/toolbar/toolbar_view.h"
#include "ui/base/metadata/metadata_impl_macros.h"
#include "ui/compositor/layer.h"
#include "ui/events/event_observer.h"
#include "ui/gfx/animation/tween.h"
#include "ui/gfx/geometry/point.h"
#include "ui/views/border.h"
#include "ui/views/event_monitor.h"
#include "ui/views/view_utils.h"
#include "ui/views/widget/widget.h"

namespace {

using ShowSidebarOption = sidebar::SidebarService::ShowSidebarOption;

sidebar::SidebarService* GetSidebarService(Profile* profile) {
  return sidebar::SidebarServiceFactory::GetForProfile(profile);
}

}  // namespace

class SidebarContainerViewNew::BrowserWindowEventObserver
    : public ui::EventObserver {
 public:
  explicit BrowserWindowEventObserver(SidebarContainerViewNew& host)
      : host_(host) {}
  ~BrowserWindowEventObserver() override = default;
  BrowserWindowEventObserver(const BrowserWindowEventObserver&) = delete;
  BrowserWindowEventObserver& operator=(const BrowserWindowEventObserver&) =
      delete;

  void OnEvent(const ui::Event& event) override {
    CHECK(event.IsMouseEvent());
    const auto* mouse_event = event.AsMouseEvent();

    gfx::Point window_event_position = mouse_event->location();
    // Convert window position to sidebar view's coordinate and check whether
    // it's included in sidebar ui or not.
    // If it's not included and sidebar could be hidden, stop monitoring and
    // hide UI.
    views::View::ConvertPointFromWidget(host_->sidebar_control_view_,
                                        &window_event_position);
    if (!host_->sidebar_control_view_->GetLocalBounds().Contains(
            window_event_position) &&
        !host_->ShouldForceShowSidebar()) {
      host_->StopBrowserWindowEventMonitoring();
      host_->HideSidebar();
    }
  }

 private:
  const raw_ref<SidebarContainerViewNew> host_;
};

SidebarContainerViewNew::SidebarContainerViewNew(BrowserWindowInterface* bwi)
    : views::AnimationDelegateViews(this),
      browser_window_interface_(bwi),
      browser_window_event_observer_(
          std::make_unique<BrowserWindowEventObserver>(*this)) {
  CHECK(base::FeatureList::IsEnabled(sidebar::features::kSidebarV2));
  constexpr base::TimeDelta kAnimationDuration = base::Milliseconds(150);
  width_animation_.SetSlideDuration(kAnimationDuration);
  SetNotifyEnterExitOnChild(true);
  SetUseDefaultFillLayout(true);
}

SidebarContainerViewNew::~SidebarContainerViewNew() = default;

void SidebarContainerViewNew::Init() {
  initialized_ = true;

  sidebar_model_ =
      browser_window_interface_->GetFeatures().sidebar_controller()->model();
  sidebar_model_observation_.Observe(sidebar_model_);

  show_side_panel_button_.Init(
      kShowSidePanelButton, browser_window_interface_->GetProfile()->GetPrefs(),
      base::BindRepeating(
          &SidebarContainerViewNew::UpdateToolbarButtonVisibility,
          base::Unretained(this)));

  AddChildViews();
  UpdateToolbarButtonVisibility();
  SetSidebarShowOption(
      GetSidebarService(browser_window_interface_->GetProfile())
          ->GetSidebarShowOption());
  SetBackground(views::CreateSolidBackground(kColorToolbar));
}

void SidebarContainerViewNew::SetSidebarOnLeft(bool sidebar_on_left) {
  CHECK(initialized_);

  if (sidebar_on_left_ == sidebar_on_left) {
    return;
  }

  sidebar_on_left_ = sidebar_on_left;

  CHECK(sidebar_control_view_);
  sidebar_control_view_->SetSidebarOnLeft(sidebar_on_left_);
}

bool SidebarContainerViewNew::IsSidebarVisible() const {
  CHECK(sidebar_control_view_);
  return sidebar_control_view_->GetVisible();
}

void SidebarContainerViewNew::UpdateBorder() {
  sidebar_control_view_->UpdateBackgroundAndBorder();
}

void SidebarContainerViewNew::ShowSidebarOnMouseOver(
    const gfx::PointF& point_in_screen) {
  if (IsSidebarVisible()) {
    return;
  }

  if (show_sidebar_option_ != ShowSidebarOption::kShowOnMouseOver) {
    return;
  }

  gfx::RectF mouse_event_detect_bounds(
      BraveBrowserView::From(
          BrowserView::GetBrowserViewForBrowser(browser_window_interface_))
          ->GetBoundingBoxInScreenForMouseOverHandling());

  constexpr int kHotCornerWidth = 7;
  const int inset = mouse_event_detect_bounds.width() - kHotCornerWidth;
  if (sidebar_on_left_) {
    mouse_event_detect_bounds.Inset(gfx::InsetsF::TLBR(0, 0, 0, inset));
  } else {
    mouse_event_detect_bounds.Inset(gfx::InsetsF::TLBR(0, inset, 0, 0));
  }

  if (!mouse_event_detect_bounds.Contains(point_in_screen)) {
    return;
  }

  ShowSidebar();
}

bool SidebarContainerViewNew::IsSidePanelShowing() const {
  return browser_window_interface_->GetFeatures()
      .side_panel_ui()
      ->IsSidePanelShowing(SidePanelEntry::PanelType::kContent);
}

void SidebarContainerViewNew::SetSidebarShowOption(
    ShowSidebarOption show_option) {
  show_sidebar_option_ = show_option;

  // When panel is visible, option change doesn't affect current UI status.
  if (IsSidePanelShowing()) {
    return;
  }

  if (show_sidebar_option_ == ShowSidebarOption::kShowAlways) {
    if (!IsSidebarVisible()) {
      ShowSidebar();
    }
    return;
  }

  if (show_sidebar_option_ == ShowSidebarOption::kShowNever) {
    HideSidebar();
    return;
  }

  // kShowOnMouseOver
  if (!IsMouseHovered()) {
    HideSidebar();
  }
}

void SidebarContainerViewNew::UpdateSidebarItemsState() {
  CHECK(sidebar_control_view_);

  // control view has items.
  sidebar_control_view_->Update();
}

void SidebarContainerViewNew::MenuClosed() {
  // Don't need to auto hide sidebar UI for other options.
  if (show_sidebar_option_ != ShowSidebarOption::kShowOnMouseOver) {
    return;
  }

  // Don't hide sidebar with below conditions.
  if (IsMouseHovered() || ShouldForceShowSidebar()) {
    return;
  }

  HideSidebar();
}

void SidebarContainerViewNew::AddChildViews() {
  sidebar_control_view_ = AddChildView(std::make_unique<SidebarControlView>(
      this, static_cast<BraveBrowser*>(
                browser_window_interface_->GetBrowserForMigrationOnly())));
  sidebar_control_view_->SetPaintToLayer();

  // To prevent showing layered-children while its bounds is invisible.
  sidebar_control_view_->layer()->SetMasksToBounds(true);

  // Hide by default. Visibility will be controlled by show options callback
  // later.
  sidebar_control_view_->SetVisible(false);
}

gfx::Size SidebarContainerViewNew::CalculatePreferredSize(
    const views::SizeBounds& available_size) const {
  if (!initialized_ || !sidebar_control_view_->GetVisible() ||
      IsFullscreenByTab()) {
    return gfx::Size();
  }

  if (!width_animation_.is_animating()) {
    return View::CalculatePreferredSize(available_size);
  }

  return {gfx::Tween::IntValueBetween(
              width_animation_.GetCurrentValue(), 0,
              sidebar_control_view_->GetPreferredSize().width()),
          0};
}

void SidebarContainerViewNew::OnMouseEntered(const ui::MouseEvent& event) {
  if (show_sidebar_option_ != ShowSidebarOption::kShowOnMouseOver) {
    return;
  }

  // Cancel hide schedule when mouse entered again quickly.
  sidebar_hide_timer_.Stop();
}

void SidebarContainerViewNew::OnMouseExited(const ui::MouseEvent& event) {
  if (show_sidebar_option_ != ShowSidebarOption::kShowOnMouseOver) {
    return;
  }

  if (IsMouseHovered()) {
    return;
  }

  if (ShouldForceShowSidebar()) {
    StartBrowserWindowEventMonitoring();
    return;
  }

  // Give some delay for hiding to prevent flickering by open/hide quickly.
  // when mouse is moved around the sidebar.
  constexpr int kHideDelayInMS = 400;
  sidebar_hide_timer_.Start(
      FROM_HERE, base::Milliseconds(kHideDelayInMS),
      base::BindOnce(&SidebarContainerViewNew::HideSidebar,
                     base::Unretained(this)));
}

void SidebarContainerViewNew::AnimationProgressed(
    const gfx::Animation* animation) {
  PreferredSizeChanged();
}

void SidebarContainerViewNew::AnimationEnded(const gfx::Animation* animation) {
  // Hide control view when hide animation ended.
  if (width_animation_.GetCurrentValue() == 0) {
    sidebar_control_view_->SetVisible(false);
  }

  PreferredSizeChanged();
}

void SidebarContainerViewNew::OnItemAdded(const sidebar::SidebarItem& item,
                                          size_t index,
                                          bool user_gesture) {
  sidebar_control_view_->Update();
  UpdateToolbarButtonVisibility();
}

void SidebarContainerViewNew::OnItemRemoved(size_t index) {
  sidebar_control_view_->Update();
  UpdateToolbarButtonVisibility();
}

bool SidebarContainerViewNew::ShouldUseAnimation() {
  return gfx::Animation::ShouldRenderRichAnimation();
}

void SidebarContainerViewNew::ShowSidebar() {
  // Don't need to show again if it's showing now.
  if (width_animation_.is_animating() && width_animation_.IsShowing()) {
    return;
  }

  if (width_animation_.is_animating() && width_animation_.IsClosing()) {
    width_animation_.Stop();
  } else {
    // Otherwise, reset animation to start from the beginning.
    width_animation_.Reset();
  }

  sidebar_control_view_->SetVisible(true);

  if (!ShouldUseAnimation()) {
    width_animation_.Reset(1.0);
  }
  width_animation_.Show();
}

void SidebarContainerViewNew::HideSidebar() {
  // Don't need to close again if it's closing now.
  if (width_animation_.is_animating() && width_animation_.IsClosing()) {
    return;
  }

  // Stop showing animation and start closing immediately from there.
  if (width_animation_.is_animating() && width_animation_.IsShowing()) {
    width_animation_.Stop();
  } else {
    // Otherwise, reset animation to hide from the end.
    width_animation_.Reset(1.0);
  }

  sidebar_hide_timer_.Stop();

  if (!ShouldUseAnimation()) {
    width_animation_.Reset();
  }
  width_animation_.Hide();
}

bool SidebarContainerViewNew::ShouldForceShowSidebar() const {
  // It is more reliable to check whether coordinator has current entry rather
  // than checking if spanel is visible.
  return browser_window_interface_->GetFeatures()
             .side_panel_ui()
             ->GetCurrentEntryId(SidePanelEntry::PanelType::kContent) ||
         sidebar_control_view_->IsItemReorderingInProgress() ||
         sidebar_control_view_->IsBubbleWidgetVisible();
}

void SidebarContainerViewNew::UpdateToolbarButtonVisibility() {
  // Coordinate sidebar toolbar button visibility based on
  // whether there are any sibebar items with a sidepanel.
  // This is similar to how chromium's side_panel_coordinator View
  // also has some control on the toolbar button.
  auto has_panel_item =
      GetSidebarService(browser_window_interface_->GetProfile())
          ->GetDefaultPanelItem()
          .has_value();
  auto* browser_view =
      BrowserView::GetBrowserViewForBrowser(browser_window_interface_);
  auto* brave_toolbar = static_cast<BraveToolbarView*>(browser_view->toolbar());
  if (brave_toolbar && brave_toolbar->side_panel_button()) {
    brave_toolbar->side_panel_button()->SetVisible(
        has_panel_item && show_side_panel_button_.GetValue());
  }
}

bool SidebarContainerViewNew::IsFullscreenByTab() const {
  const auto& features = browser_window_interface_->GetFeatures();
  CHECK(features.exclusive_access_manager() &&
        features.exclusive_access_manager()->fullscreen_controller());
  return features.exclusive_access_manager()
      ->fullscreen_controller()
      ->IsWindowFullscreenForTabOrPending();
}

void SidebarContainerViewNew::StartBrowserWindowEventMonitoring() {
  if (browser_window_event_monitor_) {
    return;
  }

  browser_window_event_monitor_ = views::EventMonitor::CreateWindowMonitor(
      browser_window_event_observer_.get(), GetWidget()->GetNativeWindow(),
      {ui::EventType::kMouseMoved});
}

void SidebarContainerViewNew::StopBrowserWindowEventMonitoring() {
  browser_window_event_monitor_.reset();
}

BEGIN_METADATA(SidebarContainerViewNew)
END_METADATA
