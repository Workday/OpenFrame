// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "components/gcm_driver/crypto/gcm_key_store.h"

#include "base/bind.h"
#include "base/files/scoped_temp_dir.h"
#include "base/message_loop/message_loop.h"
#include "base/run_loop.h"
#include "base/thread_task_runner_handle.h"
#include "components/gcm_driver/crypto/p256_key_util.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace gcm {

namespace {

const char kFakeAppId[] = "my_app_id";
const char kSecondFakeAppId[] = "my_other_app_id";

class GCMKeyStoreTest : public ::testing::Test {
 public:
  GCMKeyStoreTest() {}
  ~GCMKeyStoreTest() override {}

  void SetUp() override {
    ASSERT_TRUE(scoped_temp_dir_.CreateUniqueTempDir());
    CreateKeyStore();
  }

  void TearDown() override {
    gcm_key_store_.reset();

    // |gcm_key_store_| owns a ProtoDatabaseImpl whose destructor deletes the
    // underlying LevelDB database on the task runner.
    base::RunLoop().RunUntilIdle();
  }

  // Creates the GCM Key Store instance. May be called from within a test's body
  // to re-create the key store, causing the database to re-open.
  void CreateKeyStore() {
    gcm_key_store_.reset(
        new GCMKeyStore(scoped_temp_dir_.path(), message_loop_.task_runner()));
  }

  // Callback to use with GCMKeyStore::{GetKeys, CreateKeys} calls.
  void GotKeys(KeyPair* pair_out, std::string* auth_secret_out,
               const KeyPair& pair, const std::string& auth_secret) {
    *pair_out = pair;
    *auth_secret_out = auth_secret;
  }

  // Callback to use with GCMKeyStore::DeleteKeys calls.
  void DeletedKeys(bool* success_out, bool success) {
    DCHECK(success_out);

    *success_out = success;
  }

 protected:
  GCMKeyStore* gcm_key_store() { return gcm_key_store_.get(); }

 private:
  base::MessageLoop message_loop_;
  base::ScopedTempDir scoped_temp_dir_;

