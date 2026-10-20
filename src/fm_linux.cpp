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
 *
 * Compatibility notes:
 *  - This file intentionally avoids stat()/lstat()/fstat(). On glibc 2.33 and
 *    newer those symbols are versioned as GLIBC_2.33, which would prevent the
 *    extension from loading on older distributions. File types are determined
 *    with readlink(), open(O_DIRECTORY) and open() instead.
 *  - The binary is linked with -static-libstdc++ and -static-libgcc so that no
 *    libstdc++ version is required at runtime.
 */

#include "fm.h"

#include <cerrno>
#include <climits>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include <dirent.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <sys/sendfile.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <sys/types.h>
#include <unistd.h>

#ifdef __linux__
#include <linux/fs.h>
#endif

#ifndef O_CLOEXEC
#define O_CLOEXEC 0
#endif
#ifndef O_DIRECTORY
#define O_DIRECTORY 0
#endif
#ifndef DT_UNKNOWN
#define DT_UNKNOWN 0
#define DT_FIFO 1
#define DT_CHR 2
#define DT_DIR 4
#define DT_BLK 6
#define DT_REG 8
#define DT_LNK 10
#define DT_SOCK 12
#endif

namespace fm
{

namespace
{

const size_t kCopyChunk = 1024 * 1024;
thread_local bool g_lastRenameCrossDevice = false;

bool ReadLink(const std::string &path, std::string &target)
{
	size_t size = 256;
	for (;;)
	{
		std::vector<char> buffer(size);
		ssize_t length = readlink(path.c_str(), &buffer[0], size);
		if (length < 0)
		{
			SetOsError("readlink", path, errno);
			return false;
		}
		if (static_cast<size_t>(length) < size)
		{
			target.assign(&buffer[0], static_cast<size_t>(length));
			return true;
		}
		if (size >= 65536)
		{
			SetError("symbolic link target is too long: '%s'", path.c_str());
			return false;
		}
		size *= 2;
	}
}

bool WriteAll(int fd, const char *data, size_t length)
{
	size_t offset = 0;
	while (offset < length)
	{
		ssize_t written = write(fd, data + offset, length - offset);
		if (written > 0)
		{
			offset += static_cast<size_t>(written);
			continue;
		}
		if (written < 0 && errno == EINTR)
		{
			continue;
		}
		return false;
	}
	return true;
}

} // namespace

std::string OsErrorText(int code)
{
	char buffer[256];
	buffer[0] = '\0';

	std::string text;
#if defined(__GLIBC__) && defined(_GNU_SOURCE)
	const char *message = strerror_r(code, buffer, sizeof(buffer));
	if (message != NULL)
	{
		text = message;
	}
#else
	if (strerror_r(code, buffer, sizeof(buffer)) == 0)
	{
		text = buffer;
	}
#endif

	if (text.empty())
	{
		text = "system error";
	}
	char suffix[48];
	snprintf(suffix, sizeof(suffix), " (errno %d)", code);
	text += suffix;
	return text;
}

int CurrentProcessId()
{
	return static_cast<int>(getpid());
}

bool CaseInsensitiveFileNames()
{
	return false;
}

bool GetServerRoot(std::string &out)
{
	std::vector<char> buffer(4096);
	for (;;)
	{
		ssize_t length = readlink("/proc/self/exe", &buffer[0], buffer.size() - 1);
		if (length > 0)
		{
			if (static_cast<size_t>(length) < buffer.size() - 1)
			{
				buffer[static_cast<size_t>(length)] = '\0';
				out.assign(&buffer[0], static_cast<size_t>(length));
				break;
			}
			buffer.resize(buffer.size() * 2);
			continue;
		}
		break;
	}

	if (out.empty())
	{
		char cwd[4096];
		if (getcwd(cwd, sizeof(cwd)) == NULL)
		{
			SetOsError("getcwd", std::string("."), errno);
			return false;
		}
		out = cwd;
	}

	size_t slash = out.find_last_of('/');
	if (slash != std::string::npos)
	{
		out.resize(slash);
	}
	if (out.empty())
	{
		out = "/";
	}

	while (out.size() > 1 && out.back() == '/')
	{
		out.pop_back();
	}
	return true;
}

void NormalizeSeparators(std::string &path)
{
	(void)path;
}

bool PathIsAbsolute(const std::string &path)
{
	return !path.empty() && path[0] == '/';
}

bool PathSplitRoot(const std::string &path, std::string &root, size_t &restStart)
{
	if (path.empty() || path[0] != '/')
	{
		return false;
	}
	root = "/";
	restStart = 1;
	return true;
}

bool IsFileSystemRoot(const std::string &path)
{
	return path == "/";
}

bool IsDirectoryForDestination(const std::string &path)
{
	int fd = open(path.c_str(), O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NONBLOCK);
	if (fd >= 0)
	{
		close(fd);
		return true;
	}
	return false;
}

FileType GetFileType(const std::string &path)
{
	char byte = 0;
	if (readlink(path.c_str(), &byte, 1) >= 0)
	{
		return FileType::Symlink;
	}

	int directory = open(path.c_str(), O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NONBLOCK);
	if (directory >= 0)
	{
		close(directory);
		return FileType::Directory;
	}
	int code = errno;
	if (code == ENOENT)
	{
		return FileType::NotFound;
	}

	int file = open(path.c_str(), O_RDONLY | O_NONBLOCK | O_CLOEXEC);
	if (file >= 0)
	{
		close(file);
		return FileType::File;
	}
	code = errno;
	if (code == ENOENT || code == ENOTDIR)
	{
		return FileType::NotFound;
	}
	if (code == EACCES)
	{
		// A permission problem should be reported by the actual operation
		// rather than as a missing file.
		return FileType::File;
	}
	return FileType::Other;
}

bool ListDirectory(const std::string &path, std::vector<DirEntry> &entries)
{
	entries.clear();

	DIR *dir = opendir(path.c_str());
	if (dir == NULL)
	{
		SetOsError("opendir", path, errno);
		return false;
	}

	int code = 0;
	for (;;)
	{
		errno = 0;
		struct dirent *entry = readdir(dir);
		if (entry == NULL)
		{
			code = errno;
			break;
		}
		if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0)
		{
			continue;
		}

		DirEntry item;
		item.name = entry->d_name;

		switch (entry->d_type)
		{
			case DT_DIR:
				item.type = FileType::Directory;
				break;
			case DT_REG:
				item.type = FileType::File;
				break;
			case DT_LNK:
				item.type = FileType::Symlink;
				break;
			case DT_UNKNOWN:
				item.type = GetFileType(JoinPath(path, item.name));
				break;
			default:
				item.type = FileType::Other;
				break;
		}
		entries.push_back(item);
	}

