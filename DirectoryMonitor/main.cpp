#include "pch.h"

std::wstring FormatByteSize(size_t qdw)
{
	std::wstring buf;
	buf.resize(32);
	if (StrFormatByteSizeW((LONGLONG)qdw, &buf[0], (UINT)buf.size()) == nullptr)
	{
		buf.assign(L"conversion failed (StrFormatByteSizeW)");
	}
	else
	{
		auto idxZero = buf.find_first_of(L'\0');
		buf.resize(idxZero);
	}

	return buf;
}
void CloseHandle_mayBeNullOrInvalid(HANDLE h)
{
	if (h != NULL && h != INVALID_HANDLE_VALUE)
	{
		CloseHandle(h);
	}
}
LPCWSTR getActionname(DWORD action)
{
	if (action == FILE_ACTION_ADDED)			return L"ADDED              ";
	if (action == FILE_ACTION_REMOVED)			return L"REMOVED            ";
	if (action == FILE_ACTION_MODIFIED)			return L"MODIFIED           ";
	if (action == FILE_ACTION_RENAMED_OLD_NAME) return L"RENAMED_OLD_NAME   ";
	if (action == FILE_ACTION_RENAMED_NEW_NAME) return L"RENAMED_NEW_NAME   ";
	return L"UNKNOWN";
}
void printChanges(LPVOID buf, DWORD bytesReturned, std::wstring* str)
{
	const FILE_NOTIFY_INFORMATION* info = (FILE_NOTIFY_INFORMATION*)buf;

	str->clear();
	for(;;)
	{
		str->append(getActionname(info->Action));
		str->append(info->FileName, info->FileNameLength / sizeof(WCHAR) );
		str->push_back(L'\n');

		if (info->NextEntryOffset == 0)
		{
			break;
		}
		else
		{
			info = (FILE_NOTIFY_INFORMATION*)((BYTE*)info + info->NextEntryOffset);
		}
	}

	wprintf(L"%s", str->c_str());
}
void printStats(const Stats& stats, size_t fileCount)
{
	wprintf(L"files: %zu | added/removed/modified/renamed: %zu/%zu/%zu/%zu | max files/bytes: %zu/%s | notify records/bytes: %zu/%s\n"
		, fileCount
		, stats.added
		, stats.removed
		, stats.modified
		, stats.renamed
		, stats.largest_change_files
		, FormatByteSize(stats.largest_change_bytes).c_str()
		, stats.changes
		, FormatByteSize(stats.overall_notify_bytes).c_str());
}

void changes_updateStats(DWORD action, Stats* stats)
{
	     if (action == FILE_ACTION_ADDED)				{ stats->added    += 1;	}
	else if (action == FILE_ACTION_REMOVED)				{ stats->removed  += 1;	}
	else if (action == FILE_ACTION_MODIFIED)			{ stats->modified += 1; }
	else if (action == FILE_ACTION_RENAMED_NEW_NAME)	{ stats->renamed  += 1;	}
}

void handleRunningEnumeration(std::unordered_set<std::wstring>* files, const FILE_NOTIFY_INFORMATION* info)
{
	if (info->Action == FILE_ACTION_ADDED)
	{
		files->emplace(&(info->FileName[0]), info->FileNameLength);
#ifdef _DEBUG
		wprintf(L"notify, emplace file to hash: [%s]\n", std::wstring(&(info->FileName[0]), info->FileNameLength).c_str());
#endif

	}
	else if (info->Action == FILE_ACTION_REMOVED)
	{
		std::wstring tmpFilename(&(info->FileName[0]), info->FileNameLength);
		files->erase(tmpFilename);
#ifdef _DEBUG
		wprintf(L"notify, erase file from hash: [%s]\n", tmpFilename.c_str());
#endif

	}
}

