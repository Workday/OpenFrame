// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Constants.
/** @const */ var FEEDBACK_LANDING_PAGE =
    'https://www.google.com/support/chrome/go/feedback_confirmation';
/** @const */ var MAX_ATTACH_FILE_SIZE = 3 * 1024 * 1024;

var selectedThumbnailDivId = '';
var selectedThumbnailId = '';
var selectedImageUrl;

var savedThumbnailIds = [];
savedThumbnailIds['current-screenshots'] = '';
savedThumbnailIds['saved-screenshots'] = '';

var categoryTag = '';
var filePath = '';
var forceDisableScreenshots = false;
var traceId = 0;

// Globals to manage reading data from the attach a file option.
var attachFileBinaryData = '';
var lastReader = null;

/**
 * Returns the base filename for a given path. Handles only Unix style paths.
 * @param {string} path The path to return the basename for.
 * @return {string} Basename for the path.
 */
function getBaseName(path) {
  lastSeparator = path.lastIndexOf('/');
  if (lastSeparator == -1)
    return '';
  else
    return path.substr(lastSeparator + 1);
}

/**
 * Selects an image thumbnail in the specified div.
 * @param {string} divId The id of the div to search in.
 * @param {string} thumbnailId The id of the thumbnail to search for.
 */
function selectImage(divId, thumbnailId) {
  var thumbnailDivs = $(divId).children;
  selectedThumbnailDivId = divId;
  if (thumbnailDivs.length == 0) {
    $(divId).hidden = true;
    return;
  }

  for (var i = 0; i < thumbnailDivs.length; i++) {
    thumbnailDivs[i].className = 'image-thumbnail-container';

    // If the the current div matches the thumbnail id provided,
    // or there is no thumbnail id given, and we're at the first thumbnail.
    if (thumbnailDivs[i].id == thumbnailId || (!thumbnailId && !i)) {
      thumbnailDivs[i].classList.add('image-thumbnail-container-selected');
      selectedThumbnailId = thumbnailDivs[i].id;
      savedThumbnailIds[divId] = thumbnailId;
    }
  }
}

/**
 * Adds an image thumbnail to the specified div.
 * @param {string} divId The id of the div to add a screenshot to.
 * @param {string} screenshot The URL of the screenshot being added.
 */
function addScreenshot(divId, screenshot) {
  var thumbnailDiv = document.createElement('div');
  thumbnailDiv.className = 'image-thumbnail-container';

  thumbnailDiv.id = divId + '-thumbnailDiv-' + $(divId).children.length;
  thumbnailDiv.onclick = function() {
    selectImage(divId, thumbnailDiv.id);
  };

  var innerDiv = document.createElement('div');
  innerDiv.className = 'image-thumbnail';

  var thumbnail = document.createElement('img');
  thumbnail.id = thumbnailDiv.id + '-image';
  // We add the ?+timestamp to make sure the image URLs are unique
  // and Chrome does not load the image from cache.
  thumbnail.src = screenshot + '?' + Date.now();
  innerDiv.appendChild(thumbnail);

  thumbnailDiv.appendChild(innerDiv);
  $(divId).appendChild(thumbnailDiv);

  if (!selectedThumbnailId)
    selectImage(divId, thumbnailDiv.id);
}

/**
 * Enables screenshots.
 */
function enableScreenshots() {
  if (forceDisableScreenshots)
    return;
  $('screenshot-row').hidden = false;
}

/**
 * Reads the selected file when the user selects a file.
 * @param {event} evtFileSelected The on changed event for the file input box.
 */
function onFileSelected(evtFileSelected) {
  var file = evtFileSelected.target.files[0];
  if (!file) {
    // User canceled file selection.
    $('attach-file-checkbox').checked = false;
    attachFileBinaryData = null;
    return;
  }

  if (file.size > MAX_ATTACH_FILE_SIZE) {
    $('attach-error').hidden = false;

    // Clear our selected file.
    $('attach-file').value = '';
    attachFileBinaryData = null;
    $('attach-file-checkbox').checked = false;

    return;
  }

  $('attach-error').hidden = true;

  // Abort an existing file read operation if one exists.
  if (lastReader) {
    lastReader.abort();
    lastReader = null;
  }

  var reader = new FileReader();
  reader.onloadend = function(evtLoadEnd) {
    if (evtLoadEnd.target.readyState == FileReader.DONE) {
      attachFileBinaryData = evtLoadEnd.target.result;
      lastReader = null;
      // Check the checkbox so we do send this file. Users can uncheck the
      // box if they don't want to send the file.
      $('attach-file-checkbox').checked = true;
      $('reading-file').hidden = true;
      $('send-report-button').disabled = false;
    }
  };

  lastReader = reader;
  reader.readAsBinaryString(file);
  $('reading-file').hidden = false;
  $('send-report-button').disabled = true;
}

