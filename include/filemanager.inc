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

#if defined _filemanager_included
 #endinput
#endif
#define _filemanager_included

/**
 * Fast file and directory management.
 *
 * All natives accept UTF-8 paths. Relative paths are resolved against the
 * server directory (the directory that contains srcds), not the game/mod
 * directory. For example, "./left4dead2/addons" points at the addons folder
 * of a normal dedicated server installation.
 *
 * Wildcards are supported in the source path: '*' matches any number of
 * characters, '?' matches a single character and character classes such as
 * "[abc]" or "[a-z]" are accepted. Wildcards may be used in any path
 * component, not only in the final one. As with the Linux shell, a leading
 * dot must be matched explicitly, so "*" does not match ".hidden". Matching is
 * case-insensitive on Windows and case-sensitive on Linux.
 *
 * If a pattern matches both files and directories, all matches are processed.
 * Directories are always processed recursively, including all of their files
 * and subdirectories.
 */
methodmap FileManager
{
	/**
	 * Copies a file or directory.
	 *
	 * If newpath is an existing directory, the source is placed inside it and
	 * keeps its name. Otherwise newpath is the new name of the source, which
	 * allows renaming while copying (for example, "Copy("123.log", "456.txt")").
	 *
	 * If the final destination already exists, force controls whether it is
	 * replaced. Without force the operation fails. Directories are copied
	 * recursively.
	 *
	 * @param oldpath       Source path or wildcard pattern.
	 * @param newpath       Destination path. Must not contain wildcards.
	 * @param force         If true, replace an existing destination.
	 * @return              True on success, false otherwise.
	 *
	 * @note On Linux symbolic links are copied as symbolic links. On Windows
	 *       file symbolic links are copied as their target contents; directory
	 *       reparse points cannot be copied.
	 * @note File permissions and timestamps are not preserved on Linux.
	 */
	public static native bool Copy(const char[] oldpath, const char[] newpath, bool force = false);

	/**
	 * Moves a file or directory.
	 *
	 * If source and destination are on the same volume the move is a rename
	 * and no data is copied. Across volumes the data is copied first and the
	 * source is removed only after a successful copy.
	 *
	 * If newpath is an existing directory, the source is moved into it and
	 * keeps its name. Otherwise newpath is the new name of the source, which
	 * allows renaming while moving (for example, "Move("123.log", "456.txt")").
	 *
	 * @param oldpath       Source path or wildcard pattern.
	 * @param newpath       Destination path. Must not contain wildcards.
	 * @param force         If true, replace an existing destination.
	 * @return              True on success, false otherwise.
	 */
	public static native bool Move(const char[] oldpath, const char[] newpath, bool force = false);

	/**
	 * Deletes a file or directory.
	 *
	 * Directories are deleted recursively. Symbolic links are removed without
	 * touching their target. For safety, filesystem roots and any directory
	 * that contains the server directory (for example "..") are refused.
	 *
	 * @param path          Path or wildcard pattern to delete.
	 * @param force         If true, missing paths are ignored and read-only
	 *                      files are removed as well.
	 * @return              True on success, false otherwise.
	 */
	public static native bool Delete(const char[] path, bool force = false);

	/**
	 * Retrieves the error message produced by the most recent failed operation.
	 *
	 * The message is cleared by the next successful operation, so it should be
	 * read immediately after a native returns false. If no error is available,
	 * the buffer is set to an empty string.
	 *
	 * @param error         Buffer to store the error message.
	 * @param maxlength     Maximum size of the buffer, including the null
	 *                      terminator.
	 * @return              True if an error message was written, false if there
	 *                      was no error.
	 */
	public static native bool GetLastError(char[] error, int maxlength);
}

public Extension __ext_filemanager =
{
	name = "File Manager",
	file = "filemanager.ext",
#if defined AUTOLOAD_EXTENSIONS
	autoload = 1,
#else
	autoload = 0,
#endif
#if defined REQUIRE_EXTENSIONS
	required = 1,
#else
	required = 0,
#endif
};

#if !defined REQUIRE_EXTENSIONS
public void __ext_filemanager_SetNTVOptional()
{
	MarkNativeAsOptional("FileManager.Copy");
	MarkNativeAsOptional("FileManager.Move");
	MarkNativeAsOptional("FileManager.Delete");
	MarkNativeAsOptional("FileManager.GetLastError");
}
#endif