void processChanges(RefreshCtx* ctx, LPVOID buf, DWORD bytesReturned, Stats *stats)
{
	const FILE_NOTIFY_INFORMATION* info = (FILE_NOTIFY_INFORMATION*)buf;
	size_t changes = 0;

	{
		const std::lock_guard<std::mutex> lock_hashtable(ctx->mutex_notify_vs_enum);
		std::unordered_set<std::wstring>* set_of_files = ctx->files.load();

		for (;;)
		{
			changes += 1;
			changes_updateStats(info->Action, stats);
			if (set_of_files == nullptr)
			{
				     if (info->Action == FILE_ACTION_ADDED)   { ctx->numberFilesIncrement(); }
				else if (info->Action == FILE_ACTION_REMOVED) { ctx->numberFilesDecrement(); }
			}
			else
			{
				handleRunningEnumeration(set_of_files, info);
			}

			if (info->NextEntryOffset == 0)
			{
				break;
			}
			else
			{
				info = (FILE_NOTIFY_INFORMATION*)((BYTE*)info + info->NextEntryOffset);
			}
		}
	}
	stats->changes += 1;
	stats->largest_change_files = max(changes, stats->largest_change_files);
	stats->largest_change_bytes = max((size_t)bytesReturned, stats->largest_change_bytes);
}

bool ProcessConsoleInput(HANDLE hStdin, char* key)
{
	INPUT_RECORD record;
	DWORD numRead;

	if (!ReadConsoleInputW(hStdin, &record, 1, &numRead)) {
	}
	else if (record.EventType != KEY_EVENT) {
		// don't care about other console events
	}
	else if (!record.Event.KeyEvent.bKeyDown) {
		// really only care about keydown
	}
	else
	{
		*key = record.Event.KeyEvent.uChar.AsciiChar;
		return true;
	}

	return false;
} 

LastError* runEnumeration_hashTable(RefreshCtx* ctx, LastError* err)
{
	WIN32_FIND_DATA findData;
	std::wstring dir(ctx->rootDir);

	size_t dirStartIdx = ctx->rootDir.ends_with(L'\\') ? ctx->rootDir.length() : ctx->rootDir.length() + 1;

	ctx->setFileCount(0);

	std::unordered_set<std::wstring> set_of_files;
	{
		const std::lock_guard<std::mutex> lock_hashtable(ctx->mutex_notify_vs_enum);
		ctx->files.store(&set_of_files);
	}

	err = EnumDirRecurse(&dir, &findData,
		[&ctx, dirStartIdx, &set_of_files](const std::wstring& fullEntryname, WIN32_FIND_DATA* findData)
		{
			ctx->numberFilesIncrement();

			{
				const std::lock_guard<std::mutex> lock_hashtable(ctx->mutex_notify_vs_enum);
				set_of_files.emplace(fullEntryname.begin() + dirStartIdx, fullEntryname.end());
/*
#ifdef _DEBUG
				wprintf(L"enum, add file to hash: [%s]\n", std::wstring(fullEntryname.begin() + dirStartIdx, fullEntryname.end()).c_str());
#endif
*/

			}

		}
		, err);

	{
		const std::lock_guard<std::mutex> lock_hashtable(ctx->mutex_notify_vs_enum);
		ctx->files.store(nullptr);
	}

	ctx->setFileCount(set_of_files.size());

	return err;
}

DWORD WINAPI RefreshThread(LPVOID lpThreadParameter)
{
	RefreshCtx* refreshCtx = (RefreshCtx*)lpThreadParameter;

	LastError refreshErr;
	if (runEnumeration_hashTable(refreshCtx, &refreshErr)->failed())
	{
		refreshErr.print();
	}

	refreshCtx->refreshRunning = false;
	printf("refresh (enumerating files) ended\n");

	return 0;
}

