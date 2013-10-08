/* Copyright (c) 2012 The Chromium Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "handlers.h"

#include <arpa/inet.h>
#include <assert.h>
#include <errno.h>
#include <netdb.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <sys/types.h>
#include <sys/socket.h>
#include <sys/stat.h>

#include "nacl_io/osdirent.h"

#include "nacl_io_demo.h"

#define MAX_OPEN_FILES 10
#define MAX_OPEN_DIRS 10

#if defined(WIN32)
#define stat _stat
#endif

/**
 * A mapping from int -> FILE*, so the JavaScript messages can refer to an open
 * File. */
static FILE* g_OpenFiles[MAX_OPEN_FILES];

/**
 * A mapping from int -> DIR*, so the JavaScript messages can refer to an open
 * Directory. */
static void* g_OpenDirs[MAX_OPEN_DIRS];

/**
 * Add |object| to |map| and return the index it was added at.
 * @param[in] map The map to add the object to.
 * @param[in] max_map_size The maximum map size.
 * @param[in] object The object to add to the map.
 * @return int The index of the added object, or -1 if there is no more space.
 */
static int AddToMap(void** map, int max_map_size, void* object) {
  int i;
  assert(object != NULL);
  for (i = 0; i < max_map_size; ++i) {
    if (map[i] == NULL) {
      map[i] = object;
      return i;
    }
  }

  return -1;
}

/**
 * Remove an object at index |i| from |map|.
 * @param[in] map The map to remove from.
 * @param[in] max_map_size The size of the map.
 * @param[in] i The index to remove.
 */
static void RemoveFromMap(void** map, int max_map_size, int i) {
  assert(i >= 0 && i < max_map_size);
  map[i] = NULL;
}

/**
 * Get the object from |map| at index |i|.
 * @param[in] map The map to access.
 * @param[in] max_map_size The size of the map.
 * @param[in] i The index to access.
 * @return the object at |map|. This will be NULL if there is no object at |i|.
 */
static void* GetFromMap(void** map, int max_map_size, int i) {
  assert(i >= 0 && i < max_map_size);
  return map[i];
}

/**
 * Get an object given a string |s| containing the index.
 * @param[in] map The map to access.
 * @param[in] max_map_size The size of the map.
 * @param[in] s The string containing the object index.
 * @param[out] index The index of the object as an int.
 * @return The object, or NULL if the index is invalid.
 */
static void* GetFromIndexString(void** map,
                                int max_map_size,
                                const char* s,
                                int* index) {
  char* endptr;
  int result = strtol(s, &endptr, 10);
  if (endptr != s + strlen(s)) {
    /* Garbage at the end of the number...? */
    return NULL;
  }

  if (index)
    *index = result;

  return GetFromMap(map, max_map_size, result);
}

/**
 * Add the file to the g_OpenFiles map.
 * @param[in] file The file to add to g_OpenFiles.
 * @return int The index of the FILE in g_OpenFiles, or -1 if there are too many
 *     open files. */
static int AddFileToMap(FILE* file) {
  return AddToMap((void**)g_OpenFiles, MAX_OPEN_FILES, file);
}

/**
 * Remove the file from the g_OpenFiles map.
 * @param[in] i The index of the file handle to remove. */
static void RemoveFileFromMap(int i) {
  RemoveFromMap((void**)g_OpenFiles, MAX_OPEN_FILES, i);
}

/**
 * Get a file, given a string containing the index.
 * @param[in] s The string containing the file index.
 * @param[out] file_index The index of this file.
 * @return The FILE* for this file, or NULL if the index is invalid.
 */
static FILE* GetFileFromIndexString(const char* s, int* file_index) {
  return (FILE*)GetFromIndexString(
      (void**)g_OpenFiles, MAX_OPEN_FILES, s, file_index);
}

/* Win32 doesn't support DIR/opendir/readdir/closedir. */
#if !defined(WIN32)
/**
 * Add the dir to the g_OpenDirs map.
 * @param[in] dir The dir to add to g_OpenDirs.
 * @return int The index of the DIR in g_OpenDirs, or -1 if there are too many
 *     open dirs. */
static int AddDirToMap(DIR* dir) {
  return AddToMap((void**)g_OpenDirs, MAX_OPEN_DIRS, dir);
}

/**
 * Remove the dir from the g_OpenDirs map.
 * @param[in] i The index of the dir handle to remove. */
static void RemoveDirFromMap(int i) {
  RemoveFromMap((void**)g_OpenDirs, MAX_OPEN_DIRS, i);
}

