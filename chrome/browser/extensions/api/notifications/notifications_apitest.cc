// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "base/strings/stringprintf.h"
#include "chrome/browser/browser_process.h"
#include "chrome/browser/chrome_notification_types.h"
#include "chrome/browser/extensions/api/notifications/notifications_api.h"
#include "chrome/browser/extensions/extension_apitest.h"
#include "chrome/browser/extensions/extension_function_test_utils.h"
#include "chrome/common/extensions/features/feature.h"
#include "content/public/browser/notification_service.h"
#include "content/public/test/test_utils.h"
#include "ui/message_center/message_center.h"
#include "ui/message_center/message_center_switches.h"
#include "ui/message_center/message_center_util.h"

using extensions::Extension;

namespace utils = extension_function_test_utils;

namespace {

class NotificationsApiTest : public ExtensionApiTest {
 public:
  const extensions::Extension* LoadExtensionAndWait(
      const std::string& test_name) {
    base::FilePath extdir = test_data_dir_.AppendASCII(test_name);
    content::WindowedNotificationObserver page_created(
        chrome::NOTIFICATION_EXTENSION_BACKGROUND_PAGE_READY,
        content::NotificationService::AllSources());
    const extensions::Extension* extension = LoadExtension(extdir);
    if (extension) {
      page_created.Wait();
    }
    return extension;
  }
};

}  // namespace

IN_PROC_BROWSER_TEST_F(NotificationsApiTest, TestIdUsage) {
  // Create a new notification. A lingering output of this block is the
  // notifications ID, which we'll use in later parts of this test.
  std::string notification_id;
  scoped_refptr<Extension> empty_extension(utils::CreateEmptyExtension());
  {
    scoped_refptr<extensions::NotificationsCreateFunction>
        notification_function(
            new extensions::NotificationsCreateFunction());

    notification_function->set_extension(empty_extension.get());
    notification_function->set_has_callback(true);

    scoped_ptr<base::Value> result(utils::RunFunctionAndReturnSingleResult(
        notification_function.get(),
        "[\"\", "  // Empty string: ask API to generate ID
        "{"
        "\"type\": \"basic\","
        "\"iconUrl\": \"an/image/that/does/not/exist.png\","
        "\"title\": \"Attention!\","
        "\"message\": \"Check out Cirque du Soleil\""
        "}]",
        browser(),
        utils::NONE));

    ASSERT_EQ(base::Value::TYPE_STRING, result->GetType());
    ASSERT_TRUE(result->GetAsString(&notification_id));
    ASSERT_TRUE(notification_id.length() > 0);
  }

  // Update the existing notification.
  {
    scoped_refptr<extensions::NotificationsUpdateFunction>
        notification_function(
            new extensions::NotificationsUpdateFunction());

    notification_function->set_extension(empty_extension.get());
    notification_function->set_has_callback(true);

    scoped_ptr<base::Value> result(utils::RunFunctionAndReturnSingleResult(
        notification_function.get(),
        "[\"" + notification_id +
            "\", "
            "{"
            "\"type\": \"basic\","
            "\"iconUrl\": \"an/image/that/does/not/exist.png\","
            "\"title\": \"Attention!\","
            "\"message\": \"Too late! The show ended yesterday\""
            "}]",
        browser(),
        utils::NONE));

    ASSERT_EQ(base::Value::TYPE_BOOLEAN, result->GetType());
    bool copy_bool_value = false;
    ASSERT_TRUE(result->GetAsBoolean(&copy_bool_value));
    ASSERT_TRUE(copy_bool_value);

    // TODO(miket): add a testing method to query the message from the
    // displayed notification, and assert it matches the updated message.
    //
    // TODO(miket): add a method to count the number of outstanding
    // notifications, and confirm it remains at one at this point.
  }

  // Update a nonexistent notification.
  {
    scoped_refptr<extensions::NotificationsUpdateFunction>
        notification_function(
            new extensions::NotificationsUpdateFunction());

    notification_function->set_extension(empty_extension.get());
    notification_function->set_has_callback(true);

    scoped_ptr<base::Value> result(utils::RunFunctionAndReturnSingleResult(
        notification_function.get(),
        "[\"xxxxxxxxxxxx\", "
        "{"
        "\"type\": \"basic\","
        "\"iconUrl\": \"an/image/that/does/not/exist.png\","
        "\"title\": \"!\","
        "\"message\": \"!\""
        "}]",
        browser(),
        utils::NONE));

    ASSERT_EQ(base::Value::TYPE_BOOLEAN, result->GetType());
    bool copy_bool_value = false;
    ASSERT_TRUE(result->GetAsBoolean(&copy_bool_value));
    ASSERT_FALSE(copy_bool_value);
  }

  // Clear a nonexistent notification.
  {
    scoped_refptr<extensions::NotificationsClearFunction>
        notification_function(
            new extensions::NotificationsClearFunction());

    notification_function->set_extension(empty_extension.get());
    notification_function->set_has_callback(true);

    scoped_ptr<base::Value> result(
        utils::RunFunctionAndReturnSingleResult(notification_function.get(),
                                                "[\"xxxxxxxxxxx\"]",
                                                browser(),
                                                utils::NONE));

    ASSERT_EQ(base::Value::TYPE_BOOLEAN, result->GetType());
    bool copy_bool_value = false;
    ASSERT_TRUE(result->GetAsBoolean(&copy_bool_value));
    ASSERT_FALSE(copy_bool_value);
  }

  // Clear the notification we created.
  {
    scoped_refptr<extensions::NotificationsClearFunction>
        notification_function(
            new extensions::NotificationsClearFunction());

    notification_function->set_extension(empty_extension.get());
    notification_function->set_has_callback(true);

    scoped_ptr<base::Value> result(
        utils::RunFunctionAndReturnSingleResult(notification_function.get(),
                                                "[\"" + notification_id + "\"]",
                                                browser(),
                                                utils::NONE));

    ASSERT_EQ(base::Value::TYPE_BOOLEAN, result->GetType());
    bool copy_bool_value = false;
    ASSERT_TRUE(result->GetAsBoolean(&copy_bool_value));
    ASSERT_TRUE(copy_bool_value);
  }
}