LastError* StartMonitor(RefreshCtx* refreshCtx, const HANDLE hDir, const HANDLE hEventReadChanges, const LPVOID bufChanges, const DWORD bufChangesSize, LastError* err)
{
	Stats stats;
	HANDLE hStdin = GetStdHandle(STD_INPUT_HANDLE);
	SetConsoleMode(hStdin, ENABLE_PROCESSED_INPUT);

	OVERLAPPED ovlReadDirectoryChanges = { 0 };
	ovlReadDirectoryChanges.hEvent = hEventReadChanges;
	HANDLE waitHandles[2];
	const DWORD WAIT_IDX_ReadChanges = 0;
	const DWORD WAIT_IDX_stdin = 1;
	waitHandles[WAIT_IDX_ReadChanges] = hEventReadChanges;
	waitHandles[WAIT_IDX_stdin] = hStdin;

	DWORD bytesReturned;
	if (ReadDirectoryChangesW(
		hDir
		, bufChanges
		, bufChangesSize
		, TRUE
		, FILE_NOTIFY_CHANGE_FILE_NAME
		, &bytesReturned
		, &ovlReadDirectoryChanges
		, NULL) == 0)
	{
		err->set(L"ReadDirectoryChangesW(first call)");
	}
	else
	{
		std::wstring tmpStr;
		for (;;)
		{
			DWORD wait = WaitForMultipleObjects(2, waitHandles, FALSE, INFINITE);
			if (wait == WAIT_FAILED)
			{
				err->set(L"WaitForMultipleObjects");
				break;
			}
			else if (wait == WAIT_IDX_ReadChanges)
			{
				if (!GetOverlappedResult(hDir, &ovlReadDirectoryChanges, &bytesReturned, TRUE))
				{
					printf("%d\tGetOverlappedResult\n", GetLastError());
				}
				else
				{
					stats.overall_notify_bytes += bytesReturned;
					processChanges(refreshCtx, bufChanges, bytesReturned, &stats);
					if (refreshCtx->printChangedFiles)
					{
						printChanges(bufChanges, bytesReturned, &tmpStr);
					}
					printStats(stats, refreshCtx->getFileCount() );

					if (ReadDirectoryChangesW(
						hDir
						, bufChanges
						, bufChangesSize
						, TRUE
						, FILE_NOTIFY_CHANGE_FILE_NAME
						, &bytesReturned
						, &ovlReadDirectoryChanges
						, NULL) == 0)
					{
						err->set(L"ReadDirectoryChangesW(in loop)");
						break;
					}
				}
			}
			else if (wait == WAIT_IDX_stdin)
			{
				char key;
				if (ProcessConsoleInput(hStdin, &key))
				{
					if (key == 'r')
					{
						if ( ! refreshCtx->refreshRunning )
						{
							printf("refresh (enumerating files) started\n");
							refreshCtx->refreshRunning = true;
							DWORD threadId;
							HANDLE hThread;
							if ((hThread = CreateThread(NULL, 0, RefreshThread, refreshCtx, 0, &threadId)) == NULL)
							{
								LastError(L"CreateThread").print();
							}
							else
							{
								refreshCtx->refreshRunning = false;;
								CloseHandle(hThread);
							}
						}
					}
					else if (key == 'p')
					{
						refreshCtx->printChangedFiles = !refreshCtx->printChangedFiles;
						printf("printing changed files is now %s\n", refreshCtx->printChangedFiles ? "ON" : "OFF");
					}

				}
			}
		}
	}
	return err;
}

int wmain(int argc, wchar_t *argv[])
{
	if (argc != 2)
	{
		wprintf(L"usage: %s {directory to monitor}", argv[0]);
		return 999;
	}

	LPCWSTR dirToMonitor = argv[1];
	LPVOID bufChanges = NULL;
	const DWORD bufChangesSize = 64 * 1024;
	HANDLE hDir = NULL;
	HANDLE hEventReadChanges = NULL;
	LastError err;

	if ((bufChanges = HeapAlloc(GetProcessHeap(), 0, bufChangesSize)) == NULL)
	{
		err.set(L"HeapAlloc", ERROR_NOT_ENOUGH_MEMORY);
	}
	else if ( (hDir=CreateFileW(
		dirToMonitor
		, FILE_LIST_DIRECTORY
		, FILE_SHARE_READ 
		, NULL
		, OPEN_EXISTING
		, FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OVERLAPPED
		, NULL)) == INVALID_HANDLE_VALUE)
	{
		err.set(L"CreateFileW");
	}
	else if ((hEventReadChanges = CreateEventW(NULL, FALSE, FALSE, NULL)) == INVALID_HANDLE_VALUE)
	{
		err.set(L"CreateEventW");
	}
	else
	{
		RefreshCtx ctx(dirToMonitor);
		wprintf(L"start monitor for directory: %s\n", dirToMonitor);
		StartMonitor(&ctx, hDir, hEventReadChanges, bufChanges, bufChangesSize, &err);
	}

	CloseHandle_mayBeNullOrInvalid(hEventReadChanges);
	CloseHandle_mayBeNullOrInvalid(hDir);
	if (bufChanges != NULL) { HeapFree(GetProcessHeap(), 0, bufChanges); }

	if (err.failed())
	{
		err.print();
	}

	return err.code();
}
