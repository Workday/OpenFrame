// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/devtools/devtools_file_helper.h"

#include <set>
#include <vector>

#include "base/bind.h"
#include "base/callback.h"
#include "base/file_util.h"
#include "base/lazy_instance.h"
#include "base/md5.h"
#include "base/prefs/pref_service.h"
#include "base/strings/utf_string_conversions.h"
#include "base/value_conversions.h"
#include "chrome/browser/browser_process.h"
#include "chrome/browser/download/download_prefs.h"
#include "chrome/browser/platform_util.h"
#include "chrome/browser/prefs/scoped_user_pref_update.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/chrome_select_file_policy.h"
#include "chrome/common/pref_names.h"
#include "content/public/browser/browser_context.h"
#include "content/public/browser/browser_thread.h"
#include "content/public/browser/child_process_security_policy.h"
#include "content/public/browser/download_manager.h"
#include "content/public/browser/render_process_host.h"
#include "content/public/browser/render_view_host.h"
#include "content/public/browser/web_contents.h"
#include "content/public/browser/web_contents_view.h"
#include "content/public/common/content_client.h"
#include "content/public/common/url_constants.h"
#include "grit/generated_resources.h"
#include "ui/base/l10n/l10n_util.h"
#include "ui/shell_dialogs/select_file_dialog.h"
#include "webkit/browser/fileapi/isolated_context.h"
#include "webkit/common/fileapi/file_system_util.h"

using base::Bind;
using base::Callback;
using content::BrowserContext;
using content::BrowserThread;
using content::DownloadManager;
using content::RenderViewHost;
using content::WebContents;
using std::set;

namespace {

base::LazyInstance<base::FilePath>::Leaky
    g_last_save_path = LAZY_INSTANCE_INITIALIZER;

}  // namespace

namespace {

typedef Callback<void(const base::FilePath&)> SelectedCallback;
typedef Callback<void(void)> CanceledCallback;

class SelectFileDialog : public ui::SelectFileDialog::Listener,
                         public base::RefCounted<SelectFileDialog> {
 public:
  SelectFileDialog(const SelectedCallback& selected_callback,
                   const CanceledCallback& canceled_callback,
                   WebContents* web_contents)
      : selected_callback_(selected_callback),
        canceled_callback_(canceled_callback),
        web_contents_(web_contents) {
    select_file_dialog_ = ui::SelectFileDialog::Create(
        this, new ChromeSelectFilePolicy(NULL));
  }

  void Show(ui::SelectFileDialog::Type type,
            const base::FilePath& default_path) {
    AddRef();  // Balanced in the three listener outcomes.
    select_file_dialog_->SelectFile(
      type,
      string16(),
      default_path,
      NULL,
      0,
      base::FilePath::StringType(),
      platform_util::GetTopLevel(web_contents_->GetView()->GetNativeView()),
      NULL);
  }

  // ui::SelectFileDialog::Listener implementation.
  virtual void FileSelected(const base::FilePath& path,
                            int index,
                            void* params) OVERRIDE {
    selected_callback_.Run(path);
    Release();  // Balanced in ::Show.
  }

  virtual void MultiFilesSelected(const std::vector<base::FilePath>& files,
                                  void* params) OVERRIDE {
    Release();  // Balanced in ::Show.
    NOTREACHED() << "Should not be able to select multiple files";
  }

  virtual void FileSelectionCanceled(void* params) OVERRIDE {
    canceled_callback_.Run();
    Release();  // Balanced in ::Show.
  }

 private:
  friend class base::RefCounted<SelectFileDialog>;
  virtual ~SelectFileDialog() {}

  scoped_refptr<ui::SelectFileDialog> select_file_dialog_;
  SelectedCallback selected_callback_;
  CanceledCallback canceled_callback_;
  WebContents* web_contents_;
};

void WriteToFile(const base::FilePath& path, const std::string& content) {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::FILE));
  DCHECK(!path.empty());

  file_util::WriteFile(path, content.c_str(), content.length());
}

void AppendToFile(const base::FilePath& path, const std::string& content) {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::FILE));
  DCHECK(!path.empty());

  file_util::AppendToFile(path, content.c_str(), content.length());
}

fileapi::IsolatedContext* isolated_context() {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::UI));
  fileapi::IsolatedContext* isolated_context =
      fileapi::IsolatedContext::GetInstance();
  DCHECK(isolated_context);
  return isolated_context;
}