/**
 * Sends the report; after the report is sent, we need to be redirected to
 * the landing page, but we shouldn't be able to navigate back, hence
 * we open the landing page in a new tab and sendReport closes this tab.
 * @return {boolean} True if the report was sent.
 */
function sendReport() {
  if ($('description-text').value.length == 0) {
    alert(loadTimeData.getString('no-description'));
    return false;
  }

  var imagePath = '';
  if ($('screenshot-checkbox').checked && selectedThumbnailId)
    imagePath = $(selectedThumbnailId + '-image').src;
  var pageUrl = $('page-url-text').value;
  if (!$('page-url-checkbox').checked)
    pageUrl = '';
  var userEmail = $('user-email-text').value;
  if (!$('user-email-checkbox').checked)
    userEmail = '';

  var reportArray = [pageUrl,
                     categoryTag,
                     $('description-text').value,
                     userEmail,
                     imagePath];

  // Add chromeos data if it exists.
  if ($('sys-info-checkbox')) {
    reportArray = reportArray.concat([String($('sys-info-checkbox').checked)]);
    if (!$('performance-info-checkbox').checked) {
      traceId = 0;
    }
    reportArray = reportArray.concat([String(traceId)]);
  }

  if ($('attach-file-checkbox') &&
      $('attach-file-checkbox').checked) {
    if (attachFileBinaryData) {
      reportArray = reportArray.concat(
          [$('attach-file').files[0].name, btoa(attachFileBinaryData)]);
    }
  } else if ($('attach-file-custom-checkbox') &&
             $('attach-file-custom-checkbox').checked) {
    if (filePath)
      reportArray = reportArray.concat([filePath, '']);
  }

  // open the landing page in a new tab, sendReport will close this one.
  window.open(FEEDBACK_LANDING_PAGE, '_blank');
  chrome.send('sendReport', reportArray);
  return true;
}

/**
 * Click listener for the cancel button.
 * @param {Event} e The click event being handled.
 */
function cancel(e) {
  chrome.send('cancel');
  e.preventDefault();
}

/**
 * Select the current screenshots div, restoring the image that was
 * selected when we had this div open previously.
 */
function currentSelected() {
  // TODO(rkc): Change this to use a class instead.
  $('current-screenshots').hidden = false;
  if ($('saved-screenshots'))
    $('saved-screenshots').hidden = true;

  if (selectedThumbnailDivId != 'current-screenshots')
    selectImage('current-screenshots',
                savedThumbnailIds['current-screenshots']);
}

/**
 * Select the saved screenshots div, restoring the image that was
 * selected when we had this div open previously.
 */
function savedSelected() {
  if ($('saved-screenshots').childElementCount == 0) {
    // setupSavedScreenshots will take care of changing visibility
    chrome.send('refreshSavedScreenshots');
  } else {
    $('current-screenshots').hidden = true;
    $('saved-screenshots').hidden = false;
    if (selectedThumbnailDivId != 'saved-screenshots')
      selectImage('saved-screenshots', savedThumbnailIds['saved-screenshots']);
  }
}

/**
 * Change the type of screenshot we're showing to the user from
 * the current screenshot to saved screenshots
 */
function changeToSaved() {
  $('screenshot-label-current').hidden = true;
  $('screenshot-label-saved').hidden = false;

  // Change the link to say "go to original"
  $('screenshot-link-tosaved').hidden = true;
  $('screenshot-link-tocurrent').hidden = false;

  savedSelected();
}

/**
 * Change the type of screenshot we're showing to the user from
 * the saved screenshots to the current screenshots
 */
function changeToCurrent() {
  $('screenshot-label-current').hidden = false;
  $('screenshot-label-saved').hidden = true;

  // Change the link to say "go to saved"
  $('screenshot-link-tosaved').hidden = false;
  $('screenshot-link-tocurrent').hidden = true;

  currentSelected();
}

<if expr="pp_ifdef('chromeos')">
/**
 * Update the page when performance feedback state is changed.
 */
function performanceFeedbackChanged() {
  if ($('performance-info-checkbox').checked) {
    $('attach-file-checkbox').disabled = true;
    $('attach-file-checkbox').checked = false;

    $('screenshot-checkbox').disabled = true;
    $('screenshot-checkbox').checked = false;
  } else {
    $('attach-file-checkbox').disabled = false;
    $('screenshot-checkbox').disabled = false;
  }
}
</if>

///////////////////////////////////////////////////////////////////////////////
// Document Functions:
/**
 * Window onload handler, sets up the page.
 */
