// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "base/bind_helpers.h"
#include "chrome/browser/extensions/extension_browsertest.h"
#include "chrome/browser/extensions/extension_test_message_listener.h"
#include "chrome/common/extensions/background_info.h"
#include "chromeos/dbus/dbus_thread_manager.h"
#include "chromeos/dbus/ibus/mock_ibus_client.h"
#include "chromeos/dbus/ibus/mock_ibus_engine_factory_service.h"
#include "chromeos/dbus/ibus/mock_ibus_engine_service.h"
#include "chromeos/dbus/mock_dbus_thread_manager_without_gmock.h"
#include "chromeos/ime/input_method_descriptor.h"
#include "chromeos/ime/input_method_manager.h"
#include "content/public/test/browser_test_utils.h"
#include "content/public/test/test_utils.h"
#include "dbus/mock_bus.h"

namespace chromeos {
namespace input_method {
namespace {

const char kIdentityIMEID[] =
    "_ext_ime_iafoklpfplgfnoimmaejoeondnjnlcfpIdentityIME";
const char kToUpperIMEID[] =
    "_ext_ime_iafoklpfplgfnoimmaejoeondnjnlcfpToUpperIME";
const char kAPIArgumentIMEID[] =
    "_ext_ime_iafoklpfplgfnoimmaejoeondnjnlcfpAPIArgumentIME";

const uint32 kAltKeyMask = 1 << 3;
const uint32 kCtrlKeyMask = 1 << 2;
const uint32 kShiftKeyMask = 1 << 0;
const uint32 kCapsLockMask = 1 << 1;

// InputMethod extension should work on 1)normal extension, 2)normal extension
// in incognito mode 3)component extension.
enum TestType {
  kTestTypeNormal = 0,
  kTestTypeIncognito = 1,
  kTestTypeComponent = 2,
};

void OnRegisterComponent(const IBusComponent& ibus_component,
                         const IBusClient::RegisterComponentCallback& callback,
                         const IBusClient::ErrorCallback& error_callback) {
  callback.Run();
}

class InputMethodEngineIBusBrowserTest
    : public ExtensionBrowserTest,
      public ::testing::WithParamInterface<TestType> {
 public:
  InputMethodEngineIBusBrowserTest()
      : ExtensionBrowserTest(),
        mock_dbus_thread_manager_(NULL) {}
  virtual ~InputMethodEngineIBusBrowserTest() {}

  virtual void SetUpInProcessBrowserTestFixture() OVERRIDE {
    mock_dbus_thread_manager_ = new MockDBusThreadManagerWithoutGMock();
    DBusThreadManager::InitializeForTesting(mock_dbus_thread_manager_);
    ExtensionBrowserTest::SetUpInProcessBrowserTestFixture();
  }

  virtual void TearDownInProcessBrowserTestFixture() OVERRIDE {
    DBusThreadManager::Shutdown();
    extension_ = NULL;
  }

 protected:
  void LoadTestInputMethod() {
    MockIBusClient* ibus_client = mock_dbus_thread_manager_->mock_ibus_client();
    ibus_client->set_register_component_handler(
        base::Bind(&OnRegisterComponent));

    // This will load "chrome/test/data/extensions/input_ime"
    ExtensionTestMessageListener ime_ready_listener("ReadyToUseImeEvent",
                                                    false);
    extension_ = LoadExtensionWithType("input_ime", GetParam());
    ASSERT_TRUE(extension_);
    ASSERT_TRUE(ime_ready_listener.WaitUntilSatisfied());

    // The reason why not EXPECT_EQ is that extension will be reloaded in the
    // case of incognito mode switching. Thus registeration will be happend
    // multiple times. Calling at least once per engine is sufficient for IBus
    // component. Here, there is two engine, thus expectation is at least 4
    // times.
    EXPECT_LE(3, ibus_client->register_component_call_count());

    // Extension IMEs are not enabled by default.
    std::vector<std::string> extension_ime_ids;
    extension_ime_ids.push_back(kIdentityIMEID);
    extension_ime_ids.push_back(kToUpperIMEID);
    extension_ime_ids.push_back(kAPIArgumentIMEID);
    InputMethodManager::Get()->SetEnabledExtensionImes(&extension_ime_ids);

    InputMethodDescriptors extension_imes;
    InputMethodManager::Get()->GetInputMethodExtensions(&extension_imes);

    // Test IME has two input methods, thus InputMethodManager should have two
    // extension IME.
    // Note: Even extension is loaded by LoadExtensionAsComponent as above, the
    // IME does not managed by ComponentExtensionIMEManager or it's id won't
    // start with __comp__. The component extension IME is whitelisted and
    // managed by ComponentExtensionIMEManager, but its framework is same as
    // normal extension IME.
    EXPECT_EQ(3U, extension_imes.size());
  }

  const extensions::Extension* LoadExtensionWithType(
      const std::string& extension_name, TestType type) {
    switch (type) {
      case kTestTypeNormal:
        return LoadExtension(test_data_dir_.AppendASCII(extension_name));
      case kTestTypeIncognito:
        return LoadExtensionIncognito(
            test_data_dir_.AppendASCII(extension_name));
      case kTestTypeComponent:
        return LoadExtensionAsComponent(
            test_data_dir_.AppendASCII(extension_name));
    }
    NOTREACHED();
    return NULL;
  }

  const extensions::Extension* extension_;
  MockDBusThreadManagerWithoutGMock* mock_dbus_thread_manager_;
  scoped_refptr<dbus::MockBus> mock_bus_;
};

class KeyEventDoneCallback {
 public:
  explicit KeyEventDoneCallback(bool expected_argument)
      : expected_argument_(expected_argument),
        is_called_(false) {}
  ~KeyEventDoneCallback() {}