IN_PROC_BROWSER_TEST_F(NotificationsApiTest, TestBaseFormatNotification) {
  scoped_refptr<Extension> empty_extension(utils::CreateEmptyExtension());

  // Create a new notification with the minimum required properties.
  {
    scoped_refptr<extensions::NotificationsCreateFunction>
        notification_create_function(
            new extensions::NotificationsCreateFunction());
    notification_create_function->set_extension(empty_extension.get());
    notification_create_function->set_has_callback(true);

    scoped_ptr<base::Value> result(utils::RunFunctionAndReturnSingleResult(
        notification_create_function.get(),
        "[\"\", "
        "{"
        "\"type\": \"basic\","
        "\"iconUrl\": \"an/image/that/does/not/exist.png\","
        "\"title\": \"Attention!\","
        "\"message\": \"Check out Cirque du Soleil\""
        "}]",
        browser(),
        utils::NONE));

    std::string notification_id;
    ASSERT_EQ(base::Value::TYPE_STRING, result->GetType());
    ASSERT_TRUE(result->GetAsString(&notification_id));
    ASSERT_TRUE(notification_id.length() > 0);
  }

  // Create another new notification with more than the required properties.
  {
    scoped_refptr<extensions::NotificationsCreateFunction>
        notification_create_function(
            new extensions::NotificationsCreateFunction());
    notification_create_function->set_extension(empty_extension.get());
    notification_create_function->set_has_callback(true);

    scoped_ptr<base::Value> result(utils::RunFunctionAndReturnSingleResult(
        notification_create_function.get(),
        "[\"\", "
        "{"
        "\"type\": \"basic\","
        "\"iconUrl\": \"an/image/that/does/not/exist.png\","
        "\"title\": \"Attention!\","
        "\"message\": \"Check out Cirque du Soleil\","
        "\"priority\": 1,"
        "\"eventTime\": 1234567890.12345678,"
        "\"buttons\": ["
        "  {"
        "   \"title\": \"Up\","
        "   \"iconUrl\":\"http://www.google.com/logos/2012/\""
        "  },"
        "  {"
        "   \"title\": \"Down\""  // note: no iconUrl
        "  }"
        "],"
        "\"expandedMessage\": \"This is a longer expanded message.\","
        "\"imageUrl\": \"http://www.google.com/logos/2012/election12-hp.jpg\""
        "}]",
        browser(),
        utils::NONE));

    std::string notification_id;
    ASSERT_EQ(base::Value::TYPE_STRING, result->GetType());
    ASSERT_TRUE(result->GetAsString(&notification_id));
    ASSERT_TRUE(notification_id.length() > 0);
  }

  // Error case: missing type property.
  {
    scoped_refptr<extensions::NotificationsCreateFunction>
        notification_create_function(
            new extensions::NotificationsCreateFunction());
    notification_create_function->set_extension(empty_extension.get());
    notification_create_function->set_has_callback(true);

    utils::RunFunction(
        notification_create_function.get(),
        "[\"\", "
        "{"
        "\"iconUrl\": \"an/image/that/does/not/exist.png\","
        "\"title\": \"Attention!\","
        "\"message\": \"Check out Cirque du Soleil\""
        "}]",
        browser(),
        utils::NONE);

    EXPECT_FALSE(notification_create_function->GetError().empty());
  }

  // Error case: missing iconUrl property.
  {
    scoped_refptr<extensions::NotificationsCreateFunction>
        notification_create_function(
            new extensions::NotificationsCreateFunction());
    notification_create_function->set_extension(empty_extension.get());
    notification_create_function->set_has_callback(true);

    utils::RunFunction(
        notification_create_function.get(),
        "[\"\", "
        "{"
        "\"type\": \"basic\","
        "\"title\": \"Attention!\","
        "\"message\": \"Check out Cirque du Soleil\""
        "}]",
        browser(),
        utils::NONE);

    EXPECT_FALSE(notification_create_function->GetError().empty());
  }

  // Error case: missing title property.
  {
    scoped_refptr<extensions::NotificationsCreateFunction>
        notification_create_function(
            new extensions::NotificationsCreateFunction());
    notification_create_function->set_extension(empty_extension.get());
    notification_create_function->set_has_callback(true);

    utils::RunFunction(
        notification_create_function.get(),
        "[\"\", "
        "{"
        "\"type\": \"basic\","
        "\"iconUrl\": \"an/image/that/does/not/exist.png\","
        "\"message\": \"Check out Cirque du Soleil\""
        "}]",
        browser(),
        utils::NONE);

    EXPECT_FALSE(notification_create_function->GetError().empty());
  }

  // Error case: missing message property.
  {
    scoped_refptr<extensions::NotificationsCreateFunction>
        notification_create_function(
            new extensions::NotificationsCreateFunction());
    notification_create_function->set_extension(empty_extension.get());
    notification_create_function->set_has_callback(true);

    utils::RunFunction(
        notification_create_function.get(),
        "[\"\", "
        "{"
        "\"type\": \"basic\","
        "\"iconUrl\": \"an/image/that/does/not/exist.png\","
        "\"title\": \"Attention!\""
        "}]",
        browser(),
        utils::NONE);

    EXPECT_FALSE(notification_create_function->GetError().empty());
  }
}