function load() {
  cr.ui.FocusOutlineManager.forDocument(document);
  if ($('attach-file'))
    $('attach-file').addEventListener('change', onFileSelected);

  if ($('sysinfo-url')) {
    $('sysinfo-url').onclick = function(event) {
      chrome.send('openSystemTab');
    };
  }

<if expr="pp_ifdef('chromeos')">
  $('screenshot-link-tosaved').onclick = changeToSaved;
  $('screenshot-link-tocurrent').onclick = changeToCurrent;

  $('performance-info-checkbox').addEventListener(
      'change', performanceFeedbackChanged);
</if>
  $('send-report-button').onclick = sendReport;
  $('cancel-button').onclick = cancel;

  // Set default values for the possible parameters, and then parse the actual
  // values from the URL href.
  var parameters = {
    'description': '',
    'categoryTag': '',
    'customPageUrl': '',
    'filePath': '',
    'traceId': 0,
  };

  var loc = window.location;
  // Split the query string into an array of parameters.
  var query = loc.search.substr(1).split('&');
  // If we have a query in the hash.
  if (loc.hash.indexOf('?') >= 0) {
    // Remove the hash and split this query into parameters too.
    query = query.concat(loc.hash.substr(loc.hash.indexOf('?') + 1).split('&'));
  }
  for (var i = 0; i < query.length; i++) {
    // Decode and store each parameter value.
    parameter = query[i].split('=');
    parameters[parameter[0]] = decodeURIComponent(parameter[1]);
  }

  // Set the initial description text.
  $('description-text').textContent = parameters['description'];
  // If a page url is spcified in the parameters, override the default page url.
  if (parameters['customPageUrl'] != '') {
    $('page-url-text').value = parameters['customPageUrl'];
    // and disable the page image, since it doesn't make sense on a custom url.
    $('screenshot-checkbox').checked = false;
    forceDisableScreenshots = true;
  }

  // Pick up the category tag (for most cases this will be an empty string)
  categoryTag = parameters['categoryTag'];

  // Pick up the file path for the attached file (only user for this at the
  // moment is the quick office extension).
  filePath = parameters['filePath'];

  if (filePath != '') {
    var baseName = getBaseName(filePath);
    if (baseName) {
      // Don't let the user choose another file, we were invoked by an
      // extension already providing us the file, this report should only
      // attach that file, or no file at all.
      $('attach-file-container').hidden = true;

      // Set our filename and unhide the "Attach this file" span.
      $('attach-file-custom-name').textContent = baseName;
      $('attach-file-custom-container').hidden = false;
      // No screenshots if we're being invoked by an extension - screenshot was
      // never taken.
      $('screenshot-checkbox').checked = false;
      forceDisableScreenshots = true;
    } else {
      filePath = '';
    }
  }

  traceId = parameters['traceId'];
  if (traceId != 0 && ($('performance-info-area'))) {
    $('performance-info-area').hidden = false;
    $('performance-info-checkbox').checked = true;
    performanceFeedbackChanged();
  }

  chrome.send('getDialogDefaults');
  chrome.send('refreshCurrentScreenshot');
}

function setupCurrentScreenshot(screenshot) {
  addScreenshot('current-screenshots', screenshot);
}

function setupSavedScreenshots(screenshots) {
  if (screenshots.length == 0) {
    $('saved-screenshots').textContent =
        loadTimeData.getString('no-saved-screenshots');

    // Make sure we make the display the message.
    $('current-screenshots').hidden = true;
    $('saved-screenshots').hidden = false;

    // In case the user tries to send now; fail safe, do not send a screenshot
    // at all versus sending the current screenshot.
    selectedThumbnailDivId = '';
    selectedThumbnailId = '';
  } else {
    $('saved-screenshots').textContent = '';
    for (i = 0; i < screenshots.length; ++i)
      addScreenshot('saved-screenshots', screenshots[i]);

    // Now that we have our screenshots, try selecting the saved screenshots
    // again.
    savedSelected();
  }
}

function setupDialogDefaults(defaults) {
  // Current url.
  if ($('page-url-text').value == '')
    $('page-url-text').value = defaults.currentUrl;
  if (defaults.currentUrl == '')
    $('page-url-checkbox').checked = false;
  // User e-mail.
  $('user-email-text').value = defaults.userEmail;
  $('user-email-checkbox').checked = defaults.emailCheckboxDefault;

  document.documentElement.classList.toggle('launcher-layout',
                                            defaults.launcherFeedback);

  if (!defaults.disableScreenshots)
    enableScreenshots();

  if (defaults.useSaved) {
    $('screenshot-link-tosaved').hidden = false;
  }
}

window.addEventListener('DOMContentLoaded', load);
