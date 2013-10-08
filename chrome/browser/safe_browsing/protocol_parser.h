// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_SAFE_BROWSING_PROTOCOL_PARSER_H_
#define CHROME_BROWSER_SAFE_BROWSING_PROTOCOL_PARSER_H_

// Parse the data returned from the chunk response.
//
// Based on the SafeBrowsing v2.1 protocol:
// http://code.google.com/p/google-safe-browsing/wiki/Protocolv2Spec
//
// Read the response from a SafeBrowsing request, and parse into useful pieces.
// The protocol is generally line oriented, but can contain binary data in the
// actual chunk responses. The consumer of the protocol data should instantiate
// the parser and call the appropriate parsing function on the data.
//
// Examples of protocol responses:
//
// 1. List identification
//    i:goog-phish-shavar\n
//    <command>:<command_data>\n
//
// 2. Minimum time to wait (seconds) until the next download request can be made
//    n:1200\n
//    <command>:<time_in_seconds>\n
//
// 3. Redirect URL for retrieving a chunk
//    u:cache.googlevideo.com/safebrowsing/rd/goog-phish-shavar_a_1\n
//    <command>:<url>\n
//
// 4. Add and sub chunks
//   a:1:4:523\n...    <-- Add chunk + binary data
//   s:13:4:17\n...    <-- Sub chunk + binary data
//   <chunk_type>:<chunk_number>:<prefix_len>:<chunk_bytes>\n<binary_data>
//
// 5. Add-del and sub-del requests
//    ad:1-4000,5001\n    <-- Add-del
//    sd:1,3,5,7,903\n    <-- Sub-del
//    <command>:<chunk_range>\n


#include <string>
#include <vector>

#include "base/basictypes.h"
#include "chrome/browser/safe_browsing/chunk_range.h"
#include "chrome/browser/safe_browsing/safe_browsing_util.h"


class SafeBrowsingProtocolParser {
 public:
  SafeBrowsingProtocolParser();

  // Parse the response of an update request. Results for chunk deletions (both
  // add-del and sub-del are returned in 'chunk_deletes', and new chunk URLs to
  // download are contained in 'chunk_urls'. The next time the client is allowed
  // to request another update is returned in 'next_update_sec'. 'reset' will
  // be set to true if the SafeBrowsing service wants us to dump our database.
  // Returns 'true'if it was able to decode the chunk properly, 'false' if not
  // decoded properly and the results should be ignored.
  bool ParseUpdate(const char* chunk_data,
                   int chunk_len,
                   int* next_update_sec,
                   bool* reset,
                   std::vector<SBChunkDelete>* chunk_deletes,
                   std::vector<ChunkUrl>* chunk_urls);

  // Parse the response from a chunk URL request and returns the hosts/prefixes
  // for adds and subs in "chunks".  Returns 'true' on successful parsing,
  // 'false' otherwise. Any result should be ignored when a parse has failed.
  bool ParseChunk(const std::string& list_name,
                  const char* chunk_data,
                  int chunk_len,
                  SBChunkList* chunks);

  // Parse the result of a GetHash request, returning the list of full hashes.
  bool ParseGetHash(const char* chunk_data,
                    int chunk_len,
                    std::vector<SBFullHashResult>* full_hashes);

  // Convert a list of partial hashes into a proper GetHash request.
  void FormatGetHash(const std::vector<SBPrefix>& prefixes,
                     std::string* request);

 private:
  bool ParseAddChunk(const std::string& list_name,
                     const char* data,
                     int data_len,
                     int hash_len,
                     std::deque<SBChunkHost>* hosts);
  bool ParseSubChunk(const std::string& list_name,
                     const char* data,
                     int data_len,
                     int hash_len,
                     std::deque<SBChunkHost>* hosts);

  // Helper functions used by ParseAddChunk and ParseSubChunk.
  static bool ReadHostAndPrefixCount(const char** data,
                                     int* remaining,
                                     SBPrefix* host,
                                     int* count);
  static bool ReadChunkId(const char** data, int* remaining, int* chunk_id);
  static bool ReadPrefixes(
      const char** data, int* remaining, SBEntry* entry, int count);

  // The name of the current list
  std::string list_name_;

  DISALLOW_COPY_AND_ASSIGN(SafeBrowsingProtocolParser);
};


#endif  // CHROME_BROWSER_SAFE_BROWSING_PROTOCOL_PARSER_H_
