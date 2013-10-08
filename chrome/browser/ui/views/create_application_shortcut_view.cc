// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ui/views/create_application_shortcut_view.h"

#include <algorithm>

#include "base/bind.h"
#include "base/bind_helpers.h"
#include "base/prefs/pref_service.h"
#include "base/strings/utf_string_conversions.h"
#include "base/win/windows_version.h"
#include "chrome/browser/extensions/tab_helper.h"
#include "chrome/browser/favicon/favicon_util.h"
#include "chrome/browser/history/select_favicon_frames.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/browser_commands.h"
#include "chrome/browser/ui/browser_finder.h"
#include "chrome/browser/ui/views/constrained_window_views.h"
#include "chrome/browser/ui/web_applications/web_app_ui.h"
#include "chrome/browser/ui/webui/extensions/extension_icon_source.h"
#include "chrome/common/chrome_constants.h"
#include "chrome/common/extensions/extension.h"
#include "chrome/common/pref_names.h"
#include "content/public/browser/render_view_host.h"
#include "content/public/browser/render_widget_host_view.h"
#include "content/public/browser/web_contents.h"
#include "grit/chromium_strings.h"
#include "grit/generated_resources.h"
#include "grit/locale_settings.h"
#include "grit/theme_resources.h"
#include "net/base/load_flags.h"
#include "net/url_request/url_request.h"
#include "skia/ext/image_operations.h"
#include "third_party/skia/include/core/SkBitmap.h"
#include "third_party/skia/include/core/SkPaint.h"
#include "third_party/skia/include/core/SkRect.h"
#include "ui/base/l10n/l10n_util.h"
#include "ui/base/layout.h"
#include "ui/base/resource/resource_bundle.h"
#include "ui/gfx/canvas.h"
#include "ui/gfx/codec/png_codec.h"
#include "ui/gfx/image/image_family.h"
#include "ui/gfx/image/image_skia.h"
#include "ui/views/controls/button/checkbox.h"
#include "ui/views/controls/image_view.h"
#include "ui/views/controls/label.h"
#include "ui/views/layout/grid_layout.h"
#include "ui/views/layout/layout_constants.h"
#include "ui/views/widget/widget.h"
#include "ui/views/window/dialog_client_view.h"
#include "url/gurl.h"

namespace {

const int kIconPreviewSizePixels = 32;

// AppInfoView shows the application icon and title.
class AppInfoView : public views::View {
 public:
  AppInfoView(const string16& title,
              const string16& description,
              const gfx::ImageFamily& icon);

  // Updates the title/description of the web app.
  void UpdateText(const string16& title, const string16& description);

  // Updates the icon of the web app.
  void UpdateIcon(const gfx::ImageFamily& image);

  // Overridden from views::View:
  virtual void OnPaint(gfx::Canvas* canvas) OVERRIDE;

 private:
  // Initializes the controls
  void Init(const string16& title,
            const string16& description, const gfx::ImageFamily& icon);

  // Creates or updates description label.
  void PrepareDescriptionLabel(const string16& description);

  // Sets up layout manager.
  void SetupLayout();