IN_PROC_BROWSER_TEST_F(NotificationsApiTest, TestMultipleItemNotification) {
  scoped_refptr<extensions::NotificationsCreateFunction>
      notification_create_function(
          new extensions::NotificationsCreateFunction());
  scoped_refptr<Extension> empty_extension(utils::CreateEmptyExtension());

  notification_create_function->set_extension(empty_extension.get());
  notification_create_function->set_has_callback(true);

  scoped_ptr<base::Value> result(utils::RunFunctionAndReturnSingleResult(
      notification_create_function.get(),
      "[\"\", "
      "{"
      "\"type\": \"list\","
      "\"iconUrl\": \"an/image/that/does/not/exist.png\","
      "\"title\": \"Multiple Item Notification Title\","
      "\"message\": \"Multiple item notification message.\","
      "\"items\": ["
      "  {\"title\": \"Brett Boe\","
      " \"message\": \"This is an important message!\"},"
      "  {\"title\": \"Carla Coe\","
      " \"message\": \"Just took a look at the proposal\"},"
      "  {\"title\": \"Donna Doe\","
      " \"message\": \"I see that you went to the conference\"},"
      "  {\"title\": \"Frank Foe\","
      " \"message\": \"I ate Harry's sandwich!\"},"
      "  {\"title\": \"Grace Goe\","
      " \"message\": \"I saw Frank steal a sandwich :-)\"}"
      "],"
      "\"priority\": 1,"
      "\"eventTime\": 1361488019.9999999"
      "}]",
      browser(),
      utils::NONE));
  // TODO(dharcourt): [...], items = [{title: foo, message: bar}, ...], [...]

  std::string notification_id;
  ASSERT_EQ(base::Value::TYPE_STRING, result->GetType());
  ASSERT_TRUE(result->GetAsString(&notification_id));
  ASSERT_TRUE(notification_id.length() > 0);
}

