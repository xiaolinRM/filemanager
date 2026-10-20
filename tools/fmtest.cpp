/**
 * File Manager test harness.
 *
 * This program exercises the platform independent core directly (without
 * SourceMod) so that file operations can be validated in a disposable
 * directory. It intentionally refuses to run from a directory that looks like
 * a game server installation.
 */

#include "../src/fm.h"

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#ifdef _WIN32
#include <direct.h>
#include <windows.h>
#else
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#endif

namespace
{

int g_failures = 0;
int g_checks = 0;

void Check(bool condition, const char *name, const std::string &detail = std::string())
{
	++g_checks;
	if (condition)
	{
		printf("[PASS] %s\n", name);
	}
	else
	{
		++g_failures;
		printf("[FAIL] %s%s%s\n", name, detail.empty() ? "" : " - ", detail.c_str());
	}
}

std::string Utf8FromWide(const wchar_t *value)
{
#ifdef _WIN32
	int length = WideCharToMultiByte(CP_UTF8, 0, value, -1, NULL, 0, NULL, NULL);
	if (length <= 1)
	{
		return std::string();
	}
	std::string out(static_cast<size_t>(length - 1), '\0');
	WideCharToMultiByte(CP_UTF8, 0, value, -1, &out[0], length, NULL, NULL);
	return out;
#else
	(void)value;
	return std::string();
#endif
}

std::string ExeDirectory()
{
#ifdef _WIN32
	wchar_t buffer[32768];
	DWORD length = GetModuleFileNameW(NULL, buffer, 32768);
	if (length == 0)
	{
		return std::string();
	}
	std::string path = Utf8FromWide(std::wstring(buffer, length).c_str());
	size_t slash = path.find_last_of("/\\");
	if (slash != std::string::npos)
	{
		path.resize(slash);
	}
	return path;
#else
	char buffer[4096];
	ssize_t length = readlink("/proc/self/exe", buffer, sizeof(buffer) - 1);
	if (length <= 0)
	{
		return std::string();
	}
	buffer[length] = '\0';
	std::string path(buffer);
	size_t slash = path.find_last_of('/');
	if (slash != std::string::npos)
	{
		path.resize(slash);
	}
	return path;
#endif
}

bool MakeDirRaw(const std::string &path)
{
#ifdef _WIN32
	return _mkdir(path.c_str()) == 0;
#else
	return mkdir(path.c_str(), 0755) == 0;
#endif
}

bool FileExistsRaw(const std::string &path)
{
#ifdef _WIN32
	DWORD attributes = GetFileAttributesA(path.c_str());
	return attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_DIRECTORY) == 0;
#else
	struct stat st;
	return stat(path.c_str(), &st) == 0 && S_ISREG(st.st_mode);
#endif
}

bool DirExistsRaw(const std::string &path)
{
#ifdef _WIN32
	DWORD attributes = GetFileAttributesA(path.c_str());
	return attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
#else
	struct stat st;
	return stat(path.c_str(), &st) == 0 && S_ISDIR(st.st_mode);
#endif
}

bool SetReadOnly(const std::string &path)
{
#ifdef _WIN32
	DWORD attributes = GetFileAttributesA(path.c_str());
	if (attributes == INVALID_FILE_ATTRIBUTES)
	{
		return false;
	}
	return SetFileAttributesA(path.c_str(), attributes | FILE_ATTRIBUTE_READONLY) != 0;
#else
	return chmod(path.c_str(), 0444) == 0;
#endif
}

bool WriteFileRaw(const std::string &path, const std::string &data)
{
	FILE *file = fopen(path.c_str(), "wb");
	if (file == NULL)
	{
		return false;
	}
	bool ok = data.empty() || fwrite(data.data(), 1, data.size(), file) == data.size();
	fclose(file);
	return ok;
}

std::string ReadFileRaw(const std::string &path)
{
	FILE *file = fopen(path.c_str(), "rb");
	if (file == NULL)
	{
		return std::string();
	}
	std::string data;
	char buffer[4096];
	size_t length = 0;
	while ((length = fread(buffer, 1, sizeof(buffer), file)) > 0)
	{
		data.append(buffer, length);
	}
	fclose(file);
	return data;
}

#ifdef _WIN32
std::wstring WideFromUtf8(const std::string &value)
{
	int length = MultiByteToWideChar(CP_UTF8, 0, value.c_str(), -1, NULL, 0);
	if (length <= 1)
	{
		return std::wstring();
	}
	std::wstring out(static_cast<size_t>(length - 1), L'\0');
	MultiByteToWideChar(CP_UTF8, 0, value.c_str(), -1, &out[0], length);
	return out;
}

