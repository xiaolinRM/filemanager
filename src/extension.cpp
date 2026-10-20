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

#include "extension.h"

#include "fm.h"

#include <cstring>
#include <string>

FileManager g_FileManager;

SMEXT_LINK(&g_FileManager);

namespace
{

cell_t Native_Copy(IPluginContext *pContext, const cell_t *params)
{
	if (params[0] < 3)
	{
		return pContext->ThrowNativeError("FileManager.Copy expects at least 3 arguments");
	}

	char *oldpath = NULL;
	char *newpath = NULL;
	pContext->LocalToString(params[1], &oldpath);
	pContext->LocalToString(params[2], &newpath);

	const bool force = params[3] != 0;
	return fm::Copy(oldpath, newpath, force) ? 1 : 0;
}

cell_t Native_Move(IPluginContext *pContext, const cell_t *params)
{
	if (params[0] < 3)
	{
		return pContext->ThrowNativeError("FileManager.Move expects at least 3 arguments");
	}

	char *oldpath = NULL;
	char *newpath = NULL;
	pContext->LocalToString(params[1], &oldpath);
	pContext->LocalToString(params[2], &newpath);

	const bool force = params[3] != 0;
	return fm::Move(oldpath, newpath, force) ? 1 : 0;
}

cell_t Native_Delete(IPluginContext *pContext, const cell_t *params)
{
	if (params[0] < 2)
	{
		return pContext->ThrowNativeError("FileManager.Delete expects at least 2 arguments");
	}

	char *path = NULL;
	pContext->LocalToString(params[1], &path);

	const bool force = params[2] != 0;
	return fm::Delete(path, force) ? 1 : 0;
}

cell_t Native_GetLastError(IPluginContext *pContext, const cell_t *params)
{
	if (params[0] < 2)
	{
		return pContext->ThrowNativeError("FileManager.GetLastError expects at least 2 arguments");
	}

	char *buffer = NULL;
	pContext->LocalToString(params[1], &buffer);

	const cell_t maxlength = params[2];
	if (maxlength <= 0)
	{
		return pContext->ThrowNativeError("FileManager.GetLastError buffer size must be positive");
	}

	const char *message = fm::LastError();
	if (message == NULL || message[0] == '\0')
	{
		buffer[0] = '\0';
		return 0;
	}

	size_t length = strlen(message);
	if (length >= static_cast<size_t>(maxlength))
	{
		length = static_cast<size_t>(maxlength) - 1;
	}
	memcpy(buffer, message, length);
	buffer[length] = '\0';
	return 1;
}

const sp_nativeinfo_t g_FileManagerNatives[] = {
	{ "FileManager.Copy", Native_Copy },
	{ "FileManager.Move", Native_Move },
	{ "FileManager.Delete", Native_Delete },
	{ "FileManager.GetLastError", Native_GetLastError },
	{ NULL, NULL }
};

} // namespace

bool FileManager::SDK_OnLoad(char *error, size_t maxlength, bool late)
{
	std::string initError;
	if (!fm::Initialize(initError))
	{
		ke::SafeStrcpy(error, maxlength, initError.c_str());
		return false;
	}

	sharesys->AddNatives(myself, g_FileManagerNatives);

	smutils->LogMessage(myself, "File Manager loaded. Server directory: %s", fm::ServerRoot().c_str());
	return true;
}

void FileManager::SDK_OnUnload()
{
}

bool FileManager::QueryRunning(char *error, size_t maxlength)
{
	if (!fm::IsInitialized())
	{
		ke::SafeStrcpy(error, maxlength, "the server directory has not been initialized");
		return false;
	}
	return true;
}