  views::ImageView* icon_;
  views::Label* title_;
  views::Label* description_;
};

AppInfoView::AppInfoView(const string16& title,
                         const string16& description,
                         const gfx::ImageFamily& icon)
    : icon_(NULL),
      title_(NULL),
      description_(NULL) {
  Init(title, description, icon);
}

void AppInfoView::Init(const string16& title_text,
                       const string16& description_text,
                       const gfx::ImageFamily& icon) {
  icon_ = new views::ImageView();
  UpdateIcon(icon);
  icon_->SetImageSize(gfx::Size(kIconPreviewSizePixels,
                                kIconPreviewSizePixels));

  title_ = new views::Label(title_text);
  title_->SetMultiLine(true);
  title_->SetHorizontalAlignment(gfx::ALIGN_LEFT);
  title_->SetFont(ui::ResourceBundle::GetSharedInstance().GetFont(
      ui::ResourceBundle::BaseFont).DeriveFont(0, gfx::Font::BOLD));

  PrepareDescriptionLabel(description_text);

  SetupLayout();
}

void AppInfoView::PrepareDescriptionLabel(const string16& description) {
  // Do not make space for the description if it is empty.
  if (description.empty())
    return;

  const size_t kMaxLength = 200;
  const string16 kEllipsis(ASCIIToUTF16(" ... "));

  string16 text = description;
  if (text.length() > kMaxLength) {
    text = text.substr(0, kMaxLength);
    text += kEllipsis;
  }

  if (description_) {
    description_->SetText(text);
  } else {
    description_ = new views::Label(text);
    description_->SetMultiLine(true);
    description_->SetHorizontalAlignment(gfx::ALIGN_LEFT);
  }
}

void AppInfoView::SetupLayout() {
  views::GridLayout* layout = views::GridLayout::CreatePanel(this);
  SetLayoutManager(layout);

  static const int kColumnSetId = 0;
  views::ColumnSet* column_set = layout->AddColumnSet(kColumnSetId);
  column_set->AddColumn(views::GridLayout::CENTER, views::GridLayout::LEADING,
                        20.0f, views::GridLayout::FIXED,
                        kIconPreviewSizePixels, kIconPreviewSizePixels);
  column_set->AddColumn(views::GridLayout::FILL, views::GridLayout::CENTER,
                        80.0f, views::GridLayout::USE_PREF, 0, 0);

  layout->StartRow(0, kColumnSetId);
  layout->AddView(icon_, 1, description_ ? 2 : 1);
  layout->AddView(title_);

  if (description_) {
    layout->StartRow(0, kColumnSetId);
    layout->SkipColumns(1);
    layout->AddView(description_);
  }
}

void AppInfoView::UpdateText(const string16& title,
                             const string16& description) {
  title_->SetText(title);
  PrepareDescriptionLabel(description);

  SetupLayout();
}

void AppInfoView::UpdateIcon(const gfx::ImageFamily& image) {
  // Get the icon closest to the desired preview size.
  const gfx::Image* icon = image.GetBest(kIconPreviewSizePixels,
                                         kIconPreviewSizePixels);
  if (!icon || icon->IsEmpty())
    // The family has no icons. Leave the image blank.
    return;
  const SkBitmap& bitmap = *icon->ToSkBitmap();
  if (bitmap.width() == kIconPreviewSizePixels &&
      bitmap.height() == kIconPreviewSizePixels) {
    icon_->SetImage(gfx::ImageSkia::CreateFrom1xBitmap(bitmap));
  } else {
    // Resize the image to the desired size.
    SkBitmap resized_bitmap = skia::ImageOperations::Resize(
        bitmap, skia::ImageOperations::RESIZE_LANCZOS3,
        kIconPreviewSizePixels, kIconPreviewSizePixels);

    icon_->SetImage(gfx::ImageSkia::CreateFrom1xBitmap(resized_bitmap));
  }
}

void AppInfoView::OnPaint(gfx::Canvas* canvas) {
  gfx::Rect bounds = GetLocalBounds();

  SkRect border_rect = {
    SkIntToScalar(bounds.x()),
    SkIntToScalar(bounds.y()),
    SkIntToScalar(bounds.right()),
    SkIntToScalar(bounds.bottom())
  };

  SkPaint border_paint;
  border_paint.setAntiAlias(true);
  border_paint.setARGB(0xFF, 0xC8, 0xC8, 0xC8);

  canvas->sk_canvas()->drawRoundRect(border_rect, SkIntToScalar(2),
                                     SkIntToScalar(2), border_paint);

  SkRect inner_rect = {
    border_rect.fLeft + SkDoubleToScalar(0.5),
    border_rect.fTop + SkDoubleToScalar(0.5),
    border_rect.fRight - SkDoubleToScalar(0.5),
    border_rect.fBottom - SkDoubleToScalar(0.5),
  };

  SkPaint inner_paint;
  inner_paint.setAntiAlias(true);
  inner_paint.setARGB(0xFF, 0xF8, 0xF8, 0xF8);
  canvas->sk_canvas()->drawRoundRect(inner_rect, SkDoubleToScalar(1.5),
                                     SkDoubleToScalar(1.5), inner_paint);
}

}  // namespace

namespace chrome {

void ShowCreateWebAppShortcutsDialog(gfx::NativeWindow parent_window,
                                     content::WebContents* web_contents) {
  CreateBrowserModalDialogViews(
      new CreateUrlApplicationShortcutView(web_contents),
      parent_window)->Show();
}

void ShowCreateChromeAppShortcutsDialog(
    gfx::NativeWindow parent_window,
    Profile* profile,
    const extensions::Extension* app,
    const base::Closure& close_callback) {
  CreateBrowserModalDialogViews(
      new CreateChromeApplicationShortcutView(profile, app, close_callback),
      parent_window)->Show();
}

}  // namespace chrome