/**
 * Get a dir, given a string containing the index.
 * @param[in] s The string containing the dir index.
 * @param[out] dir_index The index of this dir.
 * @return The DIR* for this dir, or NULL if the index is invalid.
 */
static DIR* GetDirFromIndexString(const char* s, int* dir_index) {
  return (DIR*)GetFromIndexString(
      (void**)g_OpenDirs, MAX_OPEN_DIRS, s, dir_index);
}
#endif

/**
 * Handle a call to fopen() made by JavaScript.
 *
 * fopen expects 2 parameters:
 *   0: the path of the file to open
 *   1: the mode string
 * on success, fopen returns a result in |output| separated by \1:
 *   0: "fopen"
 *   1: the filename opened
 *   2: the file index
 * on failure, fopen returns an error string in |output|.
 *
 * @param[in] num_params The number of params in |params|.
 * @param[in] params An array of strings, parameters to this function.
 * @param[out] output A string to write informational function output to.
 * @return An errorcode; 0 means success, anything else is a failure. */
int HandleFopen(int num_params, char** params, char** output) {
  FILE* file;
  int file_index;
  const char* filename;
  const char* mode;

  if (num_params != 2) {
    *output = PrintfToNewString("Error: fopen takes 2 parameters.");
    return 1;
  }

  filename = params[0];
  mode = params[1];

  file = fopen(filename, mode);
  if (!file) {
    *output = PrintfToNewString("Error: fopen returned a NULL FILE*.");
    return 2;
  }

  file_index = AddFileToMap(file);
  if (file_index == -1) {
    *output = PrintfToNewString(
        "Error: Example only allows %d open file handles.", MAX_OPEN_FILES);
    return 3;
  }

  *output = PrintfToNewString("fopen\1%s\1%d", filename, file_index);
  return 0;
}

/**
 * Handle a call to fwrite() made by JavaScript.
 *
 * fwrite expects 2 parameters:
 *   0: The index of the file (which is mapped to a FILE*)
 *   1: A string to write to the file
 * on success, fwrite returns a result in |output| separated by \1:
 *   0: "fwrite"
 *   1: the file index
 *   2: the number of bytes written
 * on failure, fwrite returns an error string in |output|.
 *
 * @param[in] num_params The number of params in |params|.
 * @param[in] params An array of strings, parameters to this function.
 * @param[out] output A string to write informational function output to.
 * @return An errorcode; 0 means success, anything else is a failure. */
int HandleFwrite(int num_params, char** params, char** output) {
  FILE* file;
  const char* file_index_string;
  const char* data;
  size_t data_len;
  size_t bytes_written;

  if (num_params != 2) {
    *output = PrintfToNewString("Error: fwrite takes 2 parameters.");
    return 1;
  }

  file_index_string = params[0];
  file = GetFileFromIndexString(file_index_string, NULL);
  data = params[1];
  data_len = strlen(data);

  if (!file) {
    *output =
        PrintfToNewString("Error: Unknown file handle %s.", file_index_string);
    return 2;
  }

  bytes_written = fwrite(data, 1, data_len, file);

  if (ferror(file)) {
    *output = PrintfToNewString(
        "Error: Wrote %d bytes, but ferror() returns true.", bytes_written);
    return 3;
  }

  *output =
      PrintfToNewString("fwrite\1%s\1%d", file_index_string, bytes_written);
  return 0;
}

/**
 * Handle a call to fread() made by JavaScript.
 *
 * fread expects 2 parameters:
 *   0: The index of the file (which is mapped to a FILE*)
 *   1: The number of bytes to read from the file.
 * on success, fread returns a result in |output| separated by \1:
 *   0: "fread"
 *   1: the file index
 *   2: the data read from the file
 * on failure, fread returns an error string in |output|.
 *
 * @param[in] num_params The number of params in |params|.
 * @param[in] params An array of strings, parameters to this function.
 * @param[out] output A string to write informational function output to.
 * @return An errorcode; 0 means success, anything else is a failure. */
