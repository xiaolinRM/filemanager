/**
 * File Manager integration test plugin.
 *
 * The test tree lives in addons/sourcemod/data/fmtest (mod relative) which is
 * ./left4dead2/addons/sourcemod/data/fmtest for the extension (server root
 * relative). Nothing outside that directory is modified.
 *
 * If addons/sourcemod/data/fmtest_run exists when a map starts, the suite runs
 * automatically, writes addons/sourcemod/data/fmtest_result.txt and quits the
 * server. The command "sm_fm_selftest" runs it manually without quitting.
 */

#pragma semicolon 1
#pragma newdecls required

#include <sourcemod>
#include <filemanager>

#define TEST_BASE "addons/sourcemod/data/fmtest"
#define FM_BASE "./left4dead2/addons/sourcemod/data/fmtest"
#define RESULT_FILE "addons/sourcemod/data/fmtest_result.txt"

File g_Result = null;
int g_Pass = 0;
int g_Fail = 0;
bool g_AutoRun = false;
bool g_Running = false;
bool g_Windows = false;

public Plugin myinfo =
{
	name = "File Manager Test",
	author = "File Manager Contributors",
	description = "Integration tests for the File Manager extension",
	version = "1.0.0",
	url = ""
};

public void OnPluginStart()
{
	LogToFile("fmtest_diag.log", "OnPluginStart");
	RegConsoleCmd("sm_fm_selftest", Command_SelfTest, "Runs the File Manager test suite");
}

public void OnMapStart()
{
	char marker[PLATFORM_MAX_PATH];
	BuildPath(Path_SM, marker, sizeof(marker), "data/fmtest_run");
	LogToFile("fmtest_diag.log", "OnMapStart, marker=%s exists=%d", marker, FileExists(marker));
	if (!FileExists(marker))
	{
		return;
	}

	DeleteFile(marker);
	g_AutoRun = true;

	// L4D2 hibernates while waiting for players, which pauses timers and may
	// delay OnConfigsExecuted. File operations do not need a game frame, so
	// run the suite directly during map start.
	RunSuite();
}

public Action Command_SelfTest(int client, int args)
{
	if (g_Running)
	{
		ReplyToCommand(client, "[FileManager] tests are already running");
		return Plugin_Handled;
	}
	g_AutoRun = false;
	RunSuite();
	return Plugin_Handled;
}

void LogLine(const char[] format, any ...)
{
	char buffer[1024];
	VFormat(buffer, sizeof(buffer), format, 2);

	PrintToServer("[FMTest] %s", buffer);
	if (g_Result != null)
	{
		g_Result.WriteLine("%s", buffer);
		g_Result.Flush();
	}
}

void Check(const char[] name, bool condition)
{
	if (condition)
	{
		++g_Pass;
		LogLine("PASS: %s", name);
	}
	else
	{
		++g_Fail;
		char error[512];
		FileManager.GetLastError(error, sizeof(error));
		LogLine("FAIL: %s (last error: %s)", name, error);
	}
}

bool WriteTestFile(const char[] relative, const char[] content)
{
	char path[PLATFORM_MAX_PATH];
	Format(path, sizeof(path), "%s/%s", TEST_BASE, relative);

	File file = OpenFile(path, "w");
	if (file == null)
	{
		return false;
	}
	file.WriteString(content, false);
	delete file;
	return true;
}

bool FileEquals(const char[] relative, const char[] expected)
{
	char path[PLATFORM_MAX_PATH];
	Format(path, sizeof(path), "%s/%s", TEST_BASE, relative);

	File file = OpenFile(path, "r");
	if (file == null)
	{
		return false;
	}

	char buffer[512];
	int length = file.ReadString(buffer, sizeof(buffer), -1);
	delete file;
	return length >= 0 && StrEqual(buffer, expected);
}

bool Exists(const char[] relative)
{
	char path[PLATFORM_MAX_PATH];
	Format(path, sizeof(path), "%s/%s", TEST_BASE, relative);
	return FileExists(path);
}

bool DirExistsRel(const char[] relative)
{
	char path[PLATFORM_MAX_PATH];
	Format(path, sizeof(path), "%s/%s", TEST_BASE, relative);
	return DirExists(path);
}

void EnsureDir(const char[] relative)
{
	char path[PLATFORM_MAX_PATH];
	Format(path, sizeof(path), "%s/%s", TEST_BASE, relative);
	if (!DirExists(path))
	{
		CreateDirectory(path, 493);
	}
}

bool IsAbsolutePath(const char[] path)
{
	if (path[0] == '/' || path[0] == '\\')
	{
		return true;
	}
	return path[0] != '\0' && path[1] == ':';
}