CreateApplicationShortcutView::CreateApplicationShortcutView(Profile* profile)
    : profile_(profile),
      app_info_(NULL),
      create_shortcuts_label_(NULL),
      desktop_check_box_(NULL),
      menu_check_box_(NULL),
      quick_launch_check_box_(NULL) {}

CreateApplicationShortcutView::~CreateApplicationShortcutView() {}

void CreateApplicationShortcutView::InitControls() {
  // Create controls
  app_info_ = new AppInfoView(shortcut_info_.title, shortcut_info_.description,
                              shortcut_info_.favicon);
  create_shortcuts_label_ = new views::Label(
      l10n_util::GetStringUTF16(IDS_CREATE_SHORTCUTS_LABEL));
  create_shortcuts_label_->SetHorizontalAlignment(gfx::ALIGN_LEFT);

  desktop_check_box_ = AddCheckbox(
      l10n_util::GetStringUTF16(IDS_CREATE_SHORTCUTS_DESKTOP_CHKBOX),
      profile_->GetPrefs()->GetBoolean(prefs::kWebAppCreateOnDesktop));

  menu_check_box_ = NULL;
  quick_launch_check_box_ = NULL;

#if defined(OS_WIN)
  // Do not allow creating shortcuts on the Start Screen for Windows 8.
  if (base::win::GetVersion() < base::win::VERSION_WIN8) {
    menu_check_box_ = AddCheckbox(
        l10n_util::GetStringUTF16(IDS_CREATE_SHORTCUTS_START_MENU_CHKBOX),
        profile_->GetPrefs()->GetBoolean(prefs::kWebAppCreateInAppsMenu));
  }

  quick_launch_check_box_ = AddCheckbox(
      (base::win::GetVersion() >= base::win::VERSION_WIN7) ?
        l10n_util::GetStringUTF16(IDS_PIN_TO_TASKBAR_CHKBOX) :
        l10n_util::GetStringUTF16(
            IDS_CREATE_SHORTCUTS_QUICK_LAUNCH_BAR_CHKBOX),
      profile_->GetPrefs()->GetBoolean(prefs::kWebAppCreateInQuickLaunchBar));
#elif defined(OS_POSIX)
  menu_check_box_ = AddCheckbox(
      l10n_util::GetStringUTF16(IDS_CREATE_SHORTCUTS_MENU_CHKBOX),
      profile_->GetPrefs()->GetBoolean(prefs::kWebAppCreateInAppsMenu));
#endif

  // Layout controls
  views::GridLayout* layout = views::GridLayout::CreatePanel(this);
  SetLayoutManager(layout);

  static const int kHeaderColumnSetId = 0;
  views::ColumnSet* column_set = layout->AddColumnSet(kHeaderColumnSetId);
  column_set->AddColumn(views::GridLayout::FILL, views::GridLayout::CENTER,
                        100.0f, views::GridLayout::FIXED, 0, 0);

  static const int kTableColumnSetId = 1;
  column_set = layout->AddColumnSet(kTableColumnSetId);
  column_set->AddPaddingColumn(0, views::kPanelHorizIndentation);
  column_set->AddColumn(views::GridLayout::FILL, views::GridLayout::FILL,
                        100.0f, views::GridLayout::USE_PREF, 0, 0);

  layout->StartRow(0, kHeaderColumnSetId);
  layout->AddView(app_info_);

  layout->AddPaddingRow(0, views::kPanelSubVerticalSpacing);
  layout->StartRow(0, kHeaderColumnSetId);
  layout->AddView(create_shortcuts_label_);

  layout->AddPaddingRow(0, views::kLabelToControlVerticalSpacing);
  layout->StartRow(0, kTableColumnSetId);
  layout->AddView(desktop_check_box_);

  if (menu_check_box_ != NULL) {
    layout->AddPaddingRow(0, views::kRelatedControlSmallVerticalSpacing);
    layout->StartRow(0, kTableColumnSetId);
    layout->AddView(menu_check_box_);
  }

  if (quick_launch_check_box_ != NULL) {
    layout->AddPaddingRow(0, views::kRelatedControlSmallVerticalSpacing);
    layout->StartRow(0, kTableColumnSetId);
    layout->AddView(quick_launch_check_box_);
  }
}