bool WriteFileUtf8(const std::string &path, const std::string &data)
{
	std::wstring wide = WideFromUtf8(path);
	FILE *file = _wfopen(wide.c_str(), L"wb");
	if (file == NULL)
	{
		return false;
	}
	bool ok = data.empty() || fwrite(data.data(), 1, data.size(), file) == data.size();
	fclose(file);
	return ok;
}

std::string ReadFileUtf8(const std::string &path)
{
	std::wstring wide = WideFromUtf8(path);
	FILE *file = _wfopen(wide.c_str(), L"rb");
	if (file == NULL)
	{
		return std::string();
	}
	std::string data;
	char buffer[4096];
	size_t length = 0;
	while ((length = fread(buffer, 1, sizeof(buffer), file)) > 0)
	{
		data.append(buffer, length);
	}
	fclose(file);
	return data;
}
#else
bool WriteFileUtf8(const std::string &path, const std::string &data)
{
	return WriteFileRaw(path, data);
}

std::string ReadFileUtf8(const std::string &path)
{
	return ReadFileRaw(path);
}
#endif

std::string LastErrorText()
{
	const char *error = fm::LastError();
	return error == NULL ? std::string() : std::string(error);
}

void PrintError(const char *test)
{
	std::string error = LastErrorText();
	if (!error.empty())
	{
		printf("       error: %s\n", error.c_str());
	}
	(void)test;
}

bool CreateLargeFile(const std::string &path, size_t megabytes)
{
	FILE *file = fopen(path.c_str(), "wb");
	if (file == NULL)
	{
		return false;
	}
	std::vector<char> buffer(1024 * 1024, 'Z');
	bool ok = true;
	for (size_t i = 0; i < megabytes; ++i)
	{
		if (fwrite(buffer.data(), 1, buffer.size(), file) != buffer.size())
		{
			ok = false;
			break;
		}
	}
	fclose(file);
	return ok;
}

bool IsServerDirectory(const std::string &path)
{
#ifdef _WIN32
	return FileExistsRaw(path + "/srcds.exe");
#else
	return FileExistsRaw(path + "/srcds_linux") || FileExistsRaw(path + "/srcds_run");
#endif
}

} // namespace