std::string RegisterFileSystem(WebContents* web_contents,
                               const base::FilePath& path,
                               std::string* registered_name) {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::UI));
  CHECK(web_contents->GetURL().SchemeIs(chrome::kChromeDevToolsScheme));
  std::string file_system_id = isolated_context()->RegisterFileSystemForPath(
      fileapi::kFileSystemTypeNativeLocal, path, registered_name);

  content::ChildProcessSecurityPolicy* policy =
      content::ChildProcessSecurityPolicy::GetInstance();
  RenderViewHost* render_view_host = web_contents->GetRenderViewHost();
  int renderer_id = render_view_host->GetProcess()->GetID();
  policy->GrantReadFileSystem(renderer_id, file_system_id);
  policy->GrantWriteFileSystem(renderer_id, file_system_id);
  policy->GrantCreateFileForFileSystem(renderer_id, file_system_id);

  // We only need file level access for reading FileEntries. Saving FileEntries
  // just needs the file system to have read/write access, which is granted
  // above if required.
  if (!policy->CanReadFile(renderer_id, path))
    policy->GrantReadFile(renderer_id, path);

  return file_system_id;
}

DevToolsFileHelper::FileSystem CreateFileSystemStruct(
    WebContents* web_contents,
    const std::string& file_system_id,
    const std::string& registered_name,
    const std::string& file_system_path) {
  const GURL origin = web_contents->GetURL().GetOrigin();
  std::string file_system_name = fileapi::GetIsolatedFileSystemName(
      origin,
      file_system_id);
  std::string root_url = fileapi::GetIsolatedFileSystemRootURIString(
      origin,
      file_system_id,
      registered_name);
  return DevToolsFileHelper::FileSystem(file_system_name,
                                        root_url,
                                        file_system_path);
}

set<std::string> GetAddedFileSystemPaths(Profile* profile) {
  const DictionaryValue* file_systems_paths_value =
      profile->GetPrefs()->GetDictionary(prefs::kDevToolsFileSystemPaths);
  set<std::string> result;
  for (DictionaryValue::Iterator it(*file_systems_paths_value); !it.IsAtEnd();
       it.Advance()) {
    result.insert(it.key());
  }
  return result;
}

}  // namespace

DevToolsFileHelper::FileSystem::FileSystem() {
}

DevToolsFileHelper::FileSystem::FileSystem(const std::string& file_system_name,
                                           const std::string& root_url,
                                           const std::string& file_system_path)
    : file_system_name(file_system_name),
      root_url(root_url),
      file_system_path(file_system_path) {
}

DevToolsFileHelper::DevToolsFileHelper(WebContents* web_contents,
                                       Profile* profile)
    : web_contents_(web_contents),
      profile_(profile),
      weak_factory_(this) {
}

DevToolsFileHelper::~DevToolsFileHelper() {
}

void DevToolsFileHelper::Save(const std::string& url,
                              const std::string& content,
                              bool save_as,
                              const SaveCallback& callback) {
  PathsMap::iterator it = saved_files_.find(url);
  if (it != saved_files_.end() && !save_as) {
    SaveAsFileSelected(url, content, callback, it->second);
    return;
  }

  const DictionaryValue* file_map =
      profile_->GetPrefs()->GetDictionary(prefs::kDevToolsEditedFiles);
  base::FilePath initial_path;

  const Value* path_value;
  if (file_map->Get(base::MD5String(url), &path_value))
    base::GetValueAsFilePath(*path_value, &initial_path);

  if (initial_path.empty()) {
    GURL gurl(url);
    std::string suggested_file_name = gurl.is_valid() ?
        gurl.ExtractFileName() : url;

    if (suggested_file_name.length() > 64)
      suggested_file_name = suggested_file_name.substr(0, 64);

    if (!g_last_save_path.Pointer()->empty()) {
      initial_path = g_last_save_path.Pointer()->DirName().AppendASCII(
          suggested_file_name);
    } else {
      base::FilePath download_path = DownloadPrefs::FromDownloadManager(
          BrowserContext::GetDownloadManager(profile_))->DownloadPath();
      initial_path = download_path.AppendASCII(suggested_file_name);
    }
  }

  scoped_refptr<SelectFileDialog> select_file_dialog = new SelectFileDialog(
      Bind(&DevToolsFileHelper::SaveAsFileSelected,
           weak_factory_.GetWeakPtr(),
           url,
           content,
           callback),
      Bind(&DevToolsFileHelper::SaveAsFileSelectionCanceled,
           weak_factory_.GetWeakPtr()),
      web_contents_);
  select_file_dialog->Show(ui::SelectFileDialog::SELECT_SAVEAS_FILE,
                           initial_path);
}

void DevToolsFileHelper::Append(const std::string& url,
                                const std::string& content,
                                const AppendCallback& callback) {
  PathsMap::iterator it = saved_files_.find(url);
  if (it == saved_files_.end())
    return;
  callback.Run();
  BrowserThread::PostTask(BrowserThread::FILE, FROM_HERE,
                          Bind(&AppendToFile, it->second, content));
}