  void Run(bool consumed) {
    if (consumed == expected_argument_) {
      base::MessageLoop::current()->Quit();
      is_called_ = true;
    }
  }

  void WaitUntilCalled() {
    while (!is_called_)
      content::RunMessageLoop();
  }

 private:
  bool expected_argument_;
  bool is_called_;

  DISALLOW_COPY_AND_ASSIGN(KeyEventDoneCallback);
};

INSTANTIATE_TEST_CASE_P(InputMethodEngineIBusBrowserTest,
                        InputMethodEngineIBusBrowserTest,
                        ::testing::Values(kTestTypeNormal));
INSTANTIATE_TEST_CASE_P(InputMethodEngineIBusIncognitoBrowserTest,
                        InputMethodEngineIBusBrowserTest,
                        ::testing::Values(kTestTypeIncognito));
INSTANTIATE_TEST_CASE_P(InputMethodEngineIBusComponentExtensionBrowserTest,
                        InputMethodEngineIBusBrowserTest,
                        ::testing::Values(kTestTypeComponent));

IN_PROC_BROWSER_TEST_P(InputMethodEngineIBusBrowserTest,
                       BasicScenarioTest) {
  LoadTestInputMethod();

  MockIBusEngineFactoryService* factory_service =
      mock_dbus_thread_manager_->mock_ibus_engine_factory_service();
  factory_service->CallCreateEngine(kIdentityIMEID);

  MockIBusEngineService* engine_service =
      mock_dbus_thread_manager_->mock_ibus_engine_service();
  IBusEngineHandlerInterface* engine_handler = engine_service->GetEngine();
  ASSERT_TRUE(engine_handler);

  // onActivate event should be fired if Enable function is called.
  ExtensionTestMessageListener activated_listener("onActivate", false);
  engine_handler->Enable();
  ASSERT_TRUE(activated_listener.WaitUntilSatisfied());
  ASSERT_TRUE(activated_listener.was_satisfied());

  // onFocus event should be fired if FocusIn function is called.
  ExtensionTestMessageListener focus_listener("onFocus", false);;
  engine_handler->FocusIn();
  ASSERT_TRUE(focus_listener.WaitUntilSatisfied());
  ASSERT_TRUE(focus_listener.was_satisfied());

  // onKeyEvent should be fired if ProcessKeyEvent is called.
  KeyEventDoneCallback callback(false);  // EchoBackIME doesn't consume keys.
  ExtensionTestMessageListener keyevent_listener("onKeyEvent", false);
  engine_handler->ProcessKeyEvent(0x61,  // KeySym for 'a'.
                                  0x26,  // KeyCode for 'a'.
                                  0,  // No modifiers.
                                  base::Bind(&KeyEventDoneCallback::Run,
                                             base::Unretained(&callback)));
  ASSERT_TRUE(keyevent_listener.WaitUntilSatisfied());
  ASSERT_TRUE(keyevent_listener.was_satisfied());
  callback.WaitUntilCalled();

  // onSurroundingTextChange should be fired if SetSurroundingText is called.
  ExtensionTestMessageListener surrounding_text_listener(
      "onSurroundingTextChanged", false);
  engine_handler->SetSurroundingText("text",  // Surrounding text.
                                     0,  // focused position.
                                     1);  // anchor position.
  ASSERT_TRUE(surrounding_text_listener.WaitUntilSatisfied());
  ASSERT_TRUE(surrounding_text_listener.was_satisfied());

  // onMenuItemActivated should be fired if PropertyActivate is called.
  ExtensionTestMessageListener property_listener("onMenuItemActivated", false);
  engine_handler->PropertyActivate("property_name",
                                   ibus::IBUS_PROPERTY_STATE_CHECKED);
  ASSERT_TRUE(property_listener.WaitUntilSatisfied());
  ASSERT_TRUE(property_listener.was_satisfied());

  // onReset should be fired if Reset is called.
  ExtensionTestMessageListener reset_listener("onReset", false);
  engine_handler->Reset();
  ASSERT_TRUE(reset_listener.WaitUntilSatisfied());
  ASSERT_TRUE(reset_listener.was_satisfied());

  // onBlur should be fired if FocusOut is called.
  ExtensionTestMessageListener blur_listener("onBlur", false);
  engine_handler->FocusOut();
  ASSERT_TRUE(blur_listener.WaitUntilSatisfied());
  ASSERT_TRUE(blur_listener.was_satisfied());

  // onDeactivated should be fired if Disable is called.
  ExtensionTestMessageListener disabled_listener("onDeactivated", false);
  engine_handler->Disable();
  ASSERT_TRUE(disabled_listener.WaitUntilSatisfied());
  ASSERT_TRUE(disabled_listener.was_satisfied());
}

IN_PROC_BROWSER_TEST_P(InputMethodEngineIBusBrowserTest,
                       APIArgumentTest) {
  LoadTestInputMethod();

  MockIBusEngineFactoryService* factory_service =
      mock_dbus_thread_manager_->mock_ibus_engine_factory_service();
  factory_service->CallCreateEngine(kAPIArgumentIMEID);

  MockIBusEngineService* engine_service =
      mock_dbus_thread_manager_->mock_ibus_engine_service();
  IBusEngineHandlerInterface* engine_handler = engine_service->GetEngine();
  ASSERT_TRUE(engine_handler);

  extensions::ExtensionHost* host = FindHostWithPath(
      extensions::ExtensionSystem::Get(profile())->process_manager(),
      extensions::BackgroundInfo::GetBackgroundURL(extension_).path(),
      1);

  engine_handler->Enable();
  engine_handler->FocusIn();

  {
    SCOPED_TRACE("KeyDown, Ctrl:No, alt:No, Shift:No, Caps:No");
    KeyEventDoneCallback callback(false);
    const std::string expected_value =
        "onKeyEvent:keydown:a:KeyA:false:false:false:false";
    ExtensionTestMessageListener keyevent_listener(expected_value, false);

    engine_handler->ProcessKeyEvent(0x61,  // KeySym for 'a'.
                                    0x26,  // KeyCode for 'a'.
                                    0,  // No modifiers.
                                    base::Bind(&KeyEventDoneCallback::Run,
                                               base::Unretained(&callback)));
    ASSERT_TRUE(keyevent_listener.WaitUntilSatisfied());
    EXPECT_TRUE(keyevent_listener.was_satisfied());
    callback.WaitUntilCalled();
  }
  {
    SCOPED_TRACE("KeyDown, Ctrl:Yes, alt:No, Shift:No, Caps:No");
    KeyEventDoneCallback callback(false);
    const std::string expected_value =
        "onKeyEvent:keydown:a:KeyA:true:false:false:false";
    ExtensionTestMessageListener keyevent_listener(expected_value, false);

    engine_handler->ProcessKeyEvent(0x61,  // KeySym for 'a'.
                                    0x26,  // KeyCode for 'a'.
                                    kCtrlKeyMask,
                                    base::Bind(&KeyEventDoneCallback::Run,
                                               base::Unretained(&callback)));
    ASSERT_TRUE(keyevent_listener.WaitUntilSatisfied());
    EXPECT_TRUE(keyevent_listener.was_satisfied());
    callback.WaitUntilCalled();
  }
  {
    SCOPED_TRACE("KeyDown, Ctrl:No, alt:Yes, Shift:No, Caps:No");
    KeyEventDoneCallback callback(false);
    const std::string expected_value =
        "onKeyEvent:keydown:a:KeyA:false:true:false:false";
    ExtensionTestMessageListener keyevent_listener(expected_value, false);

    engine_handler->ProcessKeyEvent(0x61,  // KeySym for 'a'.
                                    0x26,  // KeyCode for 'a'.
                                    kAltKeyMask,
                                    base::Bind(&KeyEventDoneCallback::Run,
                                               base::Unretained(&callback)));
    ASSERT_TRUE(keyevent_listener.WaitUntilSatisfied());
    EXPECT_TRUE(keyevent_listener.was_satisfied());
    callback.WaitUntilCalled();
  }
  {
    SCOPED_TRACE("KeyDown, Ctrl:No, alt:No, Shift:Yes, Caps:No");
    KeyEventDoneCallback callback(false);
    const std::string expected_value =
        "onKeyEvent:keydown:a:KeyA:false:false:true:false";
    ExtensionTestMessageListener keyevent_listener(expected_value, false);

    engine_handler->ProcessKeyEvent(0x61,  // KeySym for 'a'.
                                    0x26,  // KeyCode for 'a'.
                                    kShiftKeyMask,
                                    base::Bind(&KeyEventDoneCallback::Run,
                                               base::Unretained(&callback)));
    ASSERT_TRUE(keyevent_listener.WaitUntilSatisfied());
    EXPECT_TRUE(keyevent_listener.was_satisfied());
    callback.WaitUntilCalled();
  }
  {
    SCOPED_TRACE("KeyDown, Ctrl:No, alt:No, Shift:No, Caps:Yes");
    KeyEventDoneCallback callback(false);
    const std::string expected_value =
        "onKeyEvent:keydown:a:KeyA:false:false:false:true";
    ExtensionTestMessageListener keyevent_listener(expected_value, false);

    engine_handler->ProcessKeyEvent(0x61,  // KeySym for 'a'.
                                    0x26,  // KeyCode for 'a'.
                                    kCapsLockMask,
                                    base::Bind(&KeyEventDoneCallback::Run,
                                               base::Unretained(&callback)));
    ASSERT_TRUE(keyevent_listener.WaitUntilSatisfied());
    EXPECT_TRUE(keyevent_listener.was_satisfied());
    callback.WaitUntilCalled();
  }
  {
    SCOPED_TRACE("KeyDown, Ctrl:Yes, alt:Yes, Shift:No, Caps:No");
    KeyEventDoneCallback callback(false);
    const std::string expected_value =
        "onKeyEvent:keydown:a:KeyA:true:true:false:false";
    ExtensionTestMessageListener keyevent_listener(expected_value, false);

    engine_handler->ProcessKeyEvent(0x61,  // KeySym for 'a'.
                                    0x26,  // KeyCode for 'a'.
                                    kAltKeyMask | kCtrlKeyMask,
                                    base::Bind(&KeyEventDoneCallback::Run,
                                               base::Unretained(&callback)));
    ASSERT_TRUE(keyevent_listener.WaitUntilSatisfied());
    EXPECT_TRUE(keyevent_listener.was_satisfied());
    callback.WaitUntilCalled();
  }
  {
    SCOPED_TRACE("KeyDown, Ctrl:No, alt:No, Shift:Yes, Caps:Yes");
    KeyEventDoneCallback callback(false);
    const std::string expected_value =
        "onKeyEvent:keydown:a:KeyA:false:false:true:true";
    ExtensionTestMessageListener keyevent_listener(expected_value, false);

    engine_handler->ProcessKeyEvent(0x61,  // KeySym for 'a'.
                                    0x26,  // KeyCode for 'a'.
                                    kShiftKeyMask | kCapsLockMask,
                                    base::Bind(&KeyEventDoneCallback::Run,
                                               base::Unretained(&callback)));
    ASSERT_TRUE(keyevent_listener.WaitUntilSatisfied());
    EXPECT_TRUE(keyevent_listener.was_satisfied());
    callback.WaitUntilCalled();
  }
  // TODO(nona): Add browser tests for other API as well.
  {
    SCOPED_TRACE("commitText test");
    const char commit_text_test_script[] =
        "chrome.input.ime.commitText({"
        "  contextID: engineBridge.getFocusedContextID().contextID,"
        "  text:'COMMIT_TEXT'"
        "});";

    engine_service->Clear();
    ASSERT_TRUE(content::ExecuteScript(host->host_contents(),
                                       commit_text_test_script));
    EXPECT_EQ(1, engine_service->commit_text_call_count());
    EXPECT_EQ("COMMIT_TEXT", engine_service->last_commit_text());
  }
  {
    SCOPED_TRACE("setComposition test");
    const char set_composition_test_script[] =
        "chrome.input.ime.setComposition({"
        "  contextID: engineBridge.getFocusedContextID().contextID,"
        "  text:'COMPOSITION_TEXT',"
        "  cursor:4,"
        "  segments : [{"
        "    start: 0,"
        "    end: 5,"
        "    style: 'underline'"
        "  },{"
        "    start: 6,"
        "    end: 10,"
        "    style: 'doubleUnderline'"
        "  }]"
        "});";

    engine_service->Clear();
    ASSERT_TRUE(content::ExecuteScript(host->host_contents(),
                                       set_composition_test_script));
    EXPECT_EQ(1, engine_service->update_preedit_call_count());

    EXPECT_EQ(4U, engine_service->last_update_preedit_arg().cursor_pos);
    EXPECT_TRUE(engine_service->last_update_preedit_arg().is_visible);

    const IBusText& ibus_text =
        engine_service->last_update_preedit_arg().ibus_text;
    EXPECT_EQ("COMPOSITION_TEXT", ibus_text.text());
    const std::vector<IBusText::UnderlineAttribute>& underlines =
        ibus_text.underline_attributes();

    ASSERT_EQ(2U, underlines.size());
    EXPECT_EQ(IBusText::IBUS_TEXT_UNDERLINE_SINGLE, underlines[0].type);
    EXPECT_EQ(0U, underlines[0].start_index);
    EXPECT_EQ(5U, underlines[0].end_index);

    EXPECT_EQ(IBusText::IBUS_TEXT_UNDERLINE_DOUBLE, underlines[1].type);
    EXPECT_EQ(6U, underlines[1].start_index);
    EXPECT_EQ(10U, underlines[1].end_index);
  }
  {
    SCOPED_TRACE("clearComposition test");
    const char commite_text_test_script[] =
        "chrome.input.ime.clearComposition({"
        "  contextID: engineBridge.getFocusedContextID().contextID,"
        "});";

    engine_service->Clear();
    ASSERT_TRUE(content::ExecuteScript(host->host_contents(),
                                       commite_text_test_script));
    EXPECT_EQ(1, engine_service->update_preedit_call_count());
    EXPECT_FALSE(engine_service->last_update_preedit_arg().is_visible);
    const IBusText& ibus_text =
        engine_service->last_update_preedit_arg().ibus_text;
    EXPECT_TRUE(ibus_text.text().empty());
  }
  {
    SCOPED_TRACE("setCandidateWindowProperties:visibility test");
    const char set_candidate_window_properties_test_script[] =
        "chrome.input.ime.setCandidateWindowProperties({"
        "  engineID: engineBridge.getActiveEngineID(),"
        "  properties: {"
        "    visible: true,"
        "  }"
        "});";
    engine_service->Clear();
    ASSERT_TRUE(content::ExecuteScript(
        host->host_contents(),
        set_candidate_window_properties_test_script));
    EXPECT_EQ(1, engine_service->update_lookup_table_call_count());
    EXPECT_TRUE(engine_service->last_update_lookup_table_arg().is_visible);
  }
  {
    SCOPED_TRACE("setCandidateWindowProperties:cursor_visibility test");
    const char set_candidate_window_properties_test_script[] =
        "chrome.input.ime.setCandidateWindowProperties({"
        "  engineID: engineBridge.getActiveEngineID(),"
        "  properties: {"
        "    cursorVisible: true,"
        "  }"
        "});";
    engine_service->Clear();
    ASSERT_TRUE(content::ExecuteScript(
        host->host_contents(),
        set_candidate_window_properties_test_script));
    EXPECT_EQ(1, engine_service->update_lookup_table_call_count());

    // window visibility is kept as before.
    EXPECT_TRUE(engine_service->last_update_lookup_table_arg().is_visible);

    const IBusLookupTable& table =
        engine_service->last_update_lookup_table_arg().lookup_table;
    EXPECT_TRUE(table.is_cursor_visible());
  }
  {
    SCOPED_TRACE("setCandidateWindowProperties:vertical test");
    const char set_candidate_window_properties_test_script[] =
        "chrome.input.ime.setCandidateWindowProperties({"
        "  engineID: engineBridge.getActiveEngineID(),"
        "  properties: {"
        "    vertical: true,"
        "  }"
        "});";
    engine_service->Clear();
    ASSERT_TRUE(content::ExecuteScript(
        host->host_contents(),
        set_candidate_window_properties_test_script));
    EXPECT_EQ(1, engine_service->update_lookup_table_call_count());

    // window visibility is kept as before.
    EXPECT_TRUE(engine_service->last_update_lookup_table_arg().is_visible);

    const IBusLookupTable& table =
        engine_service->last_update_lookup_table_arg().lookup_table;

    // cursor visibility is kept as before.
    EXPECT_TRUE(table.is_cursor_visible());

    EXPECT_EQ(IBusLookupTable::VERTICAL, table.orientation());
  }
  {
    SCOPED_TRACE("setCandidateWindowProperties:pageSize test");
    const char set_candidate_window_properties_test_script[] =
        "chrome.input.ime.setCandidateWindowProperties({"
        "  engineID: engineBridge.getActiveEngineID(),"
        "  properties: {"
        "    pageSize: 7,"
        "  }"
        "});";
    engine_service->Clear();
    ASSERT_TRUE(content::ExecuteScript(
        host->host_contents(),
        set_candidate_window_properties_test_script));
    EXPECT_EQ(1, engine_service->update_lookup_table_call_count());

    // window visibility is kept as before.
    EXPECT_TRUE(engine_service->last_update_lookup_table_arg().is_visible);

    const IBusLookupTable& table =
        engine_service->last_update_lookup_table_arg().lookup_table;

    // cursor visibility is kept as before.
    EXPECT_TRUE(table.is_cursor_visible());

    // oritantation is kept as before.
    EXPECT_EQ(IBusLookupTable::VERTICAL, table.orientation());

    EXPECT_EQ(7U, table.page_size());
  }
  {
    SCOPED_TRACE("setCandidateWindowProperties:auxTextVisibility test");
    const char set_candidate_window_properties_test_script[] =
        "chrome.input.ime.setCandidateWindowProperties({"
        "  engineID: engineBridge.getActiveEngineID(),"
        "  properties: {"
        "    auxiliaryTextVisible: true"
        "  }"
        "});";
    engine_service->Clear();
    ASSERT_TRUE(content::ExecuteScript(
        host->host_contents(),
        set_candidate_window_properties_test_script));
    EXPECT_EQ(1, engine_service->update_auxiliary_text_call_count());
    EXPECT_TRUE(engine_service->last_update_aux_text_arg().is_visible);
  }
  {
    SCOPED_TRACE("setCandidateWindowProperties:auxText test");
    const char set_candidate_window_properties_test_script[] =
        "chrome.input.ime.setCandidateWindowProperties({"
        "  engineID: engineBridge.getActiveEngineID(),"
        "  properties: {"
        "    auxiliaryText: 'AUXILIARY_TEXT'"
        "  }"
        "});";
    engine_service->Clear();
    ASSERT_TRUE(content::ExecuteScript(
        host->host_contents(),
        set_candidate_window_properties_test_script));
    EXPECT_EQ(1, engine_service->update_auxiliary_text_call_count());

    // aux text visibility is kept as before.
    EXPECT_TRUE(engine_service->last_update_aux_text_arg().is_visible);

    EXPECT_EQ("AUXILIARY_TEXT",
              engine_service->last_update_aux_text_arg().ibus_text.text());
  }
  {
    SCOPED_TRACE("setCandidates test");
    const char set_candidates_test_script[] =
        "chrome.input.ime.setCandidates({"
        "  contextID: engineBridge.getFocusedContextID().contextID,"
        "  candidates: [{"
        "    candidate: 'CANDIDATE_1',"
        "    id: 1,"
        "    },{"
        "    candidate: 'CANDIDATE_2',"
        "    id: 2,"
        "    label: 'LABEL_2',"
        "    },{"
        "    candidate: 'CANDIDATE_3',"
        "    id: 3,"
        "    label: 'LABEL_3',"
        "    annotation: 'ANNOTACTION_3'"
        "    },{"
        "    candidate: 'CANDIDATE_4',"
        "    id: 4,"
        "    label: 'LABEL_4',"
        "    annotation: 'ANNOTACTION_4',"
        "    usage: {"
        "      title: 'TITLE_4',"
        "      body: 'BODY_4'"
        "    }"
        "  }]"
        "});";
    engine_service->Clear();
    ASSERT_TRUE(content::ExecuteScript(host->host_contents(),
                                       set_candidates_test_script));

    // window visibility is kept as before.
    EXPECT_TRUE(engine_service->last_update_lookup_table_arg().is_visible);

    const IBusLookupTable& table =
        engine_service->last_update_lookup_table_arg().lookup_table;

    // cursor visibility is kept as before.
    EXPECT_TRUE(table.is_cursor_visible());

    // oritantation is kept as before.
    EXPECT_EQ(IBusLookupTable::VERTICAL, table.orientation());

    // page size is kept as before.
    EXPECT_EQ(7U, table.page_size());

    ASSERT_EQ(4U, table.candidates().size());

    EXPECT_EQ("CANDIDATE_1", table.candidates().at(0).value);

    EXPECT_EQ("CANDIDATE_2", table.candidates().at(1).value);
    EXPECT_EQ("LABEL_2", table.candidates().at(1).label);

    EXPECT_EQ("CANDIDATE_3", table.candidates().at(2).value);
    EXPECT_EQ("LABEL_3", table.candidates().at(2).label);
    EXPECT_EQ("ANNOTACTION_3", table.candidates().at(2).annotation);

    EXPECT_EQ("CANDIDATE_4", table.candidates().at(3).value);
    EXPECT_EQ("LABEL_4", table.candidates().at(3).label);
    EXPECT_EQ("ANNOTACTION_4", table.candidates().at(3).annotation);
    EXPECT_EQ("TITLE_4", table.candidates().at(3).description_title);
    EXPECT_EQ("BODY_4", table.candidates().at(3).description_body);
  }
  {
    SCOPED_TRACE("setCursorPosition test");
    const char set_cursor_position_test_script[] =
        "chrome.input.ime.setCursorPosition({"
        "  contextID: engineBridge.getFocusedContextID().contextID,"
        "  candidateID: 2"
        "});";
    engine_service->Clear();
    ASSERT_TRUE(content::ExecuteScript(
        host->host_contents(), set_cursor_position_test_script));
    EXPECT_EQ(1, engine_service->update_lookup_table_call_count());

    // window visibility is kept as before.
    EXPECT_TRUE(engine_service->last_update_lookup_table_arg().is_visible);

    const IBusLookupTable& table =
        engine_service->last_update_lookup_table_arg().lookup_table;

    // cursor visibility is kept as before.
    EXPECT_TRUE(table.is_cursor_visible());

    // oritantation is kept as before.
    EXPECT_EQ(IBusLookupTable::VERTICAL, table.orientation());

    // page size is kept as before.
    EXPECT_EQ(7U, table.page_size());

    // candidates are same as before.
    ASSERT_EQ(4U, table.candidates().size());

    // Candidate ID == 2 is 1 in index.
    EXPECT_EQ(1U, table.cursor_position());
  }
  {
    SCOPED_TRACE("setMenuItem test");
    const char set_menu_item_test_script[] =
        "chrome.input.ime.setMenuItems({"
        "  engineID: engineBridge.getActiveEngineID(),"
        "  items: [{"
        "    id: 'ID0',"
        "  },{"
        "    id: 'ID1',"
        "    label: 'LABEL1',"
        "  },{"
        "    id: 'ID2',"
        "    label: 'LABEL2',"
        "    style: 'radio',"
        "  },{"
        "    id: 'ID3',"
        "    label: 'LABEL3',"
        "    style: 'check',"
        "    visible: true,"
        "  },{"
        "    id: 'ID4',"
        "    label: 'LABEL4',"
        "    style: 'separator',"
        "    visible: true,"
        "    checked: true"
        "  }]"
        "});";
    engine_service->Clear();
    ASSERT_TRUE(content::ExecuteScript(
        host->host_contents(), set_menu_item_test_script));
    EXPECT_EQ(1, engine_service->register_properties_call_count());

    const IBusPropertyList& props =
        engine_service->last_registered_properties();
    ASSERT_EQ(5U, props.size());

    EXPECT_EQ("ID0", props[0]->key());
    EXPECT_EQ("ID1", props[1]->key());
    EXPECT_EQ("ID2", props[2]->key());
    EXPECT_EQ("ID3", props[3]->key());
    EXPECT_EQ("ID4", props[4]->key());

    EXPECT_EQ("LABEL1", props[1]->label());
    EXPECT_EQ("LABEL2", props[2]->label());
    EXPECT_EQ("LABEL3", props[3]->label());
    EXPECT_EQ("LABEL4", props[4]->label());

    EXPECT_EQ(IBusProperty::IBUS_PROPERTY_TYPE_RADIO, props[2]->type());
    EXPECT_EQ(IBusProperty::IBUS_PROPERTY_TYPE_TOGGLE, props[3]->type());
    EXPECT_EQ(IBusProperty::IBUS_PROPERTY_TYPE_SEPARATOR, props[4]->type());

    EXPECT_TRUE(props[3]->visible());
    EXPECT_TRUE(props[4]->visible());

    EXPECT_TRUE(props[4]->checked());
  }
  {
    SCOPED_TRACE("deleteSurroundingText test");
    const char delete_surrounding_text_test_script[] =
        "chrome.input.ime.deleteSurroundingText({"
        "  engineID: engineBridge.getActiveEngineID(),"
        "  contextID: engineBridge.getFocusedContextID().contextID,"
        "  offset: 5,"
        "  length: 3"
        "});";
    engine_service->Clear();
    ASSERT_TRUE(content::ExecuteScript(
        host->host_contents(), delete_surrounding_text_test_script));

    EXPECT_EQ(1, engine_service->delete_surrounding_text_call_count());
    EXPECT_EQ(5, engine_service->last_delete_surrounding_text_arg().offset);
    EXPECT_EQ(3U, engine_service->last_delete_surrounding_text_arg().length);
  }
}

}  // namespace
}  // namespace input_method
}  // namespace chromeos
