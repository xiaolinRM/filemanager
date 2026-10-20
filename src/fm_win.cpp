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

#include "fm.h"

#define WIN32_LEAN_AND_MEAN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <cctype>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace fm
{

namespace
{

thread_local bool g_lastRenameCrossDevice = false;

std::wstring Utf8ToWide(const std::string &s)
{
	if (s.empty())
	{
		return std::wstring();
	}

	int length = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, s.data(),
									 static_cast<int>(s.size()), NULL, 0);
	UINT codepage = CP_UTF8;
	DWORD flags = MB_ERR_INVALID_CHARS;
	if (length <= 0)
	{
		// SourcePawn strings are UTF-8, but be forgiving about legacy ANSI
		// strings coming from an old plugin.
		codepage = CP_ACP;
		flags = 0;
		length = MultiByteToWideChar(codepage, flags, s.data(), static_cast<int>(s.size()), NULL, 0);
	}
	if (length <= 0)
	{
		return std::wstring();
	}

	std::wstring out(static_cast<size_t>(length), L'\0');
	MultiByteToWideChar(codepage, flags, s.data(), static_cast<int>(s.size()), &out[0], length);
	return out;
}

std::string WideToUtf8(const wchar_t *s, int length = -1)
{
	if (s == NULL)
	{
		return std::string();
	}
	if (length < 0)
	{
		length = static_cast<int>(wcslen(s));
	}
	if (length == 0)
	{
		return std::string();
	}

	int size = WideCharToMultiByte(CP_UTF8, 0, s, length, NULL, 0, NULL, NULL);
	if (size <= 0)
	{
		return std::string();
	}
	std::string out(static_cast<size_t>(size), '\0');
	WideCharToMultiByte(CP_UTF8, 0, s, length, &out[0], size, NULL, NULL);
	return out;
}

std::string WideToUtf8(const std::wstring &s)
{
	return WideToUtf8(s.c_str(), static_cast<int>(s.size()));
}

/**
 * Converts an internal '/' separated absolute path to a Win32 path. The
 * extended-length prefix removes MAX_PATH limitations and avoids further
 * normalization by the kernel (we already normalized the path ourselves).
 */
std::wstring NativePath(const std::string &path)
{
	std::wstring wide = Utf8ToWide(path);
	if (wide.empty())
	{
		return wide;
	}

	for (size_t i = 0; i < wide.size(); ++i)
	{
		if (wide[i] == L'/')
		{
			wide[i] = L'\\';
		}
	}

	if (wide.size() >= 4 && wide[0] == L'\\' && wide[1] == L'\\' && wide[2] == L'?' && wide[3] == L'\\')
	{
		return wide;
	}
	if (wide.size() >= 2 && wide[0] == L'\\' && wide[1] == L'\\')
	{
		return L"\\\\?\\UNC\\" + wide.substr(2);
	}
	return L"\\\\?\\" + wide;
}

std::wstring GetModulePath()
{
	std::vector<wchar_t> buffer(MAX_PATH);
	for (;;)
	{
		DWORD length = GetModuleFileNameW(NULL, &buffer[0], static_cast<DWORD>(buffer.size()));
		if (length == 0)
		{
			return std::wstring();
		}
		if (length < buffer.size())
		{
			return std::wstring(&buffer[0], length);
		}
		buffer.resize(buffer.size() * 2);
	}
}

std::wstring GetCurrentDirectoryPath()
{
	std::vector<wchar_t> buffer(MAX_PATH);
	for (;;)
	{
		DWORD length = GetCurrentDirectoryW(static_cast<DWORD>(buffer.size()), &buffer[0]);
		if (length == 0)
		{
			return std::wstring();
		}
		if (length < buffer.size())
		{
			return std::wstring(&buffer[0], length);
		}
		buffer.resize(length + 1);
	}
}

void TrimTrailingWhitespace(std::string &s)
{
	while (!s.empty())
	{
		char c = s.back();
		if (c == ' ' || c == '\t' || c == '\r' || c == '\n')
		{
			s.pop_back();
		}
		else
		{
			break;
		}
	}
}

} // namespace