gfx::Size CreateApplicationShortcutView::GetPreferredSize() {
  // TODO(evanm): should this use IDS_CREATE_SHORTCUTS_DIALOG_WIDTH_CHARS?
  static const int kDialogWidth = 360;
  int height = GetLayoutManager()->GetPreferredHeightForWidth(this,
      kDialogWidth);
  return gfx::Size(kDialogWidth, height);
}

string16 CreateApplicationShortcutView::GetDialogButtonLabel(
    ui::DialogButton button) const {
  if (button == ui::DIALOG_BUTTON_OK)
    return l10n_util::GetStringUTF16(IDS_CREATE_SHORTCUTS_COMMIT);
  return views::DialogDelegateView::GetDialogButtonLabel(button);
}

bool CreateApplicationShortcutView::IsDialogButtonEnabled(
    ui::DialogButton button) const {
  if (button == ui::DIALOG_BUTTON_OK)
    return desktop_check_box_->checked() ||
           ((menu_check_box_ != NULL) &&
            menu_check_box_->checked()) ||
           ((quick_launch_check_box_ != NULL) &&
            quick_launch_check_box_->checked());

  return true;
}

ui::ModalType CreateApplicationShortcutView::GetModalType() const {
  return ui::MODAL_TYPE_WINDOW;
}

string16 CreateApplicationShortcutView::GetWindowTitle() const {
  return l10n_util::GetStringUTF16(IDS_CREATE_SHORTCUTS_TITLE);
}

bool CreateApplicationShortcutView::Accept() {
  if (!IsDialogButtonEnabled(ui::DIALOG_BUTTON_OK))
    return false;

  ShellIntegration::ShortcutLocations creation_locations;
  creation_locations.on_desktop = desktop_check_box_->checked();
  creation_locations.in_applications_menu = menu_check_box_ == NULL ? false :
      menu_check_box_->checked();
  creation_locations.applications_menu_subdir = shortcut_menu_subdir_;

#if defined(OS_WIN)
  creation_locations.in_quick_launch_bar = quick_launch_check_box_ == NULL ?
      NULL : quick_launch_check_box_->checked();
#elif defined(OS_POSIX)
  // Create shortcut in Mac dock or as Linux (gnome/kde) application launcher
  // are not implemented yet.
  creation_locations.in_quick_launch_bar = false;
#endif

  web_app::CreateShortcuts(shortcut_info_, creation_locations,
                           web_app::SHORTCUT_CREATION_BY_USER);
  return true;
}

views::Checkbox* CreateApplicationShortcutView::AddCheckbox(
    const string16& text, bool checked) {
  views::Checkbox* checkbox = new views::Checkbox(text);
  checkbox->SetChecked(checked);
  checkbox->set_listener(this);
  return checkbox;
}

void CreateApplicationShortcutView::ButtonPressed(views::Button* sender,
                                                  const ui::Event& event) {
  if (sender == desktop_check_box_) {
    profile_->GetPrefs()->SetBoolean(prefs::kWebAppCreateOnDesktop,
                                     desktop_check_box_->checked());
  } else if (sender == menu_check_box_) {
    profile_->GetPrefs()->SetBoolean(prefs::kWebAppCreateInAppsMenu,
                                     menu_check_box_->checked());
  } else if (sender == quick_launch_check_box_) {
    profile_->GetPrefs()->SetBoolean(prefs::kWebAppCreateInQuickLaunchBar,
                                     quick_launch_check_box_->checked());
  }

  // When no checkbox is checked we should not have the action button enabled.
  GetDialogClientView()->UpdateDialogButtons();
}

CreateUrlApplicationShortcutView::CreateUrlApplicationShortcutView(
    content::WebContents* web_contents)
    : CreateApplicationShortcutView(
          Profile::FromBrowserContext(web_contents->GetBrowserContext())),
      web_contents_(web_contents),
      pending_download_id_(-1)  {

  web_app::GetShortcutInfoForTab(web_contents_, &shortcut_info_);
  const WebApplicationInfo& app_info =
      extensions::TabHelper::FromWebContents(web_contents_)->web_app_info();
  if (!app_info.icons.empty()) {
    web_app::GetIconsInfo(app_info, &unprocessed_icons_);
    FetchIcon();
  }

  // NOTE: Leave shortcut_menu_subdir_ blank to create URL app shortcuts in the
  // top-level menu.

  InitControls();
}