void RunSuite()
{
	if (g_Running)
	{
		return;
	}
	g_Running = true;
	g_Pass = 0;
	g_Fail = 0;

	char resultPath[PLATFORM_MAX_PATH];
	BuildPath(Path_SM, resultPath, sizeof(resultPath), "data/fmtest_result.txt");
	char smPath[PLATFORM_MAX_PATH];
	BuildPath(Path_SM, smPath, sizeof(smPath), "");
	g_Windows = (StrContains(smPath, "\\") != -1);
	g_Result = OpenFile(resultPath, "w");
	if (g_Result == null)
	{
		LogToFile("fmtest_diag.log", "could not open result file: %s", resultPath);
		PrintToServer("[FMTest] could not open the result file");
	}
	else
	{
		LogToFile("fmtest_diag.log", "result file opened: %s", resultPath);
	}

	LogLine("File Manager test suite started");
	LogLine("Server root relative test path: %s", FM_BASE);

	// Clean up leftovers from a previous run.
	LogToFile("fmtest_diag.log", "cleaning test tree");
	FileManager.Delete(FM_BASE, true);

	EnsureDir("");
	EnsureDir("src");
	EnsureDir("src/sub");
	EnsureDir("dst");
	EnsureDir("mix");
	EnsureDir("mix/sub");
	EnsureDir("out");
	EnsureDir("out2");
	EnsureDir("cls");
	LogToFile("fmtest_diag.log", "test tree created");

	Check("write a.txt", WriteTestFile("src/a.txt", "AAA"));
	Check("write b.txt", WriteTestFile("src/b.txt", "BBB"));
	Check("write c.log", WriteTestFile("src/c.log", "CCC"));
	Check("write sub/d.txt", WriteTestFile("src/sub/d.txt", "DDD"));
	Check("write mix/file.txt", WriteTestFile("mix/file.txt", "MIX"));
	Check("write mix/sub/f.txt", WriteTestFile("mix/sub/f.txt", "SUBF"));

	// Basic copy.
	Check("copy file", FileManager.Copy(FM_BASE ... "/src/a.txt", FM_BASE ... "/dst/a_copy.txt"));
	Check("copied content", FileEquals("dst/a_copy.txt", "AAA"));
	char dummy[1];
	Check("success clears error", !FileManager.GetLastError(dummy, sizeof(dummy)));

	// Existing destination without force.
	Check("copy onto existing without force fails",
		  !FileManager.Copy(FM_BASE ... "/src/a.txt", FM_BASE ... "/dst/a_copy.txt"));
	char error[512];
	bool hasError = FileManager.GetLastError(error, sizeof(error));
	Check("failure reports an error", hasError && error[0] != '\0');
	Check("failed copy keeps destination", FileEquals("dst/a_copy.txt", "AAA"));

	// Force overwrite.
	Check("update a.txt", WriteTestFile("src/a.txt", "AAAA"));
	Check("force copy over existing",
		  FileManager.Copy(FM_BASE ... "/src/a.txt", FM_BASE ... "/dst/a_copy.txt", true));
	Check("force copy content", FileEquals("dst/a_copy.txt", "AAAA"));

	// Recursive directory copy.
	Check("copy directory", FileManager.Copy(FM_BASE ... "/src", FM_BASE ... "/dst/srccopy"));
	Check("directory copy file", FileEquals("dst/srccopy/a.txt", "AAAA"));
	Check("directory copy nested file", FileEquals("dst/srccopy/sub/d.txt", "DDD"));

	// Wildcard copy.
	Check("wildcard copy", FileManager.Copy(FM_BASE ... "/src/*.txt", FM_BASE ... "/dst"));
	Check("wildcard copied a.txt", Exists("dst/a.txt"));
	Check("wildcard copied b.txt", Exists("dst/b.txt"));
	Check("wildcard did not copy c.log", !Exists("dst/c.log"));

	// Character class.
	Check("character class copy", FileManager.Copy(FM_BASE ... "/src/[ab].txt", FM_BASE ... "/cls"));
	Check("character class copied files", Exists("cls/a.txt") && Exists("cls/b.txt"));

	// Move / rename.
	Check("move rename", FileManager.Move(FM_BASE ... "/dst/a.txt", FM_BASE ... "/dst/renamed.txt"));
	Check("move removed old name", !Exists("dst/a.txt"));
	Check("move kept content", FileEquals("dst/renamed.txt", "AAAA"));

	// Wildcard move.
	Check("wildcard move", FileManager.Move(FM_BASE ... "/src/*.log", FM_BASE ... "/dst"));
	Check("wildcard move destination", Exists("dst/c.log"));
	Check("wildcard move removed source", !Exists("src/c.log"));

	// Directory move.
	Check("move directory", FileManager.Move(FM_BASE ... "/dst/srccopy", FM_BASE ... "/dst/movedcopy"));
	Check("directory move removed source", !DirExistsRel("dst/srccopy"));
	Check("directory move content", FileEquals("dst/movedcopy/sub/d.txt", "DDD"));

	// Recursive delete.
	Check("delete directory", FileManager.Delete(FM_BASE ... "/dst/movedcopy"));
	Check("delete removed directory", !DirExistsRel("dst/movedcopy"));

	// Wildcard delete.
	Check("wildcard delete", FileManager.Delete(FM_BASE ... "/dst/*.txt"));
	Check("wildcard delete removed file", !Exists("dst/renamed.txt"));
	Check("wildcard delete kept directory", DirExistsRel("dst"));

	// Missing path handling.
	Check("delete missing fails", !FileManager.Delete(FM_BASE ... "/does-not-exist"));
	Check("delete missing sets error", FileManager.GetLastError(error, sizeof(error)));
	Check("delete missing with force", FileManager.Delete(FM_BASE ... "/does-not-exist", true));

	// Mixed files and directories matched by one wildcard.
	Check("mixed wildcard copy", FileManager.Copy(FM_BASE ... "/mix/*", FM_BASE ... "/out"));
	Check("mixed wildcard file", Exists("out/file.txt"));
	Check("mixed wildcard directory", FileEquals("out/sub/f.txt", "SUBF"));

	// Wildcard in an intermediate component.
	Check("nested wildcard copy", FileManager.Copy(FM_BASE ... "/mix/*/*.txt", FM_BASE ... "/out2"));
	Check("nested wildcard file", FileEquals("out2/f.txt", "SUBF"));
	Check("nested wildcard did not copy unrelated file", !Exists("out2/file.txt"));

	// Absolute path. BuildPath() returns a mod-relative path on current
	// SourceMod builds, so only test this when it really is absolute; the
	// standalone test harness covers absolute paths on both platforms.
	char absSource[PLATFORM_MAX_PATH];
	char absDest[PLATFORM_MAX_PATH];
	BuildPath(Path_SM, absSource, sizeof(absSource), "data/fmtest/src/b.txt");
	BuildPath(Path_SM, absDest, sizeof(absDest), "data/fmtest/dst/abs.txt");
	if (IsAbsolutePath(absSource))
	{
		Check("absolute path copy", FileManager.Copy(absSource, absDest));
		Check("absolute path content", FileEquals("dst/abs.txt", "BBB"));
	}
	else
	{
		LogLine("SKIP: absolute path test (BuildPath returned '%s')", absSource);
	}

	// UTF-8 file name. SourceMod's own file natives use the ANSI code page on
	// Windows, so creating the fixture through OpenFile() only works on Linux.
	// The extension itself is UTF-8 aware on both platforms; the standalone
	// harness covers the Windows case with wide-character file APIs.
	if (g_Windows)
	{
		LogLine("SKIP: UTF-8 tests (SourceMod file natives use the ANSI code page on Windows)");
	}
	else
	{
		Check("write UTF-8 name", WriteTestFile("src/中文.txt", "CN"));
		Check("copy UTF-8 name", FileManager.Copy(FM_BASE ... "/src/中文.txt", FM_BASE ... "/dst/中文副本.txt"));
		Check("UTF-8 name content", FileEquals("dst/中文副本.txt", "CN"));
	}

	// Same source and destination.
	Check("copy onto itself fails",
		  !FileManager.Copy(FM_BASE ... "/src/b.txt", FM_BASE ... "/src/b.txt", true));
	Check("copy onto itself keeps source", FileEquals("src/b.txt", "BBB"));

	// Directory into itself.
	Check("copy directory into itself fails",
		  !FileManager.Copy(FM_BASE ... "/src", FM_BASE ... "/src/self"));
	Check("directory into itself not created", !DirExistsRel("src/self"));

	// Force replacement of a file with a directory.
	Check("write replacement file", WriteTestFile("replace", "OLD"));
	Check("force replace file with directory",
		  FileManager.Copy(FM_BASE ... "/src/sub", FM_BASE ... "/replace", true));
	Check("replacement is a directory", DirExistsRel("replace"));
	Check("replacement content", FileEquals("replace/d.txt", "DDD"));

	// Copy into an existing directory keeps the source name.
	Check("copy into existing directory",
		  FileManager.Copy(FM_BASE ... "/src/a.txt", FM_BASE ... "/replace", true));
	Check("copy into directory content", FileEquals("replace/a.txt", "AAAA"));

	// Move force replacement.
	Check("write move source", WriteTestFile("mvsrc", "NEW"));
	EnsureDir("mvdst");
	Check("write move destination", WriteTestFile("mvdst/old.txt", "OLD"));
	Check("force move into directory", FileManager.Move(FM_BASE ... "/mvsrc", FM_BASE ... "/mvdst", true));
	Check("force move content", FileEquals("mvdst/mvsrc", "NEW"));
	Check("force move kept unrelated file", Exists("mvdst/old.txt"));

	// Clean the test tree.
	bool cleaned = FileManager.Delete(FM_BASE, true);
	Check("cleanup test tree", cleaned);

	LogLine("File Manager test suite finished: %d passed, %d failed", g_Pass, g_Fail);

	if (g_Result != null)
	{
		g_Result.WriteLine("RESULT: %d passed, %d failed", g_Pass, g_Fail);
		delete g_Result;
		g_Result = null;
	}

	g_Running = false;

	if (g_AutoRun)
	{
		ServerCommand("quit");
	}
}