int HandleFread(int num_params, char** params, char** output) {
  FILE* file;
  const char* file_index_string;
  char* buffer;
  size_t data_len;
  size_t bytes_read;

  if (num_params != 2) {
    *output = PrintfToNewString("Error: fread takes 2 parameters.");
    return 1;
  }

  file_index_string = params[0];
  file = GetFileFromIndexString(file_index_string, NULL);
  data_len = strtol(params[1], NULL, 10);

  if (!file) {
    *output =
        PrintfToNewString("Error: Unknown file handle %s.", file_index_string);
    return 2;
  }

  buffer = (char*)malloc(data_len + 1);
  bytes_read = fread(buffer, 1, data_len, file);
  buffer[bytes_read] = 0;

  if (ferror(file)) {
    *output = PrintfToNewString(
        "Error: Read %d bytes, but ferror() returns true.", bytes_read);
    return 3;
  }

  *output = PrintfToNewString("fread\1%s\1%s", file_index_string, buffer);
  free(buffer);
  return 0;
}

/**
 * Handle a call to fseek() made by JavaScript.
 *
 * fseek expects 3 parameters:
 *   0: The index of the file (which is mapped to a FILE*)
 *   1: The offset to seek to
 *   2: An integer representing the whence parameter of standard fseek.
 *      whence = 0: seek from the beginning of the file
 *      whence = 1: seek from the current file position
 *      whence = 2: seek from the end of the file
 * on success, fseek returns a result in |output| separated by \1:
 *   0: "fseek"
 *   1: the file index
 *   2: The new file position
 * on failure, fseek returns an error string in |output|.
 *
 * @param[in] num_params The number of params in |params|.
 * @param[in] params An array of strings, parameters to this function.
 * @param[out] output A string to write informational function output to.
 * @return An errorcode; 0 means success, anything else is a failure. */
int HandleFseek(int num_params, char** params, char** output) {
  FILE* file;
  const char* file_index_string;
  long offset;
  int whence;
  int result;

  if (num_params != 3) {
    *output = PrintfToNewString("Error: fseek takes 3 parameters.");
    return 1;
  }

  file_index_string = params[0];
  file = GetFileFromIndexString(file_index_string, NULL);
  offset = strtol(params[1], NULL, 10);
  whence = strtol(params[2], NULL, 10);

  if (!file) {
    *output =
        PrintfToNewString("Error: Unknown file handle %s.", file_index_string);
    return 2;
  }

  result = fseek(file, offset, whence);
  if (result) {
    *output = PrintfToNewString("Error: fseek returned error %d.", result);
    return 3;
  }

  offset = ftell(file);
  if (offset < 0) {
    *output = PrintfToNewString(
        "Error: fseek succeeded, but ftell returned error %d.", offset);
    return 4;
  }

  *output = PrintfToNewString("fseek\1%s\1%d", file_index_string, offset);
  return 0;
}

/**
 * Handle a call to fclose() made by JavaScript.
 *
 * fclose expects 1 parameter:
 *   0: The index of the file (which is mapped to a FILE*)
 * on success, fclose returns a result in |output| separated by \1:
 *   0: "fclose"
 *   1: the file index
 * on failure, fclose returns an error string in |output|.
 *
 * @param[in] num_params The number of params in |params|.
 * @param[in] params An array of strings, parameters to this function.
 * @param[out] output A string to write informational function output to.
 * @return An errorcode; 0 means success, anything else is a failure. */
int HandleFclose(int num_params, char** params, char** output) {
  FILE* file;
  int file_index;
  const char* file_index_string;
  int result;

  if (num_params != 1) {
    *output = PrintfToNewString("Error: fclose takes 1 parameters.");
    return 1;
  }

  file_index_string = params[0];
  file = GetFileFromIndexString(file_index_string, &file_index);
  if (!file) {
    *output =
        PrintfToNewString("Error: Unknown file handle %s.", file_index_string);
    return 2;
  }

  result = fclose(file);
  if (result) {
    *output = PrintfToNewString("Error: fclose returned error %d.", result);
    return 3;
  }

  RemoveFileFromMap(file_index);

  *output = PrintfToNewString("fclose\1%s", file_index_string);
  return 0;
}

/**
 * Handle a call to stat() made by JavaScript.
 *
 * stat expects 1 parameter:
 *   0: The name of the file
 * on success, stat returns a result in |output| separated by \1:
 *   0: "stat"
 *   1: the file name
 *   2: the size of the file
 * on failure, stat returns an error string in |output|.
 *
 * @param[in] num_params The number of params in |params|.
 * @param[in] params An array of strings, parameters to this function.
 * @param[out] output A string to write informational function output to.
 * @return An errorcode; 0 means success, anything else is a failure. */