#if defined(OS_LINUX)
#define MAYBE_TestGetAll DISABLED_TestGetAll
#else
#define MAYBE_TestGetAll TestGetAll
#endif

IN_PROC_BROWSER_TEST_F(NotificationsApiTest, MAYBE_TestGetAll) {
  scoped_refptr<Extension> empty_extension(utils::CreateEmptyExtension());

  {
    scoped_refptr<extensions::NotificationsGetAllFunction>
        notification_get_all_function(
            new extensions::NotificationsGetAllFunction());
    notification_get_all_function->set_extension(empty_extension.get());
    notification_get_all_function->set_has_callback(true);
    scoped_ptr<base::Value> result(utils::RunFunctionAndReturnSingleResult(
        notification_get_all_function.get(), "[]", browser(), utils::NONE));

    base::DictionaryValue* return_value;
    ASSERT_EQ(base::Value::TYPE_DICTIONARY, result->GetType());
    ASSERT_TRUE(result->GetAsDictionary(&return_value));
    ASSERT_TRUE(return_value->size() == 0);
  }

  const unsigned int kNotificationsToCreate = 4;

  for (unsigned int i = 0; i < kNotificationsToCreate; i++) {
    scoped_refptr<extensions::NotificationsCreateFunction>
        notification_create_function(
            new extensions::NotificationsCreateFunction());

    notification_create_function->set_extension(empty_extension.get());
    notification_create_function->set_has_callback(true);

    scoped_ptr<base::Value> result(utils::RunFunctionAndReturnSingleResult(
        notification_create_function.get(),
        base::StringPrintf("[\"identifier-%u\", "
                           "{"
                           "\"type\": \"basic\","
                           "\"iconUrl\": \"an/image/that/does/not/exist.png\","
                           "\"title\": \"Title\","
                           "\"message\": \"Message.\","
                           "\"priority\": 1,"
                           "\"eventTime\": 1361488019.9999999"
                           "}]",
                           i),
        browser(),
        utils::NONE));
  }

  {
    scoped_refptr<extensions::NotificationsGetAllFunction>
        notification_get_all_function(
            new extensions::NotificationsGetAllFunction());
    notification_get_all_function->set_extension(empty_extension.get());
    notification_get_all_function->set_has_callback(true);
    scoped_ptr<base::Value> result(utils::RunFunctionAndReturnSingleResult(
        notification_get_all_function.get(), "[]", browser(), utils::NONE));

    base::DictionaryValue* return_value;
    ASSERT_EQ(base::Value::TYPE_DICTIONARY, result->GetType());
    ASSERT_TRUE(result->GetAsDictionary(&return_value));
    ASSERT_EQ(return_value->size(), kNotificationsToCreate);
    bool dictionary_bool = false;
    for (unsigned int i = 0; i < kNotificationsToCreate; i++) {
      std::string id = base::StringPrintf("identifier-%u", i);
      ASSERT_TRUE(return_value->GetBoolean(id, &dictionary_bool));
      ASSERT_TRUE(dictionary_bool);
    }
  }
}

IN_PROC_BROWSER_TEST_F(NotificationsApiTest, TestEvents) {
  ASSERT_TRUE(RunExtensionTest("notifications/api/events")) << message_;
}

IN_PROC_BROWSER_TEST_F(NotificationsApiTest, TestCSP) {
  ASSERT_TRUE(RunExtensionTest("notifications/api/csp")) << message_;
}

// MessaceCenter-specific test.
#if defined(RUN_MESSAGE_CENTER_TESTS)
#define MAYBE_TestByUser TestByUser
#else
#define MAYBE_TestByUser DISABLED_TestByUser
#endif