  scoped_ptr<GCMKeyStore> gcm_key_store_;
};

TEST_F(GCMKeyStoreTest, CreatedByDefault) {
  KeyPair pair;
  std::string auth_secret;
  gcm_key_store()->GetKeys(kFakeAppId,
                           base::Bind(&GCMKeyStoreTest::GotKeys,
                                      base::Unretained(this), &pair,
                                      &auth_secret));

  base::RunLoop().RunUntilIdle();

  ASSERT_FALSE(pair.IsInitialized());
  EXPECT_FALSE(pair.has_type());
  EXPECT_EQ(0u, auth_secret.size());
}

TEST_F(GCMKeyStoreTest, CreateAndGetKeys) {
  KeyPair pair;
  std::string auth_secret;
  gcm_key_store()->CreateKeys(kFakeAppId,
                              base::Bind(&GCMKeyStoreTest::GotKeys,
                                         base::Unretained(this), &pair,
                                         &auth_secret));

  base::RunLoop().RunUntilIdle();

  ASSERT_TRUE(pair.IsInitialized());
  ASSERT_TRUE(pair.has_private_key());
  ASSERT_TRUE(pair.has_public_key());

  EXPECT_GT(pair.public_key().size(), 0u);
  EXPECT_GT(pair.private_key().size(), 0u);

  ASSERT_GT(auth_secret.size(), 0u);

  KeyPair read_pair;
  std::string read_auth_secret;
  gcm_key_store()->GetKeys(kFakeAppId,
                           base::Bind(&GCMKeyStoreTest::GotKeys,
                                      base::Unretained(this), &read_pair,
                                      &read_auth_secret));

  base::RunLoop().RunUntilIdle();

  ASSERT_TRUE(read_pair.IsInitialized());

  EXPECT_EQ(pair.type(), read_pair.type());
  EXPECT_EQ(pair.private_key(), read_pair.private_key());
  EXPECT_EQ(pair.public_key(), read_pair.public_key());

  EXPECT_EQ(auth_secret, read_auth_secret);
}

TEST_F(GCMKeyStoreTest, KeysPersistenceBetweenInstances) {
  KeyPair pair;
  std::string auth_secret;
  gcm_key_store()->CreateKeys(kFakeAppId,
                              base::Bind(&GCMKeyStoreTest::GotKeys,
                                         base::Unretained(this), &pair,
                                         &auth_secret));

  base::RunLoop().RunUntilIdle();

  ASSERT_TRUE(pair.IsInitialized());

  // Create a new GCM Key Store instance.
  CreateKeyStore();

  KeyPair read_pair;
  std::string read_auth_secret;
  gcm_key_store()->GetKeys(kFakeAppId,
                           base::Bind(&GCMKeyStoreTest::GotKeys,
                                      base::Unretained(this), &read_pair,
                                      &read_auth_secret));

  base::RunLoop().RunUntilIdle();

  ASSERT_TRUE(read_pair.IsInitialized());
  EXPECT_TRUE(read_pair.has_type());
  EXPECT_GT(read_auth_secret.size(), 0u);
}

TEST_F(GCMKeyStoreTest, CreateAndDeleteKeys) {
  KeyPair pair;
  std::string auth_secret;
  gcm_key_store()->CreateKeys(kFakeAppId,
                              base::Bind(&GCMKeyStoreTest::GotKeys,
                                         base::Unretained(this), &pair,
                                         &auth_secret));

  base::RunLoop().RunUntilIdle();

  ASSERT_TRUE(pair.IsInitialized());

  KeyPair read_pair;
  std::string read_auth_secret;
  gcm_key_store()->GetKeys(kFakeAppId,
                           base::Bind(&GCMKeyStoreTest::GotKeys,
                                      base::Unretained(this), &read_pair,
                                      &read_auth_secret));

  base::RunLoop().RunUntilIdle();

  ASSERT_TRUE(read_pair.IsInitialized());
  EXPECT_TRUE(read_pair.has_type());

  bool success = false;
  gcm_key_store()->DeleteKeys(kFakeAppId,
                              base::Bind(&GCMKeyStoreTest::DeletedKeys,
                                         base::Unretained(this), &success));

  base::RunLoop().RunUntilIdle();

  ASSERT_TRUE(success);

  gcm_key_store()->GetKeys(kFakeAppId,
                           base::Bind(&GCMKeyStoreTest::GotKeys,
                                      base::Unretained(this), &read_pair,
                                      &read_auth_secret));

  base::RunLoop().RunUntilIdle();

  ASSERT_FALSE(read_pair.IsInitialized());
}

TEST_F(GCMKeyStoreTest, GetKeysMultipleAppIds) {
  KeyPair pair;
  std::string auth_secret;
  gcm_key_store()->CreateKeys(kFakeAppId,
                              base::Bind(&GCMKeyStoreTest::GotKeys,
                                         base::Unretained(this), &pair,
                                         &auth_secret));

  base::RunLoop().RunUntilIdle();

  ASSERT_TRUE(pair.IsInitialized());

  gcm_key_store()->CreateKeys(kSecondFakeAppId,
                              base::Bind(&GCMKeyStoreTest::GotKeys,
                                         base::Unretained(this), &pair,
                                         &auth_secret));

  base::RunLoop().RunUntilIdle();

  ASSERT_TRUE(pair.IsInitialized());

  KeyPair read_pair;
  std::string read_auth_secret;
  gcm_key_store()->GetKeys(kFakeAppId,
                           base::Bind(&GCMKeyStoreTest::GotKeys,
                                      base::Unretained(this), &read_pair,
                                      &read_auth_secret));

  base::RunLoop().RunUntilIdle();

  ASSERT_TRUE(read_pair.IsInitialized());
  EXPECT_TRUE(read_pair.has_type());
}

TEST_F(GCMKeyStoreTest, SuccessiveCallsBeforeInitialization) {
  KeyPair pair;
  std::string auth_secret;
  gcm_key_store()->CreateKeys(kFakeAppId,
                              base::Bind(&GCMKeyStoreTest::GotKeys,
                                         base::Unretained(this), &pair,
                                         &auth_secret));

  // Deliberately do not run the message loop, so that the callback has not
  // been resolved yet. The following EXPECT() ensures this.
  EXPECT_FALSE(pair.IsInitialized());

  KeyPair read_pair;
  std::string read_auth_secret;
  gcm_key_store()->GetKeys(kFakeAppId,
                           base::Bind(&GCMKeyStoreTest::GotKeys,
                                      base::Unretained(this), &read_pair,
                                      &read_auth_secret));

  EXPECT_FALSE(read_pair.IsInitialized());

  // Now run the message loop. Both tasks should have finished executing. Due
  // to the asynchronous nature of operations, however, we can't rely on the
  // write to have finished before the read begins.
  base::RunLoop().RunUntilIdle();

  EXPECT_TRUE(pair.IsInitialized());
}

}  // namespace

}  // namespace gcm