std::string OsErrorText(int code)
{
	std::string result;
	LPWSTR buffer = NULL;
	DWORD length = FormatMessageW(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM |
									  FORMAT_MESSAGE_IGNORE_INSERTS,
								  NULL, static_cast<DWORD>(code),
								  MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
								  reinterpret_cast<LPWSTR>(&buffer), 0, NULL);
	if (length != 0 && buffer != NULL)
	{
		result = WideToUtf8(buffer, static_cast<int>(length));
		LocalFree(buffer);
		TrimTrailingWhitespace(result);
	}

	char suffix[48];
	snprintf(suffix, sizeof(suffix), " (error %d)", code);
	if (result.empty())
	{
		result = "Windows error";
	}
	result += suffix;
	return result;
}

int CurrentProcessId()
{
	return static_cast<int>(GetCurrentProcessId());
}

bool CaseInsensitiveFileNames()
{
	return true;
}

bool GetServerRoot(std::string &out)
{
	std::wstring module = GetModulePath();
	if (module.empty())
	{
		module = GetCurrentDirectoryPath();
	}
	if (module.empty())
	{
		SetError("could not determine the path of the server executable");
		return false;
	}

	size_t slash = module.find_last_of(L"\\/");
	if (slash != std::wstring::npos)
	{
		module.resize(slash);
	}
	if (module.empty())
	{
		SetError("could not determine the server directory");
		return false;
	}

	out = WideToUtf8(module);
	for (size_t i = 0; i < out.size(); ++i)
	{
		if (out[i] == '\\')
		{
			out[i] = '/';
		}
	}

	// Strip a trailing separator, but keep drive roots such as "C:/" intact.
	while (out.size() > 3 && out.back() == '/')
	{
		out.pop_back();
	}
	return true;
}

void NormalizeSeparators(std::string &path)
{
	for (size_t i = 0; i < path.size(); ++i)
	{
		if (path[i] == '\\')
		{
			path[i] = '/';
		}
	}

	// Accept \\?\ and \\?\UNC\ paths from callers.
	if (path.size() >= 4 && path.compare(0, 4, "//?/") == 0)
	{
		path.erase(0, 4);
		if (path.size() >= 4 && _strnicmp(path.c_str(), "UNC/", 4) == 0)
		{
			path = "//" + path.substr(4);
		}
	}
}

bool PathIsAbsolute(const std::string &path)
{
	if (path.size() >= 3 && std::isalpha(static_cast<unsigned char>(path[0])) && path[1] == ':' &&
		path[2] == '/')
	{
		return true;
	}
	if (path.size() >= 2 && path[0] == '/' && path[1] == '/')
	{
		return true;
	}
	return false;
}

bool PathSplitRoot(const std::string &path, std::string &root, size_t &restStart)
{
	if (path.size() >= 3 && std::isalpha(static_cast<unsigned char>(path[0])) && path[1] == ':' &&
		path[2] == '/')
	{
		root = path.substr(0, 3);
		restStart = 3;
		return true;
	}

	if (path.size() >= 2 && path[0] == '/' && path[1] == '/')
	{
		size_t serverEnd = path.find('/', 2);
		if (serverEnd == std::string::npos)
		{
			root = path + "/";
			restStart = path.size();
			return true;
		}

		size_t shareEnd = path.find('/', serverEnd + 1);
		if (shareEnd == std::string::npos)
		{
			root = path + "/";
			restStart = path.size();
			return true;
		}

		root = path.substr(0, shareEnd + 1);
		restStart = shareEnd + 1;
		return true;
	}

	return false;
}

bool IsFileSystemRoot(const std::string &path)
{
	if (path.size() == 3 && std::isalpha(static_cast<unsigned char>(path[0])) && path[1] == ':' &&
		path[2] == '/')
	{
		return true;
	}
	if (path.size() >= 2 && path[0] == '/' && path[1] == '/')
	{
		std::string root;
		size_t start = 0;
		if (PathSplitRoot(path, root, start))
		{
			return path == root;
		}
	}
	return false;
}

