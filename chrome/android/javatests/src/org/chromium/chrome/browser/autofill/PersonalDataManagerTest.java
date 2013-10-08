// Copyright (c) 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package org.chromium.chrome.browser.autofill;

import android.test.suitebuilder.annotation.SmallTest;

import org.chromium.base.test.util.Feature;
import org.chromium.chrome.browser.autofill.PersonalDataManager.AutofillProfile;
import org.chromium.chrome.browser.autofill.PersonalDataManager.CreditCard;
import org.chromium.chrome.testshell.ChromiumTestShellTestBase;

import java.util.concurrent.ExecutionException;

/**
 * Tests for Chrome on Android's usage of the PersonalDataManager API.
 */
public class PersonalDataManagerTest extends ChromiumTestShellTestBase {

    private AutofillTestHelper mHelper;

    @Override
    public void setUp() throws Exception {
        super.setUp();
        clearAppData();
        launchChromiumTestShellWithBlankPage();
        assertTrue(waitForActiveShellToBeDoneLoading());

        mHelper = new AutofillTestHelper();
    }

    @SmallTest
    @Feature({"Autofill"})
    public void testAddAndEditProfiles() throws InterruptedException, ExecutionException {
        AutofillProfile profile = new AutofillProfile(
                "" /* guid */, "https://www.example.com" /* origin */,
                "John Smith", "Acme Inc.", "1 Main", "Apt A", "San Francisco", "CA",
                "94102", "US", "4158889999", "john@acme.inc");
        String profileOneGUID = mHelper.setProfile(profile);
        assertEquals(1, mHelper.getNumberOfProfiles());

        AutofillProfile profile2 = new AutofillProfile(
                "" /* guid */, "http://www.example.com" /* origin */,
                "John Hackock", "Acme Inc.", "1 Main", "Apt A", "San Francisco", "CA",
                "94102", "US", "4158889999", "john@acme.inc");
        String profileTwoGUID = mHelper.setProfile(profile2);
        assertEquals(2, mHelper.getNumberOfProfiles());

        profile.setGUID(profileOneGUID);
        profile.setCountry("Canada");
        mHelper.setProfile(profile);
        assertEquals("Should still have only two profile", 2, mHelper.getNumberOfProfiles());

        AutofillProfile storedProfile = mHelper.getProfile(profileOneGUID);
        assertEquals(profileOneGUID, storedProfile.getGUID());
        assertEquals("https://www.example.com", storedProfile.getOrigin());
        assertEquals("CA", storedProfile.getCountryCode());
        assertEquals("San Francisco", storedProfile.getCity());
        assertNotNull(mHelper.getProfile(profileTwoGUID));
    }

    @SmallTest
    @Feature({"Autofill"})
    public void testAddAndDeleteProfile() throws InterruptedException, ExecutionException {
        AutofillProfile profile = new AutofillProfile(
                "" /* guid */, "Chrome settings" /* origin */,
                "John Smith", "Acme Inc.", "1 Main", "Apt A", "San Francisco", "CA",
                "94102", "US", "4158889999", "john@acme.inc");
        String profileOneGUID = mHelper.setProfile(profile);
        assertEquals(1, mHelper.getNumberOfProfiles());

        mHelper.deleteProfile(profileOneGUID);
        assertEquals(0, mHelper.getNumberOfProfiles());
    }

    @SmallTest
    @Feature({"Autofill"})
    public void testAddAndEditCreditCards() throws InterruptedException, ExecutionException {
        CreditCard card = new CreditCard(
                "" /* guid */, "https://www.example.com" /* origin */,
                "Visa", "1234123412341234", "", "5", "2020");
        String cardOneGUID = mHelper.setCreditCard(card);
        assertEquals(1, mHelper.getNumberOfCreditCards());

        CreditCard card2 = new CreditCard(
                "" /* guid */, "http://www.example.com" /* origin */,
                "American Express", "1234123412341234", "", "8", "2020");
        String cardTwoGUID = mHelper.setCreditCard(card2);
        assertEquals(2, mHelper.getNumberOfCreditCards());

        card.setGUID(cardOneGUID);
        card.setMonth("10");
        card.setNumber("5678567856785678");
        mHelper.setCreditCard(card);
        assertEquals("Should still have only two cards", 2, mHelper.getNumberOfCreditCards());

        CreditCard storedCard = mHelper.getCreditCard(cardOneGUID);
        assertEquals(cardOneGUID, storedCard.getGUID());
        assertEquals("https://www.example.com", storedCard.getOrigin());
        assertEquals("Visa", storedCard.getName());
        assertEquals("10", storedCard.getMonth());
        assertEquals("5678567856785678", storedCard.getNumber());
        assertEquals("************5678", storedCard.getObfuscatedNumber());
        assertNotNull(mHelper.getCreditCard(cardTwoGUID));
    }

    @SmallTest
    @Feature({"Autofill"})
    public void testAddAndDeleteCreditCard() throws InterruptedException, ExecutionException {
        CreditCard card = new CreditCard(
                "" /* guid */, "Chrome settings" /* origin */,
                "Visa", "1234123412341234", "", "5", "2020");
        String cardOneGUID = mHelper.setCreditCard(card);
        assertEquals(1, mHelper.getNumberOfCreditCards());

        mHelper.deleteCreditCard(cardOneGUID);
        assertEquals(0, mHelper.getNumberOfCreditCards());
    }

    @SmallTest
    @Feature({"Autofill"})
    public void testRespectCountryCodes() throws InterruptedException, ExecutionException {
        // The constructor should accept country names and ISO 3166-1-alpha-2 country codes.
        // getCountryCode() should return a country code.
        AutofillProfile profile1 = new AutofillProfile(
                "" /* guid */, "https://www.example.com" /* origin */,
                "John Smith", "Acme Inc.", "1 Main", "Apt A", "Montreal", "Quebec",
                "H3B 2Y5", "Canada", "514-670-1234", "john@acme.inc");
        String profileGuid1 = mHelper.setProfile(profile1);

        AutofillProfile profile2 = new AutofillProfile(
                "" /* guid */, "https://www.example.com" /* origin */,
                "Greg Smith", "Ucme Inc.", "123 Bush", "Apt 125", "Montreal", "Quebec",
                "H3B 2Y5", "CA", "514-670-4321", "greg@ucme.inc");
        String profileGuid2 = mHelper.setProfile(profile2);

        assertEquals(2, mHelper.getNumberOfProfiles());

        AutofillProfile storedProfile1 = mHelper.getProfile(profileGuid1);
        assertEquals("CA", storedProfile1.getCountryCode());

        AutofillProfile storedProfile2 = mHelper.getProfile(profileGuid2);
        assertEquals("CA", storedProfile2.getCountryCode());
    }

}