int HandleStat(int num_params, char** params, char** output) {
  const char* filename;
  int result;
  struct stat buf;

  if (num_params != 1) {
    *output = PrintfToNewString("Error: stat takes 1 parameter.");
    return 1;
  }

  filename = params[0];

  memset(&buf, 0, sizeof(buf));
  result = stat(filename, &buf);
  if (result == -1) {
    *output = PrintfToNewString("Error: stat returned error %d.", errno);
    return 2;
  }

  *output = PrintfToNewString("stat\1%s\1%d", filename, buf.st_size);
  return 0;
}

/**
 * Handle a call to opendir() made by JavaScript.
 *
 * opendir expects 1 parameter:
 *   0: The name of the directory
 * on success, opendir returns a result in |output| separated by \1:
 *   0: "opendir"
 *   1: the directory name
 *   2: the index of the directory
 * on failure, opendir returns an error string in |output|.
 *
 * @param[in] num_params The number of params in |params|.
 * @param[in] params An array of strings, parameters to this function.
 * @param[out] output A string to write informational function output to.
 * @return An errorcode; 0 means success, anything else is a failure. */
int HandleOpendir(int num_params, char** params, char** output) {
#if defined(WIN32)
  *output = PrintfToNewString("Error: Win32 does not support opendir.");
  return 1;
#else
  DIR* dir;
  int dir_index;
  const char* dirname;

  if (num_params != 1) {
    *output = PrintfToNewString("Error: opendir takes 1 parameter.");
    return 1;
  }

  dirname = params[0];

  dir = opendir(dirname);
  if (!dir) {
    *output = PrintfToNewString("Error: opendir returned a NULL DIR*.");
    return 2;
  }

  dir_index = AddDirToMap(dir);
  if (dir_index == -1) {
    *output = PrintfToNewString(
        "Error: Example only allows %d open dir handles.", MAX_OPEN_DIRS);
    return 3;
  }

  *output = PrintfToNewString("opendir\1%s\1%d", dirname, dir_index);
  return 0;
#endif
}

/**
 * Handle a call to readdir() made by JavaScript.
 *
 * readdir expects 1 parameter:
 *   0: The index of the directory (which is mapped to a DIR*)
 * on success, opendir returns a result in |output| separated by \1:
 *   0: "readdir"
 *   1: the inode number of the entry
 *   2: the name of the entry
 * on failure, readdir returns an error string in |output|.
 *
 * @param[in] num_params The number of params in |params|.
 * @param[in] params An array of strings, parameters to this function.
 * @param[out] output A string to write informational function output to.
 * @return An errorcode; 0 means success, anything else is a failure. */
int HandleReaddir(int num_params, char** params, char** output) {
#if defined(WIN32)
  *output = PrintfToNewString("Error: Win32 does not support readdir.");
  return 1;
#else
  DIR* dir;
  const char* dir_index_string;
  struct dirent* entry;

  if (num_params != 1) {
    *output = PrintfToNewString("Error: readdir takes 1 parameter.");
    return 1;
  }

  dir_index_string = params[0];
  dir = GetDirFromIndexString(dir_index_string, NULL);

  if (!dir) {
    *output = PrintfToNewString("Error: Unknown dir handle %s.",
                                dir_index_string);
    return 2;
  }

  entry = readdir(dir);
  if (entry != NULL) {
    *output = PrintfToNewString("readdir\1%s\1%d\1%s", dir_index_string,
                                entry->d_ino, entry->d_name);
  } else {
    *output = PrintfToNewString("readdir\1%s\1\1", dir_index_string);
  }

  return 0;
#endif
}

/**
 * Handle a call to closedir() made by JavaScript.
 *
 * closedir expects 1 parameter:
 *   0: The index of the directory (which is mapped to a DIR*)
 * on success, closedir returns a result in |output| separated by \1:
 *   0: "closedir"
 *   1: the name of the directory
 * on failure, closedir returns an error string in |output|.
 *
 * @param[in] num_params The number of params in |params|.
 * @param[in] params An array of strings, parameters to this function.
 * @param[out] output A string to write informational function output to.
 * @return An errorcode; 0 means success, anything else is a failure. */
int HandleClosedir(int num_params, char** params, char** output) {
#if defined(WIN32)
  *output = PrintfToNewString("Error: Win32 does not support closedir.");
  return 1;
#else
  DIR* dir;
  int dir_index;
  const char* dir_index_string;
  int result;

  if (num_params != 1) {
    *output = PrintfToNewString("Error: closedir takes 1 parameters.");
    return 1;
  }

  dir_index_string = params[0];
  dir = GetDirFromIndexString(dir_index_string, &dir_index);
  if (!dir) {
    *output = PrintfToNewString("Error: Unknown dir handle %s.",
                                dir_index_string);
    return 2;
  }

  result = closedir(dir);
  if (result) {
    *output = PrintfToNewString("Error: closedir returned error %d.", result);
    return 3;
  }

  RemoveDirFromMap(dir_index);

  *output = PrintfToNewString("closedir\1%s", dir_index_string);
  return 0;
#endif
}