IN_PROC_BROWSER_TEST_F(NotificationsApiTest, MAYBE_TestByUser) {
  ASSERT_TRUE(message_center::IsRichNotificationEnabled());

  const extensions::Extension* extension =
      LoadExtensionAndWait("notifications/api/by_user");
  ASSERT_TRUE(extension) << message_;

  {
    ResultCatcher catcher;
    g_browser_process->message_center()->RemoveNotification(
        extension->id() + "-FOO",
        false);
    EXPECT_TRUE(catcher.GetNextResult()) << catcher.message();
  }

  {
    ResultCatcher catcher;
    g_browser_process->message_center()->RemoveNotification(
        extension->id() + "-BAR",
        true);
    EXPECT_TRUE(catcher.GetNextResult()) << catcher.message();
  }
}


#if defined(OS_LINUX)
#define MAYBE_TestProgressNotification DISABLED_TestProgressNotification
#else
#define MAYBE_TestProgressNotification TestProgressNotification
#endif

IN_PROC_BROWSER_TEST_F(NotificationsApiTest, MAYBE_TestProgressNotification) {
  scoped_refptr<Extension> empty_extension(utils::CreateEmptyExtension());

  // Create a new progress notification.
  std::string notification_id;
  {
    scoped_refptr<extensions::NotificationsCreateFunction>
        notification_create_function(
            new extensions::NotificationsCreateFunction());
    notification_create_function->set_extension(empty_extension.get());
    notification_create_function->set_has_callback(true);

    scoped_ptr<base::Value> result(utils::RunFunctionAndReturnSingleResult(
        notification_create_function.get(),
        "[\"\", "
        "{"
        "\"type\": \"progress\","
        "\"iconUrl\": \"an/image/that/does/not/exist.png\","
        "\"title\": \"Test!\","
        "\"message\": \"This is a progress notification.\","
        "\"priority\": 1,"
        "\"eventTime\": 1234567890.12345678,"
        "\"progress\": 30"
        "}]",
        browser(),
        utils::NONE));

    EXPECT_EQ(base::Value::TYPE_STRING, result->GetType());
    EXPECT_TRUE(result->GetAsString(&notification_id));
    EXPECT_TRUE(notification_id.length() > 0);
  }

  // Update the progress property only.
  {
    scoped_refptr<extensions::NotificationsUpdateFunction>
        notification_function(
            new extensions::NotificationsUpdateFunction());
    notification_function->set_extension(empty_extension.get());
    notification_function->set_has_callback(true);

    scoped_ptr<base::Value> result(utils::RunFunctionAndReturnSingleResult(
        notification_function.get(),
        "[\"" + notification_id +
            "\", "
            "{"
            "\"progress\": 60"
            "}]",
        browser(),
        utils::NONE));

    EXPECT_EQ(base::Value::TYPE_BOOLEAN, result->GetType());
    bool copy_bool_value = false;
    EXPECT_TRUE(result->GetAsBoolean(&copy_bool_value));
    EXPECT_TRUE(copy_bool_value);
  }

  // Error case: progress value provided for non-progress type.
  {
    scoped_refptr<extensions::NotificationsCreateFunction>
        notification_create_function(
            new extensions::NotificationsCreateFunction());
    notification_create_function->set_extension(empty_extension.get());
    notification_create_function->set_has_callback(true);

    utils::RunFunction(
        notification_create_function.get(),
        "[\"\", "
        "{"
        "\"type\": \"basic\","
        "\"iconUrl\": \"an/image/that/does/not/exist.png\","
        "\"title\": \"Test!\","
        "\"message\": \"This is a progress notification.\","
        "\"priority\": 1,"
        "\"eventTime\": 1234567890.12345678,"
        "\"progress\": 10"
        "}]",
        browser(),
        utils::NONE);
    EXPECT_FALSE(notification_create_function->GetError().empty());
  }

  // Error case: progress value less than lower bound.
  {
    scoped_refptr<extensions::NotificationsCreateFunction>
        notification_create_function(
            new extensions::NotificationsCreateFunction());
    notification_create_function->set_extension(empty_extension.get());
    notification_create_function->set_has_callback(true);

    utils::RunFunction(
        notification_create_function.get(),
        "[\"\", "
        "{"
        "\"type\": \"progress\","
        "\"iconUrl\": \"an/image/that/does/not/exist.png\","
        "\"title\": \"Test!\","
        "\"message\": \"This is a progress notification.\","
        "\"priority\": 1,"
        "\"eventTime\": 1234567890.12345678,"
        "\"progress\": -10"
        "}]",
        browser(),
        utils::NONE);
    EXPECT_FALSE(notification_create_function->GetError().empty());
  }

  // Error case: progress value greater than upper bound.
  {
    scoped_refptr<extensions::NotificationsCreateFunction>
        notification_create_function(
            new extensions::NotificationsCreateFunction());
    notification_create_function->set_extension(empty_extension.get());
    notification_create_function->set_has_callback(true);

    utils::RunFunction(
        notification_create_function.get(),
        "[\"\", "
        "{"
        "\"type\": \"progress\","
        "\"iconUrl\": \"an/image/that/does/not/exist.png\","
        "\"title\": \"Test!\","
        "\"message\": \"This is a progress notification.\","
        "\"priority\": 1,"
        "\"eventTime\": 1234567890.12345678,"
        "\"progress\": 101"
        "}]",
        browser(),
        utils::NONE);
    EXPECT_FALSE(notification_create_function->GetError().empty());
  }
}

