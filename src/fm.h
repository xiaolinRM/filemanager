/**
 * vim: set ts=4 sw=4 tw=99 noet :
 * =============================================================================
 * File Manager Extension
 * Copyright (C) 2026 File Manager Contributors.  All rights reserved.
 * =============================================================================
 *
 * This program is free software; you can redistribute it and/or modify it under
 * the terms of the GNU General Public License, version 3.0, as published by the
 * Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 * FOR A PARTICULAR PURPOSE.  See the GNU General Public License for more
 * details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef FILEMANAGER_FM_H
#define FILEMANAGER_FM_H

#include <cstddef>
#include <string>
#include <vector>

/**
 * Platform independent file management core.
 *
 * All paths accepted by this module are UTF-8 strings. Internally every path is
 * converted to an absolute, lexically normalized path using '/' as the
 * separator. The Windows backend translates those paths to UTF-16 and to the
 * extended-length form ("\\?\") right before calling the Win32 API.
 */
namespace fm
{

enum class FileType
{
	NotFound,
	File,
	Directory,
	Symlink,
	Other
};

struct DirEntry
{
	std::string name;
	FileType type;
};

/**
 * Initializes the module and caches the server root directory (the directory
 * that contains the srcds executable). Returns false and fills @p error on
 * failure.
 */
bool Initialize(std::string &error);

/** Returns whether Initialize() succeeded. */
bool IsInitialized();

/**
 * Copies or moves one or more sources.
 *
 * Wildcards are expanded in @p oldpath. Relative paths are resolved against the
 * server root. @p newpath must not contain wildcards.
 */
bool Copy(const std::string &oldpath, const std::string &newpath, bool force = false);
bool Move(const std::string &oldpath, const std::string &newpath, bool force = false);

/**
 * Deletes one or more files/directories recursively. Wildcards are supported.
 * With @p force, missing paths are ignored and read-only files are removed.
 */
bool Delete(const std::string &path, bool force = false);

/** Returns the last error message, or an empty string if there is none. */
const char *LastError();

/** Clears the last error message. */
void ClearLastError();

/* ------------------------------------------------------------------------- */
/* Shared helpers used by the platform backends.                              */
/* ------------------------------------------------------------------------- */

void SetError(const char *format, ...);
void SetOsError(const char *operation, const std::string &path, int code);

/** Returns the cached server root (absolute, '/' separated). */
const std::string &ServerRoot();

/**
 * Converts @p input to an absolute, lexically normalized path. Handles "." and
 * ".." components, duplicate separators and relative paths (resolved against
 * the server root). Wildcard characters are preserved.
 */
bool NormalizePath(const std::string &input, std::string &output);

/** Joins two path components with a single '/'. */
std::string JoinPath(const std::string &dir, const std::string &name);

/** Returns the final component (empty for a filesystem root). */
std::string BaseName(const std::string &path);

/** Returns the parent directory. */
std::string ParentPath(const std::string &path);

/** Returns true if @p path contains '*', '?' or '['. */
bool HasWildcard(const std::string &path);

/**
 * Matches a single path component against a wildcard pattern. Supports '*',
 * '?', character classes ("[abc]", "[a-z]", "[!a-z]") and UTF-8 input.
 */
bool WildcardMatch(const char *pattern, const char *name);

/** Case-insensitive string comparison on Windows, case-sensitive elsewhere. */
int ComparePath(const std::string &a, const std::string &b);
bool SamePath(const std::string &a, const std::string &b);
bool IsPathInside(const std::string &parent, const std::string &child);

/** True for "/" (POSIX) or "C:/", "//server/share/" (Windows). */
bool IsFileSystemRoot(const std::string &path);

/* ------------------------------------------------------------------------- */
/* Platform backend.                                                          */
/* ------------------------------------------------------------------------- */

bool GetServerRoot(std::string &out);

/** Returs true if @p path is absolute according to the platform rules. */
bool PathIsAbsolute(const std::string &path);

/**
 * Splits an absolute path into its root prefix (including the trailing '/')
 * and the offset of the first component.
 */
bool PathSplitRoot(const std::string &path, std::string &root, size_t &restStart);

/** Replaces platform specific separators with '/' (no-op on POSIX). */
void NormalizeSeparators(std::string &path);

/** Returns true if @p path is a directory after following symlinks. */
bool IsDirectoryForDestination(const std::string &path);

FileType GetFileType(const std::string &path);
bool ListDirectory(const std::string &path, std::vector<DirEntry> &entries);
bool MakeDir(const std::string &path);
bool RemoveFile(const std::string &path);
bool RemoveDir(const std::string &path);
bool RenamePath(const std::string &src, const std::string &dst, bool replace);
bool CopyFileContents(const std::string &src, const std::string &dst);
bool CopySymlink(const std::string &src, const std::string &dst);

/** True when the last rename failure was caused by a cross-volume move. */
bool LastRenameWasCrossDevice();

/** Resolves symlinks for existing paths; returns an empty string on failure. */
std::string RealPathOf(const std::string &path);

/** Human readable text for an OS error code (errno or GetLastError). */
std::string OsErrorText(int code);

int CurrentProcessId();

/** True when wildcard matching should be case-insensitive. */
bool CaseInsensitiveFileNames();

} // namespace fm

#endif // FILEMANAGER_FM_H