	closedir(dir);
	if (code != 0)
	{
		SetOsError("readdir", path, code);
		return false;
	}
	return true;
}

bool MakeDir(const std::string &path)
{
	if (mkdir(path.c_str(), 0755) == 0)
	{
		return true;
	}

	int code = errno;
	if (code == EEXIST && IsDirectoryForDestination(path))
	{
		return true;
	}
	SetOsError("mkdir", path, code);
	return false;
}

bool RemoveFile(const std::string &path)
{
	if (unlink(path.c_str()) == 0)
	{
		return true;
	}

	int code = errno;
	if (code == ENOENT)
	{
		return true;
	}
	if (code == EISDIR)
	{
		if (rmdir(path.c_str()) == 0)
		{
			return true;
		}
		code = errno;
	}
	SetOsError("unlink", path, code);
	return false;
}

bool RemoveDir(const std::string &path)
{
	if (rmdir(path.c_str()) == 0)
	{
		return true;
	}

	int code = errno;
	if (code == ENOENT)
	{
		return true;
	}
	SetOsError("rmdir", path, code);
	return false;
}

bool RenamePath(const std::string &src, const std::string &dst, bool replace)
{
	(void)replace;
	g_lastRenameCrossDevice = false;

	if (rename(src.c_str(), dst.c_str()) == 0)
	{
		return true;
	}

	int code = errno;
	if (code == EXDEV)
	{
		g_lastRenameCrossDevice = true;
	}
	SetOsError("rename", src, code);
	return false;
}