IN_PROC_BROWSER_TEST_F(NotificationsApiTest, TestPartialUpdate) {
  scoped_refptr<Extension> empty_extension(utils::CreateEmptyExtension());

  // Create a new notification.
  std::string notification_id;
  {
    scoped_refptr<extensions::NotificationsCreateFunction>
        notification_function(
            new extensions::NotificationsCreateFunction());
    notification_function->set_extension(empty_extension.get());
    notification_function->set_has_callback(true);

    scoped_ptr<base::Value> result(utils::RunFunctionAndReturnSingleResult(
        notification_function.get(),
        "[\"\", "  // Empty string: ask API to generate ID
        "{"
        "\"type\": \"basic\","
        "\"iconUrl\": \"an/image/that/does/not/exist.png\","
        "\"title\": \"Attention!\","
        "\"message\": \"Check out Cirque du Soleil\""
        "}]",
        browser(),
        utils::NONE));

    ASSERT_EQ(base::Value::TYPE_STRING, result->GetType());
    ASSERT_TRUE(result->GetAsString(&notification_id));
    ASSERT_TRUE(notification_id.length() > 0);
  }

  // Update a few properties in the existing notification.
  {
    scoped_refptr<extensions::NotificationsUpdateFunction>
        notification_function(
            new extensions::NotificationsUpdateFunction());
    notification_function->set_extension(empty_extension.get());
    notification_function->set_has_callback(true);

    scoped_ptr<base::Value> result(utils::RunFunctionAndReturnSingleResult(
        notification_function.get(),
        "[\"" + notification_id +
            "\", "
            "{"
            "\"title\": \"Changed!\","
            "\"message\": \"Too late! The show ended yesterday\""
            "}]",
        browser(),
        utils::NONE));

    ASSERT_EQ(base::Value::TYPE_BOOLEAN, result->GetType());
    bool copy_bool_value = false;
    ASSERT_TRUE(result->GetAsBoolean(&copy_bool_value));
    ASSERT_TRUE(copy_bool_value);
  }

  // Update another property in the existing notification.
  {
    scoped_refptr<extensions::NotificationsUpdateFunction>
        notification_function(
            new extensions::NotificationsUpdateFunction());
    notification_function->set_extension(empty_extension.get());
    notification_function->set_has_callback(true);

    scoped_ptr<base::Value> result(utils::RunFunctionAndReturnSingleResult(
        notification_function.get(),
        "[\"" + notification_id +
            "\", "
            "{"
            "\"priority\": 2"
            "}]",
        browser(),
        utils::NONE));

    ASSERT_EQ(base::Value::TYPE_BOOLEAN, result->GetType());
    bool copy_bool_value = false;
    ASSERT_TRUE(result->GetAsBoolean(&copy_bool_value));
    ASSERT_TRUE(copy_bool_value);
  }
}