bool IsDirectoryForDestination(const std::string &path)
{
	DWORD attributes = GetFileAttributesW(NativePath(path).c_str());
	if (attributes == INVALID_FILE_ATTRIBUTES)
	{
		return false;
	}
	return (attributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
}

FileType GetFileType(const std::string &path)
{
	DWORD attributes = GetFileAttributesW(NativePath(path).c_str());
	if (attributes == INVALID_FILE_ATTRIBUTES)
	{
		return FileType::NotFound;
	}
	if (attributes & FILE_ATTRIBUTE_REPARSE_POINT)
	{
		return FileType::Symlink;
	}
	if (attributes & FILE_ATTRIBUTE_DIRECTORY)
	{
		return FileType::Directory;
	}
	return FileType::File;
}

bool ListDirectory(const std::string &path, std::vector<DirEntry> &entries)
{
	entries.clear();

	std::wstring pattern = NativePath(path);
	if (pattern.empty())
	{
		SetError("invalid directory path: '%s'", path.c_str());
		return false;
	}
	if (pattern.back() != L'\\')
	{
		pattern += L'\\';
	}
	pattern += L'*';

	WIN32_FIND_DATAW data;
	HANDLE handle = FindFirstFileW(pattern.c_str(), &data);
	if (handle == INVALID_HANDLE_VALUE)
	{
		DWORD code = GetLastError();
		SetOsError("FindFirstFileW", path, static_cast<int>(code));
		return false;
	}

	do
	{
		if (wcscmp(data.cFileName, L".") == 0 || wcscmp(data.cFileName, L"..") == 0)
		{
			continue;
		}

		DirEntry entry;
		entry.name = WideToUtf8(data.cFileName);
		if (data.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT)
		{
			entry.type = FileType::Symlink;
		}
		else if (data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
		{
			entry.type = FileType::Directory;
		}
		else
		{
			entry.type = FileType::File;
		}
		entries.push_back(entry);
	} while (FindNextFileW(handle, &data));

	DWORD code = GetLastError();
	FindClose(handle);

	if (code != ERROR_NO_MORE_FILES)
	{
		SetOsError("FindNextFileW", path, static_cast<int>(code));
		return false;
	}
	return true;
}

bool MakeDir(const std::string &path)
{
	const std::wstring native = NativePath(path);
	if (CreateDirectoryW(native.c_str(), NULL))
	{
		return true;
	}

	DWORD code = GetLastError();
	if (code == ERROR_ALREADY_EXISTS)
	{
		if (IsDirectoryForDestination(path))
		{
			return true;
		}
	}

	SetOsError("CreateDirectoryW", path, static_cast<int>(code));
	return false;
}

bool RemoveFile(const std::string &path)
{
	const std::wstring native = NativePath(path);
	if (DeleteFileW(native.c_str()))
	{
		return true;
	}

	DWORD code = GetLastError();
	if (code == ERROR_FILE_NOT_FOUND || code == ERROR_PATH_NOT_FOUND)
	{
		return true;
	}

	// Read-only files and directory reparse points need special handling.
	DWORD attributes = GetFileAttributesW(native.c_str());
	if (attributes != INVALID_FILE_ATTRIBUTES)
	{
		if (attributes & FILE_ATTRIBUTE_READONLY)
		{
			SetFileAttributesW(native.c_str(), attributes & ~FILE_ATTRIBUTE_READONLY);
			if (DeleteFileW(native.c_str()))
			{
				return true;
			}
			code = GetLastError();
			if (code == ERROR_FILE_NOT_FOUND || code == ERROR_PATH_NOT_FOUND)
			{
				return true;
			}
		}
		if (attributes & FILE_ATTRIBUTE_DIRECTORY)
		{
			if (RemoveDirectoryW(native.c_str()))
			{
				return true;
			}
			code = GetLastError();
			if (code == ERROR_FILE_NOT_FOUND || code == ERROR_PATH_NOT_FOUND)
			{
				return true;
			}
		}
	}

	SetOsError("DeleteFileW", path, static_cast<int>(code));
	return false;
}

bool RemoveDir(const std::string &path)
{
	const std::wstring native = NativePath(path);
	if (RemoveDirectoryW(native.c_str()))
	{
		return true;
	}

	DWORD code = GetLastError();
	if (code == ERROR_FILE_NOT_FOUND || code == ERROR_PATH_NOT_FOUND)
	{
		return true;
	}

	if (code == ERROR_ACCESS_DENIED)
	{
		DWORD attributes = GetFileAttributesW(native.c_str());
		if (attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_READONLY))
		{
			SetFileAttributesW(native.c_str(), attributes & ~FILE_ATTRIBUTE_READONLY);
			if (RemoveDirectoryW(native.c_str()))
			{
				return true;
			}
			code = GetLastError();
		}
	}

	SetOsError("RemoveDirectoryW", path, static_cast<int>(code));
	return false;
}

bool RenamePath(const std::string &src, const std::string &dst, bool replace)
{
	g_lastRenameCrossDevice = false;

	DWORD flags = MOVEFILE_COPY_ALLOWED;
	if (replace)
	{
		flags |= MOVEFILE_REPLACE_EXISTING;
	}

	if (MoveFileExW(NativePath(src).c_str(), NativePath(dst).c_str(), flags))
	{
		return true;
	}

	DWORD code = GetLastError();
	if (code == ERROR_NOT_SAME_DEVICE)
	{
		g_lastRenameCrossDevice = true;
	}
	SetOsError("MoveFileExW", src, static_cast<int>(code));
	return false;
}

bool LastRenameWasCrossDevice()
{
	return g_lastRenameCrossDevice;
}

bool CopyFileContents(const std::string &src, const std::string &dst)
{
	const DWORD flags = COPY_FILE_FAIL_IF_EXISTS | COPY_FILE_ALLOW_DECRYPTED_DESTINATION;
	if (CopyFileExW(NativePath(src).c_str(), NativePath(dst).c_str(), NULL, NULL, NULL, flags))
	{
		return true;
	}

	DWORD code = GetLastError();
	SetOsError("CopyFileExW", src, static_cast<int>(code));
	return false;
}

bool CopySymlink(const std::string &src, const std::string &dst)
{
	DWORD attributes = GetFileAttributesW(NativePath(src).c_str());
	if (attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_DIRECTORY))
	{
		SetError("copying directory reparse points is not supported: '%s'", src.c_str());
		return false;
	}

	// CopyFileEx follows file symlinks and copies the target contents.
	return CopyFileContents(src, dst);
}