CreateUrlApplicationShortcutView::~CreateUrlApplicationShortcutView() {
}

bool CreateUrlApplicationShortcutView::Accept() {
  if (!CreateApplicationShortcutView::Accept())
    return false;

  // Get the smallest icon in the icon family (should have only 1).
  const gfx::Image* icon = shortcut_info_.favicon.GetBest(0, 0);
  SkBitmap bitmap = icon ? icon->AsBitmap() : SkBitmap();
  extensions::TabHelper::FromWebContents(web_contents_)->SetAppIcon(bitmap);
  Browser* browser = chrome::FindBrowserWithWebContents(web_contents_);
  if (browser)
    chrome::ConvertTabToAppWindow(browser, web_contents_);
  return true;
}

void CreateUrlApplicationShortcutView::FetchIcon() {
  // There should only be fetch job at a time.
  DCHECK_EQ(-1, pending_download_id_);

  if (unprocessed_icons_.empty())  // No icons to fetch.
    return;

  int preferred_size = std::max(unprocessed_icons_.back().width,
                                unprocessed_icons_.back().height);
  pending_download_id_ = web_contents_->DownloadImage(
      unprocessed_icons_.back().url,
      true,  // is a favicon
      preferred_size,
      0,  // no maximum size
      base::Bind(&CreateUrlApplicationShortcutView::DidDownloadFavicon,
                 base::Unretained(this)));

  unprocessed_icons_.pop_back();
}

void CreateUrlApplicationShortcutView::DidDownloadFavicon(
    int id,
    int http_status_code,
    const GURL& image_url,
    int requested_size,
    const std::vector<SkBitmap>& bitmaps) {
  if (id != pending_download_id_)
    return;
  pending_download_id_ = -1;

  SkBitmap image;

  if (!bitmaps.empty()) {
    std::vector<ui::ScaleFactor> scale_factors;
    ui::ScaleFactor scale_factor = ui::GetScaleFactorForNativeView(
        web_contents_->GetRenderViewHost()->GetView()->GetNativeView());
    scale_factors.push_back(scale_factor);
    size_t closest_index = FaviconUtil::SelectBestFaviconFromBitmaps(
        bitmaps,
        scale_factors,
        requested_size);
    image = bitmaps[closest_index];
  }

  if (!image.isNull()) {
    shortcut_info_.favicon.Add(gfx::ImageSkia::CreateFrom1xBitmap(image));
    static_cast<AppInfoView*>(app_info_)->UpdateIcon(shortcut_info_.favicon);
  } else {
    FetchIcon();
  }
}

CreateChromeApplicationShortcutView::CreateChromeApplicationShortcutView(
    Profile* profile,
    const extensions::Extension* app,
    const base::Closure& close_callback)
        : CreateApplicationShortcutView(profile),
          app_(app),
          close_callback_(close_callback),
          weak_ptr_factory_(this) {
  // Required by InitControls().
  shortcut_info_.title = UTF8ToUTF16(app->name());
  shortcut_info_.description = UTF8ToUTF16(app->description());

  // Place Chrome app shortcuts in the "Chrome Apps" submenu.
  shortcut_menu_subdir_ = web_app::GetAppShortcutsSubdirName();

  InitControls();

  // Get shortcut information and icon now; they are needed for our UI.
  web_app::UpdateShortcutInfoAndIconForApp(
      *app, profile,
      base::Bind(&CreateChromeApplicationShortcutView::OnShortcutInfoLoaded,
                 weak_ptr_factory_.GetWeakPtr()));
}

CreateChromeApplicationShortcutView::~CreateChromeApplicationShortcutView() {}

bool CreateChromeApplicationShortcutView::Accept() {
  if (!close_callback_.is_null())
    close_callback_.Run();
  return CreateApplicationShortcutView::Accept();
}

bool CreateChromeApplicationShortcutView::Cancel() {
  if (!close_callback_.is_null())
    close_callback_.Run();
  return CreateApplicationShortcutView::Cancel();
}

// Called when the app's ShortcutInfo (with icon) is loaded.
void CreateChromeApplicationShortcutView::OnShortcutInfoLoaded(
    const ShellIntegration::ShortcutInfo& shortcut_info) {
  shortcut_info_ = shortcut_info;

  CHECK(app_info_);
  static_cast<AppInfoView*>(app_info_)->UpdateIcon(shortcut_info_.favicon);
}
