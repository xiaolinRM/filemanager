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

#include <algorithm>
#include <cctype>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstring>

namespace fm
{

namespace
{

const int kMaxDepth = 256;
const size_t kErrorBufferSize = 1024;

std::string g_serverRoot;
thread_local char g_lastError[kErrorBufferSize] = { 0 };

#ifdef _WIN32
bool IsAsciiAlpha(char c)
{
	return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
}
#endif

char AsciiLower(char c)
{
	if (c >= 'A' && c <= 'Z')
	{
		return static_cast<char>(c - 'A' + 'a');
	}
	return c;
}

/** Decodes one UTF-8 code point. Invalid sequences are returned byte by byte. */
uint32_t DecodeUtf8(const char *&p)
{
	const unsigned char c = static_cast<unsigned char>(*p);
	if (c < 0x80)
	{
		++p;
		return c;
	}

	int extra = 0;
	uint32_t cp = 0;
	if ((c & 0xE0) == 0xC0)
	{
		extra = 1;
		cp = c & 0x1F;
	}
	else if ((c & 0xF0) == 0xE0)
	{
		extra = 2;
		cp = c & 0x0F;
	}
	else if ((c & 0xF8) == 0xF0)
	{
		extra = 3;
		cp = c & 0x07;
	}
	else
	{
		++p;
		return c;
	}

	for (int i = 1; i <= extra; ++i)
	{
		const unsigned char cc = static_cast<unsigned char>(p[i]);
		if ((cc & 0xC0) != 0x80)
		{
			++p;
			return c;
		}
		cp = (cp << 6) | (cc & 0x3F);
	}

	p += extra + 1;
	return cp;
}

bool CharEqual(uint32_t a, uint32_t b)
{
	if (a == b)
	{
		return true;
	}
	if (CaseInsensitiveFileNames() && a < 0x80 && b < 0x80)
	{
		return AsciiLower(static_cast<char>(a)) == AsciiLower(static_cast<char>(b));
	}
	return false;
}

bool MatchClass(const char *&p, uint32_t c)
{
	// p points at '['.
	++p;

	bool negate = false;
	if (*p == '!' || *p == '^')
	{
		negate = true;
		++p;
	}

	bool matched = false;
	bool first = true;
	while (*p != '\0' && (*p != ']' || first))
	{
		first = false;
		uint32_t lo = DecodeUtf8(p);
		uint32_t hi = lo;
		if (*p == '-' && p[1] != '\0' && p[1] != ']')
		{
			++p;
			hi = DecodeUtf8(p);
		}
		if (c >= lo && c <= hi)
		{
			matched = true;
		}
	}

	if (*p == ']')
	{
		++p;
	}

	return negate ? !matched : matched;
}

bool MatchImpl(const char *p, const char *s)
{
	while (*p != '\0')
	{
		if (*p == '*')
		{
			while (*p == '*')
			{
				++p;
			}
			if (*p == '\0')
			{
				return true;
			}
			const char *t = s;
			for (;;)
			{
				if (MatchImpl(p, t))
				{
					return true;
				}
				if (*t == '\0')
				{
					return false;
				}
				DecodeUtf8(t);
			}
		}

		if (*p == '?')
		{
			if (*s == '\0')
			{
				return false;
			}
			++p;
			DecodeUtf8(s);
			continue;
		}

		if (*p == '[')
		{
			if (*s == '\0')
			{
				return false;
			}
			uint32_t sc = DecodeUtf8(s);
			if (!MatchClass(p, sc))
			{
				return false;
			}
			continue;
		}

		uint32_t pc = DecodeUtf8(p);
		if (*s == '\0')
		{
			return false;
		}
		uint32_t sc = DecodeUtf8(s);
		if (!CharEqual(pc, sc))
		{
			return false;
		}
	}

	return *s == '\0';
}

int ComparePrefix(const std::string &value, const std::string &prefix)
{
	for (size_t i = 0; i < prefix.size(); ++i)
	{
		char a = value[i];
		char b = prefix[i];
		if (CaseInsensitiveFileNames())
		{
			a = AsciiLower(a);
			b = AsciiLower(b);
		}
		if (a != b)
		{
			return static_cast<unsigned char>(a) < static_cast<unsigned char>(b) ? -1 : 1;
		}
	}
	return 0;
}

void SplitComponents(const std::string &path, size_t start, std::vector<std::string> &out)
{
	out.clear();
	size_t pos = start;
	while (pos < path.size())
	{
		size_t slash = path.find('/', pos);
		size_t end = (slash == std::string::npos) ? path.size() : slash;
		std::string comp = path.substr(pos, end - pos);
		pos = (slash == std::string::npos) ? path.size() : slash + 1;

		if (comp.empty() || comp == ".")
		{
			continue;
		}
		if (comp == "..")
		{
			if (!out.empty())
			{
				out.pop_back();
			}
			continue;
		}
		out.push_back(comp);
	}
}

bool ExpandPattern(const std::string &raw, std::vector<std::string> &out, bool *noMatch)
{
	out.clear();
	if (noMatch)
	{
		*noMatch = false;
	}

	std::string pattern;
	if (!NormalizePath(raw, pattern))
	{
		return false;
	}

	if (!HasWildcard(pattern))
	{
		if (GetFileType(pattern) == FileType::NotFound)
		{
			SetError("source path not found: '%s'", pattern.c_str());
			if (noMatch)
			{
				*noMatch = true;
			}
			return false;
		}
		out.push_back(pattern);
		return true;
	}

	std::string root;
	size_t start = 0;
	if (!PathSplitRoot(pattern, root, start))
	{
		SetError("invalid path: '%s'", pattern.c_str());
		return false;
	}

	std::vector<std::string> components;
	SplitComponents(pattern, start, components);

	std::vector<std::string> current;
	current.push_back(root);

	for (size_t i = 0; i < components.size(); ++i)
	{
		const std::string &comp = components[i];
		std::vector<std::string> next;

		if (!HasWildcard(comp))
		{
			for (size_t j = 0; j < current.size(); ++j)
			{
				next.push_back(JoinPath(current[j], comp));
			}
		}
		else
		{
			for (size_t j = 0; j < current.size(); ++j)
			{
				if (!IsDirectoryForDestination(current[j]))
				{
					continue;
				}

				std::vector<DirEntry> entries;
				if (!ListDirectory(current[j], entries))
				{
					// A missing or unreadable intermediate directory simply
					// produces no matches; the error is discarded below if
					// other branches still match.
					continue;
				}

				for (size_t k = 0; k < entries.size(); ++k)
				{
					if (WildcardMatch(comp.c_str(), entries[k].name.c_str()))
					{
						next.push_back(JoinPath(current[j], entries[k].name));
					}
				}
			}
		}

		current.swap(next);
		if (current.empty())
		{
			break;
		}
	}

	for (size_t i = 0; i < current.size(); ++i)
	{
		if (GetFileType(current[i]) != FileType::NotFound)
		{
			out.push_back(current[i]);
		}
	}

	if (out.empty())
	{
		SetError("no files matched pattern: '%s'", pattern.c_str());
		if (noMatch)
		{
			*noMatch = true;
		}
		return false;
	}

	std::sort(out.begin(), out.end(), [](const std::string &a, const std::string &b) {
		int cmp = ComparePath(a, b);
		if (cmp != 0)
		{
			return cmp < 0;
		}
		return a < b;
	});

	ClearLastError();
	return true;
}

bool IsDestinationInsideSource(const std::string &src, const std::string &dst)
{
	if (IsPathInside(src, dst))
	{
		return true;
	}

	std::string realSrc = RealPathOf(src);
	std::string realParent = RealPathOf(ParentPath(dst));
	if (!realSrc.empty() && !realParent.empty())
	{
		if (SamePath(realSrc, realParent) || IsPathInside(realSrc, realParent))
		{
			return true;
		}
	}
	return false;
}

bool ResolveDestination(const std::string &rawNew, const std::vector<std::string> &sources,
						std::vector<std::string> &dests)
{
	dests.clear();

	if (HasWildcard(rawNew))
	{
		SetError("destination path must not contain wildcards: '%s'", rawNew.c_str());
		return false;
	}

	std::string separatorCheck = rawNew;
	NormalizeSeparators(separatorCheck);
	const bool trailingSeparator = !separatorCheck.empty() && separatorCheck.back() == '/';

	std::string dest;
	if (!NormalizePath(rawNew, dest))
	{
		return false;
	}

	const bool destIsDir = IsDirectoryForDestination(dest);
	if (trailingSeparator && !destIsDir)
	{
		SetError("destination directory does not exist: '%s'", dest.c_str());
		return false;
	}

	const bool multiple = sources.size() > 1;
	if (multiple && !destIsDir)
	{
		SetError("destination must be an existing directory when copying or moving multiple files: '%s'",
				 dest.c_str());
		return false;
	}

	dests.reserve(sources.size());
	for (size_t i = 0; i < sources.size(); ++i)
	{
		const std::string &src = sources[i];
		std::string dst;

		if (multiple || destIsDir)
		{
			std::string base = BaseName(src);
			if (base.empty())
			{
				SetError("cannot move or copy a filesystem root: '%s'", src.c_str());
				return false;
			}
			dst = JoinPath(dest, base);
		}
		else
		{
			dst = dest;
		}

		if (SamePath(src, dst))
		{
			SetError("source and destination are the same: '%s'", src.c_str());
			return false;
		}

		if (GetFileType(src) == FileType::Directory && IsDestinationInsideSource(src, dst))
		{
			SetError("cannot copy or move a directory into itself: '%s' -> '%s'", src.c_str(), dst.c_str());
			return false;
		}

		dests.push_back(dst);
	}

	return true;
}

bool CopyEntry(const std::string &src, const std::string &dst, FileType type, bool force,
			   int depth, bool checkDestination);

bool RemoveEntry(const std::string &path, FileType type, bool force, int depth)
{
	if (depth > kMaxDepth)
	{
		SetError("maximum directory depth exceeded at '%s'", path.c_str());
		return false;
	}

	if (type == FileType::NotFound)
	{
		if (force)
		{
			return true;
		}
		SetError("path not found: '%s'", path.c_str());
		return false;
	}

	if (type == FileType::Directory)
	{
		std::vector<DirEntry> entries;
		if (!ListDirectory(path, entries))
		{
			return false;
		}

		bool ok = true;
		for (size_t i = 0; i < entries.size(); ++i)
		{
			FileType childType = entries[i].type;
			std::string childPath = JoinPath(path, entries[i].name);
			if (childType == FileType::Other || childType == FileType::NotFound)
			{
				childType = GetFileType(childPath);
			}
			if (!RemoveEntry(childPath, childType, true, depth + 1))
			{
				ok = false;
			}
		}

		if (!RemoveDir(path))
		{
			return false;
		}
		return ok;
	}

	return RemoveFile(path);
}

bool RemoveTree(const std::string &path, bool force, int depth)
{
	FileType type = GetFileType(path);
	return RemoveEntry(path, type, force, depth);
}

bool MakeTempSibling(const std::string &dst, std::string &out)
{
	static unsigned long counter = 0;
	char suffix[96];

	for (int i = 0; i < 100; ++i)
	{
		++counter;
		snprintf(suffix, sizeof(suffix), ".fm.%d.%lu.tmp", CurrentProcessId(), counter);
		out = dst + suffix;
		if (GetFileType(out) == FileType::NotFound)
		{
			return true;
		}
	}

	SetError("could not create a temporary path for '%s'", dst.c_str());
	return false;
}

bool MoveNoReplace(const std::string &src, const std::string &dst, FileType type, int depth)
{
	if (RenamePath(src, dst, false))
	{
		return true;
	}

	if (!LastRenameWasCrossDevice())
	{
		return false;
	}

	// Cross-volume move: copy first, delete the source only after a successful
	// copy. This is what "mv" does too.
	if (!CopyEntry(src, dst, type, false, depth, false))
	{
		RemoveTree(dst, true, depth + 1);
		return false;
	}

	if (!RemoveTree(src, true, depth + 1))
	{
		SetError("moved '%s' to '%s' but could not remove the source", src.c_str(), dst.c_str());
		return false;
	}
	return true;
}

bool MoveEntry(const std::string &src, const std::string &dst, bool force, int depth)
{
	if (depth > kMaxDepth)
	{
		SetError("maximum directory depth exceeded at '%s'", src.c_str());
		return false;
	}

	FileType srcType = GetFileType(src);
	if (srcType == FileType::NotFound)
	{
		SetError("source path not found: '%s'", src.c_str());
		return false;
	}

	FileType dstType = GetFileType(dst);

	if (dstType != FileType::NotFound && !force)
	{
		SetError("destination already exists: '%s'", dst.c_str());
		return false;
	}

	if (dstType != FileType::NotFound)
	{
		if (srcType == FileType::File && dstType == FileType::File)
		{
			// Fast path: atomically replace the destination file.
			if (RenamePath(src, dst, true))
			{
				return true;
			}
			if (!LastRenameWasCrossDevice())
			{
				return false;
			}
			if (!RemoveTree(dst, true, depth + 1))
			{
				return false;
			}
			return MoveNoReplace(src, dst, srcType, depth);
		}

		// Move the existing destination out of the way first so that a directory
		// can replace a file, a file can replace a directory, and a directory can
		// replace a directory. If the actual move fails, put it back.
		std::string backup;
		if (!MakeTempSibling(dst, backup))
		{
			return false;
		}
		if (!RenamePath(dst, backup, false))
		{
			return false;
		}
		if (!MoveNoReplace(src, dst, srcType, depth))
		{
			RenamePath(backup, dst, false);
			return false;
		}
		// The old destination is no longer needed. Failure to remove it is not
		// fatal for the move itself.
		RemoveTree(backup, true, depth + 1);
		return true;
	}

	return MoveNoReplace(src, dst, srcType, depth);
}

bool CopyEntry(const std::string &src, const std::string &dst, FileType type, bool force,
			   int depth, bool checkDestination)
{
	if (depth > kMaxDepth)
	{
		SetError("maximum directory depth exceeded at '%s'", src.c_str());
		return false;
	}

	if (type == FileType::NotFound)
	{
		SetError("source path not found: '%s'", src.c_str());
		return false;
	}
	if (type == FileType::Other)
	{
		SetError("unsupported file type (device, fifo or socket): '%s'", src.c_str());
		return false;
	}

	if (checkDestination)
	{
		FileType dstType = GetFileType(dst);
		if (dstType != FileType::NotFound)
		{
			if (!force)
			{
				SetError("destination already exists: '%s'", dst.c_str());
				return false;
			}
			if (!RemoveTree(dst, true, depth + 1))
			{
				return false;
			}
		}
	}

	if (type == FileType::Directory)
	{
		if (!MakeDir(dst))
		{
			return false;
		}

		std::vector<DirEntry> entries;
		if (!ListDirectory(src, entries))
		{
			return false;
		}

		bool ok = true;
		for (size_t i = 0; i < entries.size(); ++i)
		{
			FileType childType = entries[i].type;
			std::string childSrc = JoinPath(src, entries[i].name);
			std::string childDst = JoinPath(dst, entries[i].name);
			if (childType == FileType::Other || childType == FileType::NotFound)
			{
				childType = GetFileType(childSrc);
			}
			if (!CopyEntry(childSrc, childDst, childType, true, depth + 1, false))
			{
				ok = false;
			}
		}
		return ok;
	}

	if (type == FileType::Symlink)
	{
		return CopySymlink(src, dst);
	}

	return CopyFileContents(src, dst);
}

bool IsProtectedPath(const std::string &path)
{
	if (IsFileSystemRoot(path))
	{
		return true;
	}
	if (g_serverRoot.empty())
	{
		return true;
	}
	if (SamePath(path, g_serverRoot))
	{
		return true;
	}
	// Refuse to delete an ancestor of the server directory (for example "..").
	if (IsPathInside(path, g_serverRoot))
	{
		return true;
	}

	std::string real = RealPathOf(path);
	if (!real.empty())
	{
		if (SamePath(real, g_serverRoot) || IsPathInside(real, g_serverRoot))
		{
			return true;
		}
	}
	return false;
}

} // namespace

void SetError(const char *format, ...)
{
	va_list ap;
	va_start(ap, format);
	vsnprintf(g_lastError, sizeof(g_lastError), format, ap);
	va_end(ap);
	g_lastError[sizeof(g_lastError) - 1] = '\0';
}

void SetOsError(const char *operation, const std::string &path, int code)
{
	std::string text = OsErrorText(code);
	SetError("%s failed for '%s': %s", operation, path.c_str(), text.c_str());
}

const char *LastError()
{
	return g_lastError;
}

void ClearLastError()
{
	g_lastError[0] = '\0';
}

const std::string &ServerRoot()
{
	return g_serverRoot;
}

bool IsInitialized()
{
	return !g_serverRoot.empty();
}

bool Initialize(std::string &error)
{
	ClearLastError();

	std::string root;
	if (!GetServerRoot(root))
	{
		error = g_lastError;
		if (error.empty())
		{
			error = "could not determine the server directory";
		}
		return false;
	}
	if (root.empty())
	{
		error = "could not determine the server directory";
		return false;
	}

	g_serverRoot = root;

	if (GetFileType(g_serverRoot) != FileType::Directory)
	{
		error = "server directory does not exist: " + g_serverRoot;
		return false;
	}

	return true;
}

bool NormalizePath(const std::string &input, std::string &output)
{
	std::string s = input;
	NormalizeSeparators(s);

	if (s.empty())
	{
		s = ".";
	}

	// Drive-relative paths such as "C:foo" are ambiguous and not supported.
#ifdef _WIN32
	if (s.size() >= 2 && IsAsciiAlpha(s[0]) && s[1] == ':' && (s.size() == 2 || s[2] != '/'))
	{
		SetError("drive-relative paths are not supported: '%s'", input.c_str());
		return false;
	}
#endif

	if (!PathIsAbsolute(s))
	{
		if (g_serverRoot.empty())
		{
			SetError("the server directory has not been initialized");
			return false;
		}
		s = JoinPath(g_serverRoot, s);
	}

	std::string root;
	size_t start = 0;
	if (!PathSplitRoot(s, root, start))
	{
		SetError("invalid absolute path: '%s'", input.c_str());
		return false;
	}

	std::vector<std::string> components;
	SplitComponents(s, start, components);

	output = root;
	if (!components.empty())
	{
		if (output.empty() || output.back() != '/')
		{
			output += '/';
		}
		for (size_t i = 0; i < components.size(); ++i)
		{
			if (i != 0)
			{
				output += '/';
			}
			output += components[i];
		}
	}

	return true;
}

std::string JoinPath(const std::string &dir, const std::string &name)
{
	if (dir.empty())
	{
		return name;
	}
	if (name.empty())
	{
		return dir;
	}
	if (dir.back() == '/')
	{
		return dir + name;
	}
	return dir + "/" + name;
}

std::string BaseName(const std::string &path)
{
	size_t end = path.size();
	while (end > 0 && path[end - 1] == '/')
	{
		--end;
	}
	if (end == 0)
	{
		return std::string();
	}

	size_t slash = path.rfind('/', end - 1);
	if (slash == std::string::npos)
	{
		return path.substr(0, end);
	}
	return path.substr(slash + 1, end - slash - 1);
}

std::string ParentPath(const std::string &path)
{
	if (IsFileSystemRoot(path))
	{
		return path;
	}

	size_t end = path.size();
	while (end > 0 && path[end - 1] == '/')
	{
		--end;
	}
	if (end == 0)
	{
		return std::string("/");
	}

	size_t slash = path.rfind('/', end - 1);
	if (slash == std::string::npos)
	{
		return std::string();
	}
	if (slash == 0)
	{
		return std::string("/");
	}
	return path.substr(0, slash + 1);
}

bool HasWildcard(const std::string &path)
{
	return path.find_first_of("*?[") != std::string::npos;
}

bool WildcardMatch(const char *pattern, const char *name)
{
	// Match the shell rule: a leading dot must be matched explicitly.
	if (name[0] == '.' && pattern[0] != '.')
	{
		return false;
	}
	return MatchImpl(pattern, name);
}

int ComparePath(const std::string &a, const std::string &b)
{
	if (!CaseInsensitiveFileNames())
	{
		return a.compare(b);
	}

	const size_t len = a.size() < b.size() ? a.size() : b.size();
	for (size_t i = 0; i < len; ++i)
	{
		char ca = AsciiLower(a[i]);
		char cb = AsciiLower(b[i]);
		if (ca != cb)
		{
			return static_cast<unsigned char>(ca) < static_cast<unsigned char>(cb) ? -1 : 1;
		}
	}
	if (a.size() == b.size())
	{
		return 0;
	}
	return a.size() < b.size() ? -1 : 1;
}

bool SamePath(const std::string &a, const std::string &b)
{
	return ComparePath(a, b) == 0;
}

bool IsPathInside(const std::string &parent, const std::string &child)
{
	if (parent.empty() || child.empty())
	{
		return false;
	}
	if (SamePath(parent, child))
	{
		return false;
	}
	if (child.size() <= parent.size())
	{
		return false;
	}
	if (ComparePrefix(child, parent) != 0)
	{
		return false;
	}

	const char next = child[parent.size()];
	return parent.back() == '/' || next == '/';
}

bool Copy(const std::string &oldpath, const std::string &newpath, bool force)
{
	ClearLastError();

	std::vector<std::string> sources;
	if (!ExpandPattern(oldpath, sources, 0))
	{
		return false;
	}

	std::vector<std::string> dests;
	if (!ResolveDestination(newpath, sources, dests))
	{
		return false;
	}

	bool ok = true;
	for (size_t i = 0; i < sources.size(); ++i)
	{
		if (IsFileSystemRoot(sources[i]))
		{
			SetError("refusing to copy a filesystem root: '%s'", sources[i].c_str());
			ok = false;
			continue;
		}
		if (!CopyEntry(sources[i], dests[i], GetFileType(sources[i]), force, 0, true))
		{
			ok = false;
		}
	}

	if (ok)
	{
		ClearLastError();
	}
	return ok;
}

bool Move(const std::string &oldpath, const std::string &newpath, bool force)
{
	ClearLastError();

	std::vector<std::string> sources;
	if (!ExpandPattern(oldpath, sources, 0))
	{
		return false;
	}

	std::vector<std::string> dests;
	if (!ResolveDestination(newpath, sources, dests))
	{
		return false;
	}

	bool ok = true;
	for (size_t i = 0; i < sources.size(); ++i)
	{
		if (IsFileSystemRoot(sources[i]))
		{
			SetError("refusing to move a filesystem root: '%s'", sources[i].c_str());
			ok = false;
			continue;
		}
		if (!MoveEntry(sources[i], dests[i], force, 0))
		{
			ok = false;
		}
	}

	if (ok)
	{
		ClearLastError();
	}
	return ok;
}

bool Delete(const std::string &path, bool force)
{
	ClearLastError();

	std::vector<std::string> sources;
	bool noMatch = false;
	if (!ExpandPattern(path, sources, &noMatch))
	{
		if (force && noMatch)
		{
			ClearLastError();
			return true;
		}
		return false;
	}

	bool ok = true;
	for (size_t i = 0; i < sources.size(); ++i)
	{
		if (IsProtectedPath(sources[i]))
		{
			SetError("refusing to delete a protected path: '%s'", sources[i].c_str());
			ok = false;
			continue;
		}
		if (!RemoveTree(sources[i], force, 0))
		{
			ok = false;
		}
	}

	if (ok)
	{
		ClearLastError();
	}
	return ok;
}

} // namespace fm