std::string RealPathOf(const std::string &path)
{
	const std::wstring native = NativePath(path);
	HANDLE handle = CreateFileW(native.c_str(), 0,
								FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL,
								OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, NULL);

	if (handle != INVALID_HANDLE_VALUE)
	{
		std::vector<wchar_t> buffer(MAX_PATH);
		for (;;)
		{
			DWORD length = GetFinalPathNameByHandleW(handle, &buffer[0],
													 static_cast<DWORD>(buffer.size()),
													 FILE_NAME_NORMALIZED | VOLUME_NAME_DOS);
			if (length == 0)
			{
				break;
			}
			if (length < buffer.size())
			{
				std::string result = WideToUtf8(&buffer[0], static_cast<int>(length));
				CloseHandle(handle);
				for (size_t i = 0; i < result.size(); ++i)
				{
					if (result[i] == '\\')
					{
						result[i] = '/';
					}
				}
				if (result.size() >= 4 && result.compare(0, 4, "//?/") == 0)
				{
					result.erase(0, 4);
					if (result.size() >= 4 && _strnicmp(result.c_str(), "UNC/", 4) == 0)
					{
						result = "//" + result.substr(4);
					}
				}
				return result;
			}
			buffer.resize(length + 1);
		}
		CloseHandle(handle);
	}

	std::wstring full;
	DWORD required = GetFullPathNameW(native.c_str(), 0, NULL, NULL);
	if (required != 0)
	{
		std::vector<wchar_t> buffer(required);
		DWORD length = GetFullPathNameW(native.c_str(), required, &buffer[0], NULL);
		if (length != 0 && length < required)
		{
			full.assign(&buffer[0], length);
		}
	}
	if (full.empty())
	{
		return std::string();
	}

	std::string result = WideToUtf8(full);
	NormalizeSeparators(result);
	return result;
}

} // namespace fm