/**
 * Handle a call to mkdir() made by JavaScript.
 *
 * mkdir expects 1 parameter:
 *   0: The name of the directory
 *   1: The mode to use for the new directory, in octal.
 * on success, mkdir returns a result in |output| separated by \1:
 *   0: "mkdir"
 *   1: the name of the directory
 * on failure, mkdir returns an error string in |output|.
 *
 * @param[in] num_params The number of params in |params|.
 * @param[in] params An array of strings, parameters to this function.
 * @param[out] output A string to write informational function output to.
 * @return An errorcode; 0 means success, anything else is a failure. */
int HandleMkdir(int num_params, char** params, char** output) {
  const char* dirname;
  int result;
  int mode;

  if (num_params != 2) {
    *output = PrintfToNewString("Error: mkdir takes 2 parameters.");
    return 1;
  }

  dirname = params[0];
  mode = strtol(params[1], NULL, 8);

  result = mkdir(dirname, mode);
  if (result != 0) {
    *output = PrintfToNewString("Error: mkdir returned error: %d", errno);
    return 2;
  }

  *output = PrintfToNewString("mkdir\1%s", dirname);
  return 0;
}

/**
 * Handle a call to gethostbyname() made by JavaScript.
 *
 * gethostbyname expects 1 parameter:
 *   0: The name of the host to look up.
 * on success, gethostbyname returns a result in |output| separated by \1:
 *   0: "gethostbyname"
 *   1: Host name
 *   2: Address type (either "AF_INET" or "AF_INET6")
 *   3. The first address.
 *   4+ The second, third, etc. addresses.
 * on failure, gethostbyname returns an error string in |output|.
 *
 * @param[in] num_params The number of params in |params|.
 * @param[in] params An array of strings, parameters to this function.
 * @param[out] output A string to write informational function output to.
 * @return An errorcode; 0 means success, anything else is a failure. */
int HandleGethostbyname(int num_params, char** params, char** output) {
  struct hostent* info;
  struct in_addr **addr_list;
  const char* addr_type;
  const char* name;
  char inet6_addr_str[INET6_ADDRSTRLEN];
  int non_variable_len, output_len;
  int current_pos;
  int i;

  if (num_params != 1) {
    *output = PrintfToNewString("Error: gethostbyname takes 1 parameter.");
    return 1;
  }

  name = params[0];

  info = gethostbyname(name);
  if (!info) {
    *output = PrintfToNewString("Error: gethostbyname failed, error is \"%s\"",
                                hstrerror(h_errno));
    return 2;
  }

  addr_type = info->h_addrtype == AF_INET ? "AF_INET" : "AF_INET6";

  non_variable_len = strlen("gethostbyname") + 1
    + strlen(info->h_name) + 1 + strlen(addr_type);
  output_len = non_variable_len;

  addr_list = (struct in_addr **)info->h_addr_list;
  for (i = 0; addr_list[i] != NULL; i++) {
    output_len += 1; // for the divider
    if (info->h_addrtype == AF_INET) {
      output_len += strlen(inet_ntoa(*addr_list[i]));
    } else { // IPv6
      inet_ntop(AF_INET6, addr_list[i], inet6_addr_str, INET6_ADDRSTRLEN);
      output_len += strlen(inet6_addr_str);
    }
  }

  *output = (char*) calloc(output_len + 1, 1);
  if (!*output) {
    *output = PrintfToNewString("Error: out of memory.");
    return 3;
  }
  snprintf(*output, non_variable_len + 1, "gethostbyname\1%s\1%s",
           info->h_name, addr_type);

  current_pos = non_variable_len;
  for (i = 0; addr_list[i] != NULL; i++) {
    if (info->h_addrtype == AF_INET) {
      current_pos += sprintf(*output + current_pos,
                             "\1%s", inet_ntoa(*addr_list[i]));
    } else { // IPv6
      inet_ntop(AF_INET6, addr_list[i], inet6_addr_str, INET6_ADDRSTRLEN);
      sprintf(*output + current_pos, "\1%s", inet6_addr_str);
    }
  }
  return 0;
}