void DevToolsFileHelper::SaveAsFileSelected(const std::string& url,
                                            const std::string& content,
                                            const SaveCallback& callback,
                                            const base::FilePath& path) {
  *g_last_save_path.Pointer() = path;
  saved_files_[url] = path;

  DictionaryPrefUpdate update(profile_->GetPrefs(),
                              prefs::kDevToolsEditedFiles);
  DictionaryValue* files_map = update.Get();
  files_map->SetWithoutPathExpansion(base::MD5String(url),
                                     base::CreateFilePathValue(path));
  callback.Run();
  BrowserThread::PostTask(BrowserThread::FILE, FROM_HERE,
                          Bind(&WriteToFile, path, content));
}

void DevToolsFileHelper::SaveAsFileSelectionCanceled() {
}

void DevToolsFileHelper::AddFileSystem(
    const AddFileSystemCallback& callback,
    const ShowInfoBarCallback& show_info_bar_callback) {
  scoped_refptr<SelectFileDialog> select_file_dialog = new SelectFileDialog(
      Bind(&DevToolsFileHelper::InnerAddFileSystem,
           weak_factory_.GetWeakPtr(),
           callback,
           show_info_bar_callback),
      Bind(callback, FileSystem()),
      web_contents_);
  select_file_dialog->Show(ui::SelectFileDialog::SELECT_FOLDER,
                           base::FilePath());
}

void DevToolsFileHelper::InnerAddFileSystem(
    const AddFileSystemCallback& callback,
    const ShowInfoBarCallback& show_info_bar_callback,
    const base::FilePath& path) {
  std::string file_system_path = path.AsUTF8Unsafe();

  const DictionaryValue* file_systems_paths_value =
      profile_->GetPrefs()->GetDictionary(prefs::kDevToolsFileSystemPaths);
  if (file_systems_paths_value->HasKey(file_system_path)) {
    callback.Run(FileSystem());
    return;
  }

  std::string path_display_name = path.AsEndingWithSeparator().AsUTF8Unsafe();
  string16 message = l10n_util::GetStringFUTF16(
      IDS_DEV_TOOLS_CONFIRM_ADD_FILE_SYSTEM_MESSAGE,
      UTF8ToUTF16(path_display_name));
  show_info_bar_callback.Run(
      message,
      Bind(&DevToolsFileHelper::AddUserConfirmedFileSystem,
           weak_factory_.GetWeakPtr(),
           callback, path));
}

void DevToolsFileHelper::AddUserConfirmedFileSystem(
    const AddFileSystemCallback& callback,
    const base::FilePath& path,
    bool allowed) {
  if (!allowed) {
    callback.Run(FileSystem());
    return;
  }
  std::string registered_name;
  std::string file_system_id = RegisterFileSystem(web_contents_,
                                                  path,
                                                  &registered_name);
  std::string file_system_path = path.AsUTF8Unsafe();

  DictionaryPrefUpdate update(profile_->GetPrefs(),
                              prefs::kDevToolsFileSystemPaths);
  DictionaryValue* file_systems_paths_value = update.Get();
  file_systems_paths_value->SetWithoutPathExpansion(file_system_path,
                                                    Value::CreateNullValue());

  FileSystem filesystem = CreateFileSystemStruct(web_contents_,
                                                 file_system_id,
                                                 registered_name,
                                                 file_system_path);
  callback.Run(filesystem);
}

void DevToolsFileHelper::RequestFileSystems(
    const RequestFileSystemsCallback& callback) {
  set<std::string> file_system_paths = GetAddedFileSystemPaths(profile_);
  set<std::string>::const_iterator it = file_system_paths.begin();
  std::vector<FileSystem> file_systems;
  for (; it != file_system_paths.end(); ++it) {
    std::string file_system_path = *it;
    base::FilePath path = base::FilePath::FromUTF8Unsafe(file_system_path);

    std::string registered_name;
    std::string file_system_id = RegisterFileSystem(web_contents_,
                                                    path,
                                                    &registered_name);
    FileSystem filesystem = CreateFileSystemStruct(web_contents_,
                                                   file_system_id,
                                                   registered_name,
                                                   file_system_path);
    file_systems.push_back(filesystem);
  }
  callback.Run(file_systems);
}

void DevToolsFileHelper::RemoveFileSystem(const std::string& file_system_path) {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::UI));
  base::FilePath path = base::FilePath::FromUTF8Unsafe(file_system_path);
  isolated_context()->RevokeFileSystemByPath(path);

  DictionaryPrefUpdate update(profile_->GetPrefs(),
                              prefs::kDevToolsFileSystemPaths);
  DictionaryValue* file_systems_paths_value = update.Get();
  file_systems_paths_value->RemoveWithoutPathExpansion(file_system_path, NULL);
}

bool DevToolsFileHelper::IsFileSystemAdded(
    const std::string& file_system_path) {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::UI));
  set<std::string> file_system_paths = GetAddedFileSystemPaths(profile_);
  return file_system_paths.find(file_system_path) != file_system_paths.end();
}