int main(int argc, char **argv)
{
	const std::string exeDir = ExeDirectory();
	if (exeDir.empty())
	{
		fprintf(stderr, "Could not determine the executable directory.\n");
		return 2;
	}

	if (IsServerDirectory(exeDir))
	{
		fprintf(stderr, "Refusing to run inside a game server directory (%s).\n", exeDir.c_str());
		fprintf(stderr, "Run this harness from a disposable directory instead.\n");
		return 2;
	}

#ifdef _WIN32
	_wchdir(WideFromUtf8(exeDir).c_str());
#else
	if (chdir(exeDir.c_str()) != 0)
	{
		fprintf(stderr, "chdir failed\n");
		return 2;
	}
#endif

	std::string initError;
	if (!fm::Initialize(initError))
	{
		fprintf(stderr, "fm::Initialize failed: %s\n", initError.c_str());
		return 2;
	}

	printf("Server root: %s\n", fm::ServerRoot().c_str());

	// Clean any leftovers from a previous run.
	fm::Delete("./sandbox", true);

	Check(MakeDirRaw("./sandbox"), "create sandbox");
	Check(MakeDirRaw("./sandbox/src"), "create sandbox/src");
	Check(MakeDirRaw("./sandbox/src/sub"), "create sandbox/src/sub");
	Check(MakeDirRaw("./sandbox/src/dir"), "create sandbox/src/dir");
	Check(MakeDirRaw("./sandbox/dst"), "create sandbox/dst");
	Check(MakeDirRaw("./sandbox/dst/wild"), "create sandbox/dst/wild");
	Check(MakeDirRaw("./sandbox/dst/cls"), "create sandbox/dst/cls");
	Check(MakeDirRaw("./sandbox/mix"), "create sandbox/mix");
	Check(MakeDirRaw("./sandbox/mix/sub"), "create sandbox/mix/sub");
	Check(MakeDirRaw("./sandbox/out"), "create sandbox/out");
	Check(MakeDirRaw("./sandbox/out2"), "create sandbox/out2");

	Check(WriteFileRaw("./sandbox/src/a.txt", "AAA"), "write a.txt");
	Check(WriteFileRaw("./sandbox/src/b.txt", "BBB"), "write b.txt");
	Check(WriteFileRaw("./sandbox/src/c.log", "CCC"), "write c.log");
	Check(WriteFileRaw("./sandbox/src/sub/d.txt", "DDD"), "write d.txt");
	Check(WriteFileRaw("./sandbox/src/dir/e.bin", "EEE"), "write e.bin");
	Check(WriteFileRaw("./sandbox/src/.hidden.txt", "HID"), "write .hidden.txt");
	Check(WriteFileRaw("./sandbox/mix/file.txt", "MIX"), "write mix/file.txt");
	Check(WriteFileRaw("./sandbox/mix/sub/f.txt", "SUBF"), "write mix/sub/f.txt");

	// Basic copy and rename.
	Check(fm::Copy("./sandbox/src/a.txt", "./sandbox/dst/a_copy.txt"), "copy file");
	Check(ReadFileRaw("./sandbox/dst/a_copy.txt") == "AAA", "copied file content");
	Check(LastErrorText().empty(), "no error after successful copy");

	// Existing destination without force.
	Check(!fm::Copy("./sandbox/src/a.txt", "./sandbox/dst/a_copy.txt"), "copy over existing without force fails");
	Check(!LastErrorText().empty(), "error message set on failure");
	PrintError("copy over existing");
	Check(ReadFileRaw("./sandbox/dst/a_copy.txt") == "AAA", "failed copy left destination unchanged");

	// Force overwrite.
	Check(WriteFileRaw("./sandbox/src/a.txt", "AAAA"), "update a.txt");
	Check(fm::Copy("./sandbox/src/a.txt", "./sandbox/dst/a_copy.txt", true), "force copy over existing");
	Check(ReadFileRaw("./sandbox/dst/a_copy.txt") == "AAAA", "force copy replaced content");

	// Recursive directory copy.
	Check(fm::Copy("./sandbox/src", "./sandbox/dst/srccopy"), "copy directory recursively");
	Check(ReadFileRaw("./sandbox/dst/srccopy/a.txt") == "AAAA", "directory copy file");
	Check(ReadFileRaw("./sandbox/dst/srccopy/sub/d.txt") == "DDD", "directory copy nested file");
	Check(ReadFileRaw("./sandbox/dst/srccopy/dir/e.bin") == "EEE", "directory copy nested dir file");

	// Wildcard copy.
	Check(fm::Copy("./sandbox/src/*.txt", "./sandbox/dst/wild"), "wildcard copy into directory");
	Check(FileExistsRaw("./sandbox/dst/wild/a.txt"), "wildcard copied a.txt");
	Check(FileExistsRaw("./sandbox/dst/wild/b.txt"), "wildcard copied b.txt");
	Check(!FileExistsRaw("./sandbox/dst/wild/c.log"), "wildcard did not copy c.log");
	Check(!FileExistsRaw("./sandbox/dst/wild/.hidden.txt"), "wildcard skipped dotfile");

	// Character classes and '?'.
	Check(fm::Copy("./sandbox/src/[ab].txt", "./sandbox/dst/cls"), "character class copy");
	Check(FileExistsRaw("./sandbox/dst/cls/a.txt") && FileExistsRaw("./sandbox/dst/cls/b.txt"),
		  "character class matched both files");

	// Move and rename.
	Check(fm::Move("./sandbox/dst/wild/a.txt", "./sandbox/dst/wild/renamed.txt"), "move rename");
	Check(!FileExistsRaw("./sandbox/dst/wild/a.txt"), "move removed old name");
	Check(ReadFileRaw("./sandbox/dst/wild/renamed.txt") == "AAAA", "renamed content intact");

	// Wildcard move into parent.
	Check(fm::Move("./sandbox/src/*.log", "./sandbox/dst"), "wildcard move into directory");
	Check(FileExistsRaw("./sandbox/dst/c.log"), "wildcard moved c.log");
	Check(!FileExistsRaw("./sandbox/src/c.log"), "wildcard move removed source");

	// Directory move.
	Check(fm::Move("./sandbox/dst/srccopy", "./sandbox/dst/movedcopy"), "move directory");
	Check(!DirExistsRaw("./sandbox/dst/srccopy"), "move removed old directory");
	Check(ReadFileRaw("./sandbox/dst/movedcopy/sub/d.txt") == "DDD", "moved directory nested file");

	// Recursive delete.
	Check(fm::Delete("./sandbox/dst/movedcopy"), "delete directory recursively");
	Check(!DirExistsRaw("./sandbox/dst/movedcopy"), "deleted directory is gone");

	// Wildcard delete.
	Check(fm::Delete("./sandbox/dst/wild/*.txt"), "wildcard delete");
	Check(!FileExistsRaw("./sandbox/dst/wild/renamed.txt"), "wildcard delete removed file");
	Check(DirExistsRaw("./sandbox/dst/wild"), "wildcard delete kept directory");

	// Missing paths.
	Check(!fm::Delete("./sandbox/does-not-exist"), "delete missing without force fails");
	Check(!LastErrorText().empty(), "missing path sets error");
	Check(fm::Delete("./sandbox/does-not-exist", true), "delete missing with force succeeds");
	Check(LastErrorText().empty(), "force delete clears error");

	// Mixed file and directory wildcard.
	Check(fm::Copy("./sandbox/mix/*", "./sandbox/out"), "copy wildcard matching files and directories");
	Check(FileExistsRaw("./sandbox/out/file.txt"), "mixed copy copied file");
	Check(ReadFileRaw("./sandbox/out/sub/f.txt") == "SUBF", "mixed copy copied directory recursively");

	// Wildcard in an intermediate component.
	Check(fm::Copy("./sandbox/mix/*/*.txt", "./sandbox/out2"), "wildcard in intermediate component");
	Check(ReadFileRaw("./sandbox/out2/f.txt") == "SUBF", "nested wildcard copied the right file");
	Check(!FileExistsRaw("./sandbox/out2/file.txt"), "nested wildcard did not copy unrelated file");

	// Absolute path handling.
	const std::string absSource = exeDir + "/sandbox/src/b.txt";
	Check(fm::Copy(absSource, "./sandbox/dst/abs.txt"), "absolute source path");
	Check(ReadFileRaw("./sandbox/dst/abs.txt") == "BBB", "absolute source content");

	// UTF-8 names.
	Check(WriteFileUtf8("./sandbox/src/中文.txt", "CN"), "write UTF-8 filename");
	Check(fm::Copy("./sandbox/src/中文.txt", "./sandbox/dst/中文副本.txt"), "copy UTF-8 filename");
	Check(ReadFileUtf8("./sandbox/dst/中文副本.txt") == "CN", "UTF-8 filename content");

	// Same source and destination.
	Check(!fm::Copy("./sandbox/src/b.txt", "./sandbox/src/b.txt", true), "copy onto itself fails");
	Check(ReadFileRaw("./sandbox/src/b.txt") == "BBB", "copy onto itself kept source");

	// Directory into itself.
	Check(!fm::Copy("./sandbox/src", "./sandbox/src/self"), "copy directory into itself fails");
	Check(!DirExistsRaw("./sandbox/src/self"), "directory into itself was not created");

	// Replace across types. When the destination is an existing directory the
	// source is placed inside it, exactly like cp/mv; otherwise it replaces the
	// destination.
	Check(WriteFileRaw("./sandbox/replace", "OLD"), "write replace file");
	Check(fm::Copy("./sandbox/src/sub", "./sandbox/replace", true), "force replace file with directory");
	Check(DirExistsRaw("./sandbox/replace"), "replace became a directory");
	Check(ReadFileRaw("./sandbox/replace/d.txt") == "DDD", "replaced directory content");
	Check(fm::Copy("./sandbox/src/a.txt", "./sandbox/replace", true), "force copy into existing directory");
	Check(FileExistsRaw("./sandbox/replace/a.txt"), "copy into existing directory kept source name");
	Check(ReadFileRaw("./sandbox/replace/a.txt") == "AAAA", "copy into existing directory content");

	Check(WriteFileRaw("./sandbox/replace2", "OLD2"), "write second replace file");
	Check(fm::Copy("./sandbox/src/a.txt", "./sandbox/replace2", true), "force replace file with file");
	Check(ReadFileRaw("./sandbox/replace2") == "AAAA", "file replacement content");

	// Move force replacement.
	Check(WriteFileRaw("./sandbox/mvsrc", "NEW"), "write move source");
	Check(MakeDirRaw("./sandbox/mvdst"), "create move destination directory");
	Check(WriteFileRaw("./sandbox/mvdst/old.txt", "OLD"), "write move destination file");
	Check(fm::Move("./sandbox/mvsrc", "./sandbox/mvdst", true), "force move into existing directory");
	Check(FileExistsRaw("./sandbox/mvdst/mvsrc"), "moved file kept its name inside the directory");
	Check(ReadFileRaw("./sandbox/mvdst/mvsrc") == "NEW", "moved file content");
	Check(FileExistsRaw("./sandbox/mvdst/old.txt"), "unrelated destination file kept");

	Check(MakeDirRaw("./sandbox/mvdir"), "create move directory");
	Check(WriteFileRaw("./sandbox/mvdir/x.txt", "X"), "write move directory file");
	Check(WriteFileRaw("./sandbox/mvfile", "OLD3"), "write move file destination");
	Check(fm::Move("./sandbox/mvdir", "./sandbox/mvfile", true), "force move directory over file");
	Check(DirExistsRaw("./sandbox/mvfile"), "move replaced file with directory");
	Check(ReadFileRaw("./sandbox/mvfile/x.txt") == "X", "moved directory content");

	// Read-only deletion.
	Check(WriteFileRaw("./sandbox/readonly.txt", "RO"), "write read-only file");
	SetReadOnly("./sandbox/readonly.txt");
	Check(fm::Delete("./sandbox/readonly.txt"), "delete read-only file");
	Check(!FileExistsRaw("./sandbox/readonly.txt"), "read-only file was removed");

	// Empty directory copy.
	Check(MakeDirRaw("./sandbox/emptydir"), "create empty directory");
	Check(fm::Copy("./sandbox/emptydir", "./sandbox/emptydir_copy"), "copy empty directory");
	Check(DirExistsRaw("./sandbox/emptydir_copy"), "empty directory copied");

#ifndef _WIN32
	// Cross-filesystem move (tmpfs -> disk), which must fall back to copy and
	// delete after rename() reports EXDEV.
	if (DirExistsRaw("/dev/shm"))
	{
		const std::string xdevSource = "/dev/shm/fmtest_xdev";
		const std::string xdevDest = exeDir + "/sandbox/xdev";
		fm::Delete(xdevSource, true);
		Check(MakeDirRaw(xdevSource), "create cross-device source");
		Check(WriteFileRaw(xdevSource + "/file.txt", "XDEV"), "write cross-device source file");
		Check(fm::Move(xdevSource, xdevDest), "cross-device move");
		Check(!DirExistsRaw(xdevSource), "cross-device move removed source");
		Check(ReadFileRaw(xdevDest + "/file.txt") == "XDEV", "cross-device move content");
	}
#endif

	// Protected paths.
	Check(!fm::Delete(".", true), "delete server root is refused");
	Check(DirExistsRaw("./sandbox"), "delete server root did not remove test directory");
	Check(!fm::Delete("..", true), "delete server root parent is refused");

	// Cross-volume move when possible (tmp on a different filesystem is rare,
	// so this is mostly a smoke test for the fallback path on Windows).
	// (Skipped: the harness cannot guarantee two volumes.)

	// Large file performance (informational, not a pass/fail criterion).
	if (argc > 1 && strcmp(argv[1], "--bench") == 0)
	{
		const size_t megabytes = (argc > 2) ? static_cast<size_t>(atoi(argv[2])) : 128;
		printf("\nCreating %zu MB test file...\n", megabytes);
		if (CreateLargeFile("./sandbox/bench_src.bin", megabytes))
		{
			auto start = std::chrono::steady_clock::now();
			bool ok = fm::Copy("./sandbox/bench_src.bin", "./sandbox/bench_dst.bin", true);
			auto end = std::chrono::steady_clock::now();
			double seconds = std::chrono::duration<double>(end - start).count();
			if (ok)
			{
				printf("[INFO] copied %zu MB in %.3f s (%.1f MB/s)\n", megabytes, seconds,
					   seconds > 0.0 ? megabytes / seconds : 0.0);
			}
			else
			{
				printf("[FAIL] large file copy: %s\n", LastErrorText().c_str());
				++g_failures;
			}
			fm::Delete("./sandbox/bench_src.bin");
			fm::Delete("./sandbox/bench_dst.bin");
		}
		else
		{
			printf("[FAIL] could not create the large test file\n");
			++g_failures;
		}
	}

	printf("\n%d checks, %d failures\n", g_checks, g_failures);

	if (g_failures == 0)
	{
		fm::Delete("./sandbox", true);
	}
	else
	{
		printf("Sandbox kept for inspection: %s/sandbox\n", exeDir.c_str());
	}

	return g_failures == 0 ? 0 : 1;
}
