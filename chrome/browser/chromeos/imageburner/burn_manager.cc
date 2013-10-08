// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/chromeos/imageburner/burn_manager.h"

#include "base/bind.h"
#include "base/file_util.h"
#include "base/strings/string_util.h"
#include "base/threading/worker_pool.h"
#include "chrome/browser/chromeos/system/statistics_provider.h"
#include "chromeos/dbus/dbus_thread_manager.h"
#include "chromeos/dbus/image_burner_client.h"
#include "chromeos/network/network_state.h"
#include "chromeos/network/network_state_handler.h"
#include "content/public/browser/browser_thread.h"
#include "grit/generated_resources.h"
#include "net/url_request/url_fetcher.h"
#include "net/url_request/url_request_context_getter.h"
#include "net/url_request/url_request_status.h"
#include "third_party/zlib/google/zip.h"

using content::BrowserThread;

namespace chromeos {
namespace imageburner {

namespace {

const char kConfigFileUrl[] =
    "https://dl.google.com/dl/edgedl/chromeos/recovery/recovery.conf";
const char kTempImageFolderName[] = "chromeos_image";

const char kImageZipFileName[] = "chromeos_image.bin.zip";

const int64 kBytesImageDownloadProgressReportInterval = 10240;

BurnManager* g_burn_manager = NULL;

// Cretes a directory and calls |callback| with the result on UI thread.
void CreateDirectory(const base::FilePath& path,
                     base::Callback<void(bool success)> callback) {
  const bool success = file_util::CreateDirectory(path);
  BrowserThread::PostTask(BrowserThread::UI, FROM_HERE,
                          base::Bind(callback, success));
}

// Unzips |source_zip_file| and sets the filename of the unzipped image to
// |source_image_file|.
void UnzipImage(const base::FilePath& source_zip_file,
                const std::string& image_name,
                scoped_refptr<base::RefCountedString> source_image_file) {
  if (zip::Unzip(source_zip_file, source_zip_file.DirName())) {
    source_image_file->data() =
        source_zip_file.DirName().Append(image_name).value();
  }
}

}  // namespace

const char kName[] = "name";
const char kHwid[] = "hwid";
const char kFileName[] = "file";
const char kUrl[] = "url";

////////////////////////////////////////////////////////////////////////////////
//
// ConfigFile
//
////////////////////////////////////////////////////////////////////////////////
ConfigFile::ConfigFile() {
}

ConfigFile::ConfigFile(const std::string& file_content) {
  reset(file_content);
}

ConfigFile::~ConfigFile() {
}

void ConfigFile::reset(const std::string& file_content) {
  clear();

  std::vector<std::string> lines;
  Tokenize(file_content, "\n", &lines);

  std::vector<std::string> key_value_pair;
  for (size_t i = 0; i < lines.size(); ++i) {
    if (lines[i].empty())
      continue;

    key_value_pair.clear();
    Tokenize(lines[i], "=", &key_value_pair);
    // Skip lines that don't contain key-value pair and lines without a key.
    if (key_value_pair.size() != 2 || key_value_pair[0].empty())
      continue;

    ProcessLine(key_value_pair);
  }

  // Make sure last block has at least one hwid associated with it.
  DeleteLastBlockIfHasNoHwid();
}

void ConfigFile::clear() {
  config_struct_.clear();
}

const std::string& ConfigFile::GetProperty(
    const std::string& property_name,
    const std::string& hwid) const {
  // We search for block that has desired hwid property, and if we find it, we
  // return its property_name property.
  for (BlockList::const_iterator block_it = config_struct_.begin();
       block_it != config_struct_.end();
       ++block_it) {
    if (block_it->hwids.find(hwid) != block_it->hwids.end()) {
      PropertyMap::const_iterator property =
          block_it->properties.find(property_name);
      if (property != block_it->properties.end()) {
        return property->second;
      } else {
        return EmptyString();
      }
    }
  }

  return EmptyString();
}

// Check if last block has a hwid associated with it, and erase it if it
// doesn't,
void ConfigFile::DeleteLastBlockIfHasNoHwid() {
  if (!config_struct_.empty() && config_struct_.back().hwids.empty()) {
    config_struct_.pop_back();
  }
}

void ConfigFile::ProcessLine(const std::vector<std::string>& line) {
  // If line contains name key, new image block is starting, so we have to add
  // new entry to our data structure.
  if (line[0] == kName) {
    // If there was no hardware class defined for previous block, we can
    // disregard is since we won't be abble to access any of its properties
    // anyway. This should not happen, but let's be defensive.
    DeleteLastBlockIfHasNoHwid();
    config_struct_.resize(config_struct_.size() + 1);
  }

  // If we still haven't added any blocks to data struct, we disregard this
  // line. Again, this should never happen.
  if (config_struct_.empty())
    return;

  ConfigFileBlock& last_block = config_struct_.back();

  if (line[0] == kHwid) {
    // Check if line contains hwid property. If so, add it to set of hwids
    // associated with current block.
    last_block.hwids.insert(line[1]);
  } else {
    // Add new block property.
    last_block.properties.insert(std::make_pair(line[0], line[1]));
  }
}

ConfigFile::ConfigFileBlock::ConfigFileBlock() {
}

ConfigFile::ConfigFileBlock::~ConfigFileBlock() {
}

////////////////////////////////////////////////////////////////////////////////
//
// StateMachine
//
////////////////////////////////////////////////////////////////////////////////
StateMachine::StateMachine()
    : download_started_(false),
      download_finished_(false),
      state_(INITIAL) {
}

StateMachine::~StateMachine() {
}

void StateMachine::OnError(int error_message_id) {
  if (state_ == INITIAL)
    return;
  if (!download_finished_)
    download_started_ = false;

  state_ = INITIAL;
  FOR_EACH_OBSERVER(Observer, observers_, OnError(error_message_id));
}

void StateMachine::OnSuccess() {
  if (state_ == INITIAL)
    return;
  state_ = INITIAL;
  OnStateChanged();
}

////////////////////////////////////////////////////////////////////////////////
//
// BurnManager
//
////////////////////////////////////////////////////////////////////////////////

BurnManager::BurnManager(
    const base::FilePath& downloads_directory,
    scoped_refptr<net::URLRequestContextGetter> context_getter)
    : device_handler_(disks::DiskMountManager::GetInstance()),
      image_dir_created_(false),
      unzipping_(false),
      cancelled_(false),
      burning_(false),
      block_burn_signals_(false),
      image_dir_(downloads_directory.Append(kTempImageFolderName)),
      config_file_url_(kConfigFileUrl),
      config_file_fetched_(false),
      state_machine_(new StateMachine()),
      url_request_context_getter_(context_getter),
      bytes_image_download_progress_last_reported_(0),
      weak_ptr_factory_(this) {
  NetworkHandler::Get()->network_state_handler()->AddObserver(
      this, FROM_HERE);
  base::WeakPtr<BurnManager> weak_ptr(weak_ptr_factory_.GetWeakPtr());
  device_handler_.SetCallbacks(
      base::Bind(&BurnManager::NotifyDeviceAdded, weak_ptr),
      base::Bind(&BurnManager::NotifyDeviceRemoved, weak_ptr));
  DBusThreadManager::Get()->GetImageBurnerClient()->SetEventHandlers(
      base::Bind(&BurnManager::OnBurnFinished,
                 weak_ptr_factory_.GetWeakPtr()),
      base::Bind(&BurnManager::OnBurnProgressUpdate,
                 weak_ptr_factory_.GetWeakPtr()));
}

BurnManager::~BurnManager() {
  if (image_dir_created_) {
    base::DeleteFile(image_dir_, true);
  }
  if (NetworkHandler::IsInitialized()) {
    NetworkHandler::Get()->network_state_handler()->RemoveObserver(
        this, FROM_HERE);
  }
  DBusThreadManager::Get()->GetImageBurnerClient()->ResetEventHandlers();
}

// static
void BurnManager::Initialize(
    const base::FilePath& downloads_directory,
    scoped_refptr<net::URLRequestContextGetter> context_getter) {
  if (g_burn_manager) {
    LOG(WARNING) << "BurnManager was already initialized";
    return;
  }
  g_burn_manager = new BurnManager(downloads_directory, context_getter);
  VLOG(1) << "BurnManager initialized";
}

// static
void BurnManager::Shutdown() {
  if (!g_burn_manager) {
    LOG(WARNING) << "BurnManager::Shutdown() called with NULL manager";
    return;
  }
  delete g_burn_manager;
  g_burn_manager = NULL;
  VLOG(1) << "BurnManager Shutdown completed";
}

// static
BurnManager* BurnManager::GetInstance() {
  return g_burn_manager;
}

void BurnManager::AddObserver(Observer* observer) {
  observers_.AddObserver(observer);
}

void BurnManager::RemoveObserver(Observer* observer) {
  observers_.RemoveObserver(observer);
}

std::vector<disks::DiskMountManager::Disk> BurnManager::GetBurnableDevices() {
  return device_handler_.GetBurnableDevices();
}

void BurnManager::Cancel() {
  OnError(IDS_IMAGEBURN_USER_ERROR);
}

void BurnManager::OnError(int message_id) {
  // If we are in intial state, error has already been dispached.
  if (state_machine_->state() == StateMachine::INITIAL) {
    return;
  }

  // Remember burner state, since it will be reset after OnError call.
  StateMachine::State state = state_machine_->state();

  // Dispach error. All hadlers' OnError event will be called before returning
  // from this. This includes us, too.
  state_machine_->OnError(message_id);

  // Cancel and clean up the current task.
  // Note: the cancellation of this class looks not handled correctly.
  // In particular, there seems no clean-up code for creating a temporary
  // directory, or fetching config files. Also, there seems an issue
  // about the cancellation of burning.
  // TODO(hidehiko): Fix the issue.
  if (state  == StateMachine::DOWNLOADING) {
    CancelImageFetch();
  } else if (state == StateMachine::BURNING) {
    // Burn library doesn't send cancelled signal upon CancelBurnImage
    // invokation.
    CancelBurnImage();
  }
  ResetTargetPaths();
}

void BurnManager::CreateImageDir() {
  if (!image_dir_created_) {
    BrowserThread::PostBlockingPoolTask(
        FROM_HERE,
        base::Bind(CreateDirectory,
                   image_dir_,
                   base::Bind(&BurnManager::OnImageDirCreated,
                              weak_ptr_factory_.GetWeakPtr())));
  } else {
    const bool success = true;
    OnImageDirCreated(success);
  }
}

void BurnManager::OnImageDirCreated(bool success) {
  if (!success) {
    // Failed to create the directory. Finish the burning process
    // with failure state.
    OnError(IDS_IMAGEBURN_DOWNLOAD_ERROR);
    return;
  }

  image_dir_created_ = true;
  zip_image_file_path_ = image_dir_.Append(kImageZipFileName);
  FetchConfigFile();
}

base::FilePath BurnManager::GetImageDir() {
  if (!image_dir_created_)
    return base::FilePath();
  return image_dir_;
}

void BurnManager::FetchConfigFile() {
  if (config_file_fetched_) {
    // The config file is already fetched. So start to fetch the image.
    FetchImage();
    return;
  }

  if (config_fetcher_.get())
    return;

  config_fetcher_.reset(net::URLFetcher::Create(
      config_file_url_, net::URLFetcher::GET, this));
  config_fetcher_->SetRequestContext(url_request_context_getter_.get());
  config_fetcher_->Start();
}

void BurnManager::FetchImage() {
  if (state_machine_->download_finished()) {
    DoBurn();
    return;
  }

  if (state_machine_->download_started()) {
    // The image downloading is already started. Do nothing.
    return;
  }

  tick_image_download_start_ = base::TimeTicks::Now();
  bytes_image_download_progress_last_reported_ = 0;
  image_fetcher_.reset(net::URLFetcher::Create(image_download_url_,
                                               net::URLFetcher::GET,
                                               this));
  image_fetcher_->SetRequestContext(url_request_context_getter_.get());
  image_fetcher_->SaveResponseToFileAtPath(
      zip_image_file_path_,
      BrowserThread::GetMessageLoopProxyForThread(BrowserThread::FILE));
  image_fetcher_->Start();

  state_machine_->OnDownloadStarted();
}

void BurnManager::CancelImageFetch() {
  image_fetcher_.reset();
}

void BurnManager::DoBurn() {
  if (state_machine_->state() == StateMachine::BURNING)
    return;

  if (unzipping_) {
    // We have unzip in progress, maybe it was "cancelled" before and did not
    // finish yet. In that case, let's pretend cancel did not happen.
    cancelled_ = false;
    UpdateBurnStatus(UNZIP_STARTED, ImageBurnStatus());
    return;
  }

  source_image_path_.clear();

  unzipping_ = true;
  cancelled_ = false;
  UpdateBurnStatus(UNZIP_STARTED, ImageBurnStatus());

  const bool task_is_slow = true;
  scoped_refptr<base::RefCountedString> result(new base::RefCountedString);
  base::WorkerPool::PostTaskAndReply(
      FROM_HERE,
      base::Bind(UnzipImage, zip_image_file_path_, image_file_name_, result),
      base::Bind(&BurnManager::OnImageUnzipped,
                 weak_ptr_factory_.GetWeakPtr(),
                 result),
      task_is_slow);
  state_machine_->OnBurnStarted();
}

void BurnManager::CancelBurnImage() {
  // At the moment, we cannot really stop uzipping or burning. Instead we
  // prevent events from being sent to listeners.
  if (burning_)
    block_burn_signals_ = true;
  cancelled_ = true;
}

void BurnManager::OnURLFetchComplete(const net::URLFetcher* source) {
  // TODO(hidehiko): Split the handler implementation into two, for
  // the config file fetcher and the image file fetcher.
  const bool success =
      source->GetStatus().status() == net::URLRequestStatus::SUCCESS;

  if (source == config_fetcher_.get()) {
    // Handler for the config file fetcher.
    std::string data;
    if (success)
      config_fetcher_->GetResponseAsString(&data);
    config_fetcher_.reset();
    ConfigFileFetched(success, data);
    return;
  }

  if (source == image_fetcher_.get()) {
    // Handler for the image file fetcher.
    state_machine_->OnDownloadFinished();
    if (!success) {
      OnError(IDS_IMAGEBURN_DOWNLOAD_ERROR);
      return;
    }
    DoBurn();
    return;
  }

  NOTREACHED();
}

void BurnManager::OnURLFetchDownloadProgress(const net::URLFetcher* source,
                                             int64 current,
                                             int64 total) {
  if (source == image_fetcher_.get()) {
    if (current >= bytes_image_download_progress_last_reported_ +
        kBytesImageDownloadProgressReportInterval) {
      bytes_image_download_progress_last_reported_ = current;
      base::TimeDelta estimated_remaining_time;
      if (current > 0) {
        // Extrapolate from the elapsed time.
        const base::TimeDelta elapsed_time =
            base::TimeTicks::Now() - tick_image_download_start_;
        estimated_remaining_time = elapsed_time * (total - current) / current;
      }

      // TODO(hidehiko): We should be able to clean the state check here.
      if (state_machine_->state() == StateMachine::DOWNLOADING) {
        FOR_EACH_OBSERVER(
            Observer, observers_,
            OnProgressWithRemainingTime(
                DOWNLOADING, current, total, estimated_remaining_time));
      }
    }
  }
}

void BurnManager::DefaultNetworkChanged(const NetworkState* network) {
  // TODO(hidehiko): Split this into a class to write tests.
  if (state_machine_->state() == StateMachine::INITIAL && network)
    FOR_EACH_OBSERVER(Observer, observers_, OnNetworkDetected());

  if (state_machine_->state() == StateMachine::DOWNLOADING && !network)
    OnError(IDS_IMAGEBURN_NETWORK_ERROR);
}

void BurnManager::UpdateBurnStatus(BurnEvent event,
                                   const ImageBurnStatus& status) {
  if (cancelled_)
    return;

  if (event == BURN_FAIL || event == BURN_SUCCESS) {
    burning_ = false;
    if (block_burn_signals_) {
      block_burn_signals_ = false;
      return;
    }
  }

  if (block_burn_signals_ && event == BURN_UPDATE)
    return;

  // Notify observers.
  switch (event) {
    case BURN_SUCCESS:
      // The burning task is successfully done.
      // Update the state.
      ResetTargetPaths();
      state_machine_->OnSuccess();
      FOR_EACH_OBSERVER(Observer, observers_, OnSuccess());
      break;
    case BURN_FAIL:
      OnError(IDS_IMAGEBURN_BURN_ERROR);
      break;
    case BURN_UPDATE:
      FOR_EACH_OBSERVER(
          Observer, observers_,
          OnProgress(BURNING, status.amount_burnt, status.total_size));
      break;
    case(UNZIP_STARTED):
      FOR_EACH_OBSERVER(Observer, observers_, OnProgress(UNZIPPING, 0, 0));
      break;
    case UNZIP_FAIL:
      OnError(IDS_IMAGEBURN_EXTRACTING_ERROR);
      break;
    case UNZIP_COMPLETE:
      // We ignore this.
      break;
    default:
      NOTREACHED();
      break;
  }
}

void BurnManager::ConfigFileFetched(bool fetched, const std::string& content) {
  if (config_file_fetched_)
    return;

  // Get image file name and image download URL.
  std::string hwid;
  if (fetched && system::StatisticsProvider::GetInstance()->
      GetMachineStatistic(system::kHardwareClass, &hwid)) {
    ConfigFile config_file(content);
    image_file_name_ = config_file.GetProperty(kFileName, hwid);
    image_download_url_ = GURL(config_file.GetProperty(kUrl, hwid));
  }

  // Error check.
  if (fetched && !image_file_name_.empty() && !image_download_url_.is_empty()) {
    config_file_fetched_ = true;
  } else {
    fetched = false;
    image_file_name_.clear();
    image_download_url_ = GURL();
  }

  if (!fetched) {
    OnError(IDS_IMAGEBURN_DOWNLOAD_ERROR);
    return;
  }

  FetchImage();
}

void BurnManager::OnImageUnzipped(
    scoped_refptr<base::RefCountedString> source_image_file) {
  source_image_path_ = base::FilePath(source_image_file->data());

  bool success = !source_image_path_.empty();
  UpdateBurnStatus(success ? UNZIP_COMPLETE : UNZIP_FAIL, ImageBurnStatus());

  unzipping_ = false;
  if (cancelled_) {
    cancelled_ = false;
    return;
  }

  if (!success)
    return;

  burning_ = true;

  chromeos::disks::DiskMountManager::GetInstance()->UnmountDeviceRecursively(
      target_device_path_.value(),
      base::Bind(&BurnManager::OnDevicesUnmounted,
                 weak_ptr_factory_.GetWeakPtr()));
}

void BurnManager::OnDevicesUnmounted(bool success) {
  if (!success) {
    UpdateBurnStatus(BURN_FAIL, ImageBurnStatus(0, 0));
    return;
  }

  DBusThreadManager::Get()->GetImageBurnerClient()->BurnImage(
      source_image_path_.value(),
      target_file_path_.value(),
      base::Bind(&BurnManager::OnBurnImageFail,
                 weak_ptr_factory_.GetWeakPtr()));
}

void BurnManager::OnBurnImageFail() {
  UpdateBurnStatus(BURN_FAIL, ImageBurnStatus(0, 0));
}

void BurnManager::OnBurnFinished(const std::string& target_path,
                                 bool success,
                                 const std::string& error) {
  UpdateBurnStatus(success ? BURN_SUCCESS : BURN_FAIL, ImageBurnStatus(0, 0));
}

void BurnManager::OnBurnProgressUpdate(const std::string& target_path,
                                       int64 amount_burnt,
                                       int64 total_size) {
  UpdateBurnStatus(BURN_UPDATE, ImageBurnStatus(amount_burnt, total_size));
}

void BurnManager::NotifyDeviceAdded(
    const disks::DiskMountManager::Disk& disk) {
  FOR_EACH_OBSERVER(Observer, observers_, OnDeviceAdded(disk));
}

void BurnManager::NotifyDeviceRemoved(
    const disks::DiskMountManager::Disk& disk) {
  FOR_EACH_OBSERVER(Observer, observers_, OnDeviceRemoved(disk));

  if (target_device_path_.value() == disk.device_path()) {
    // The device is removed during the burning process.
    // Note: in theory, this is not a part of notification, but cancelling
    // the running burning task. However, there is no good place to be in the
    // current code.
    // TODO(hidehiko): Clean this up after refactoring.
    OnError(IDS_IMAGEBURN_DEVICE_NOT_FOUND_ERROR);
  }
}

}  // namespace imageburner
}  // namespace chromeos