bool LastRenameWasCrossDevice()
{
	return g_lastRenameCrossDevice;
}

bool CopyFileContents(const std::string &src, const std::string &dst)
{
	int input = open(src.c_str(), O_RDONLY | O_NONBLOCK | O_CLOEXEC);
	if (input < 0)
	{
		SetOsError("open", src, errno);
		return false;
	}

	mode_t mode = (access(src.c_str(), X_OK) == 0) ? 0755 : 0644;
	int output = open(dst.c_str(), O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC, mode);
	if (output < 0)
	{
		int code = errno;
		close(input);
		SetOsError("open", dst, code);
		return false;
	}

	bool copied = false;
	bool ok = true;

#ifdef FICLONE
	// Reflink: instantaneous on btrfs/xfs and harmless where unsupported.
	if (ioctl(output, FICLONE, input) == 0)
	{
		copied = true;
	}
#endif

#ifdef SYS_copy_file_range
	if (!copied)
	{
		for (;;)
		{
			ssize_t count = syscall(SYS_copy_file_range, input, NULL, output, NULL, kCopyChunk, 0u);
			if (count > 0)
			{
				continue;
			}
			if (count == 0)
			{
				copied = true;
				break;
			}

			int code = errno;
			if (code == EINTR)
			{
				continue;
			}
			if (code == ENOSYS || code == EXDEV || code == EINVAL || code == EOPNOTSUPP ||
				code == EPERM)
			{
				break;
			}
			SetOsError("copy_file_range", src, code);
			ok = false;
			break;
		}
	}
#endif

	if (ok && !copied)
	{
		for (;;)
		{
			ssize_t count = sendfile(output, input, NULL, kCopyChunk);
			if (count > 0)
			{
				continue;
			}
			if (count == 0)
			{
				copied = true;
				break;
			}

			int code = errno;
			if (code == EINTR)
			{
				continue;
			}
			if (code == EINVAL || code == ENOSYS || code == EOPNOTSUPP || code == EXDEV)
			{
				break;
			}
			SetOsError("sendfile", src, code);
			ok = false;
			break;
		}
	}

	if (ok && !copied)
	{
		char *buffer = static_cast<char *>(malloc(kCopyChunk));
		if (buffer == NULL)
		{
			SetError("out of memory while copying '%s'", src.c_str());
			ok = false;
		}
		else
		{
			for (;;)
			{
				ssize_t count = read(input, buffer, kCopyChunk);
				if (count > 0)
				{
					if (!WriteAll(output, buffer, static_cast<size_t>(count)))
					{
						SetOsError("write", dst, errno);
						ok = false;
						break;
					}
					continue;
				}
				if (count == 0)
				{
					copied = true;
					break;
				}
				if (errno == EINTR)
				{
					continue;
				}
				SetOsError("read", src, errno);
				ok = false;
				break;
			}
			free(buffer);
		}
	}

	if (close(output) != 0 && ok)
	{
		SetOsError("close", dst, errno);
		ok = false;
	}
	close(input);

	if (!ok || !copied)
	{
		if (ok && !copied)
		{
			SetError("could not copy '%s': no data was transferred", src.c_str());
		}
		unlink(dst.c_str());
		return false;
	}
	return true;
}

bool CopySymlink(const std::string &src, const std::string &dst)
{
	std::string target;
	if (!ReadLink(src, target))
	{
		return false;
	}

	if (symlink(target.c_str(), dst.c_str()) != 0)
	{
		SetOsError("symlink", dst, errno);
		return false;
	}
	return true;
}

std::string RealPathOf(const std::string &path)
{
	char buffer[4096];
	if (realpath(path.c_str(), buffer) == NULL)
	{
		return std::string();
	}
	return std::string(buffer);
}

} // namespace fm
