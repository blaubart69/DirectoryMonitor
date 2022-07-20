#include "pch.h"

#include "DirectoryMonitor.h"
#include "WriteStdout.h"
#include "LastError.h"
#include "util.h"

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

int localtime_as_str(WCHAR* buf, const size_t cchBuf)
{
	SYSTEMTIME systemTime, localTime;

	GetSystemTime(&systemTime);
	GetLocalTime (&localTime);

	return 
		swprintf_s(
			buf
			, cchBuf
			, L"%04u-%02u-%02u %02u:%02u:%02u"
			, localTime.wYear, localTime.wMonth, localTime.wDay
			, localTime.wHour, localTime.wMinute, localTime.wSecond);
}
LPCWSTR getActionname(DWORD action)
{
	if (action == FILE_ACTION_ADDED)			return L"ADD    \t";
	if (action == FILE_ACTION_REMOVED)			return L"DEL    \t";
	if (action == FILE_ACTION_MODIFIED)			return L"MOD    \t";
	if (action == FILE_ACTION_RENAMED_OLD_NAME) return L"REN_OLD\t";
	if (action == FILE_ACTION_RENAMED_NEW_NAME) return L"REN_NEW\t";
	                                            return L"UNKNOWN\t";
}
void printChanges(LPVOID buf, DWORD bytesReturned, const std::wstring& root_dir, std::wstring* str)
{
	const FILE_NOTIFY_INFORMATION* info = (FILE_NOTIFY_INFORMATION*)buf;
	WCHAR localtime_string[20];
	localtime_as_str(localtime_string, sizeof(localtime_string) / sizeof(WCHAR));

	str->clear();
	for(;;)
	{
		str->append(localtime_string);
		str->push_back(L'\t');
		str->append(getActionname(info->Action));
		str->append(root_dir);
		str->append(info->FileName, info->FileNameLength / sizeof(WCHAR) );
		str->push_back(L'\r');
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

	WriteStdout(*str);
}
void printStats(bool shouldPrint, const Stats& stats, size_t fileCount, bool refreshRunning, size_t everyMilliseconds)
{
	if (!shouldPrint)
	{
		return;
	}

	static size_t last_added	= 0;
	static size_t last_removed	= 0;
	static size_t last_modified	= 0;
	static size_t last_renamed  = 0;

	static size_t last_ticks = 0;

	ULONGLONG currentTicks = GetTickCount64();
	if ((currentTicks - last_ticks) < everyMilliseconds)
	{
		return;
	}

	wprintf(L"files%s: %zu | +/-/mod/ren: %zu(%zu)/%zu(%zu)/%zu(%zu)/%zu(%zu) | notify records/bytes: %zu/%s | max files/bytes: %zu/%s\n"
		, refreshRunning ? L"(refresh running)" : L""
		, fileCount
		, stats.added,		(stats.added    - last_added)
		, stats.removed,    (stats.removed  - last_removed)
		, stats.modified,	(stats.modified - last_modified)
		, stats.renamed,	(stats.renamed  - last_renamed)
		,                stats.changes
		, FormatByteSize(stats.overall_notify_bytes).c_str()
		,               stats.largest_change_files
		, FormatByteSize(stats.largest_change_bytes).c_str()
	);

	last_added    = stats.added;
	last_removed  = stats.removed;
	last_modified = stats.modified;
	last_renamed  = stats.renamed;
	last_ticks    = currentTicks;
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

	std::unordered_set<std::wstring> set_of_files;
	{
		const std::lock_guard<std::mutex> lock_hashtable(ctx->mutex_notify_vs_enum);
		ctx->files.store(&set_of_files);
	}

	size_t dirStartIdx; 

	if (dir.ends_with(L'\\'))
	{
		dir.resize(dir.size() - 1);
		dirStartIdx = ctx->rootDir.length();
	}
	else
	{
		dirStartIdx = ctx->rootDir.length() + 1;
	}

	ctx->setFileCount(0);
	err = EnumDirRecurse(&dir, &findData,
		[&ctx, dirStartIdx, &set_of_files](const std::wstring& fullEntryname, WIN32_FIND_DATA* findData)
		{
			ctx->numberFilesIncrement();

			{
				const std::lock_guard<std::mutex> lock_hashtable(ctx->mutex_notify_vs_enum);
				set_of_files.emplace(fullEntryname.begin() + dirStartIdx, fullEntryname.end());
#ifdef _DEBUG
				wprintf(L"enum, add file to hash: [%s]\n", std::wstring(fullEntryname.begin() + dirStartIdx, fullEntryname.end()).c_str());
#endif

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

	refreshCtx->SetFinishedEvent();
	
	return 0;
}

void StartRefresh(RefreshCtx* refreshCtx)
{
	if ( ! refreshCtx->refreshRunning() )
	{
		DWORD threadId;
		HANDLE hThread;
		if ((hThread = CreateThread(NULL, 0, RefreshThread, refreshCtx, 0, &threadId)) == NULL)
		{
			LastError(L"CreateThread").print();
		}
		else
		{
			CloseHandle(hThread);
		}
	}
}

LastError* StartMonitor(LPCWSTR dirToMonitor, const HANDLE hDir, const HANDLE hEventReadChanges, const HANDLE hRefreshFinished, const LPVOID bufChanges, const DWORD bufChangesSize, const Options& opts, LastError* err)
{
	Stats stats;
	HANDLE hStdin = GetStdHandle(STD_INPUT_HANDLE);
	SetConsoleMode(hStdin, ENABLE_PROCESSED_INPUT);

	OVERLAPPED ovlReadDirectoryChanges = { 0 };
	ovlReadDirectoryChanges.hEvent = hEventReadChanges;
	HANDLE waitHandles[3];
	const DWORD WAIT_IDX_ReadChanges = 0;
	const DWORD WAIT_IDX_stdin = 1;
	const DWORD WAIT_IDX_refreshFinished = 2;
	waitHandles[WAIT_IDX_ReadChanges] = hEventReadChanges;
	waitHandles[WAIT_IDX_stdin] = hStdin;
	waitHandles[WAIT_IDX_refreshFinished] = hRefreshFinished;

	std::wstring root_dir_for_print(dirToMonitor);
	if (!root_dir_for_print.ends_with(L'\\'))
	{
		root_dir_for_print.push_back(L'\\');
	}

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
		wprintf(L"start monitoring directory: %s\n", dirToMonitor);
		std::wstring tmpStr;
		RefreshCtx refreshCtx(dirToMonitor, hRefreshFinished);
		for (;;)
		{
			DWORD wait = WaitForMultipleObjects(3, waitHandles, FALSE, INFINITE);
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
					processChanges(&refreshCtx, bufChanges, bytesReturned, &stats);
					if (refreshCtx.printChangedFiles)
					{
						printChanges(bufChanges, bytesReturned, root_dir_for_print, &tmpStr);
					}
					
					printStats(refreshCtx.printStats, stats, refreshCtx.getFileCount(), refreshCtx.refreshRunning(), opts.printStatsEveryMillis);

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
						StartRefresh(&refreshCtx);
					}
					else if (key == 'f')
					{
						refreshCtx.printChangedFiles = !refreshCtx.printChangedFiles;
						printf("printing changed files is now %s\n", refreshCtx.printChangedFiles ? "ON" : "OFF");
					}
					else if (key == 's')
					{
						refreshCtx.printStats = !refreshCtx.printStats;
						printf("printing statistics is now %s\n", refreshCtx.printStats ? "ON" : "OFF");
					}
					else if (key == 'S')
					{
						printStats(true, stats, refreshCtx.getFileCount(), refreshCtx.refreshRunning(), opts.printStatsEveryMillis);
					}
				}
			}
			else if (wait == WAIT_IDX_refreshFinished)
			{
				printStats(refreshCtx.printStats, stats, refreshCtx.getFileCount(), refreshCtx.refreshRunning(), opts.printStatsEveryMillis);
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

	if (!TryToSetPrivilege(SE_BACKUP_NAME, TRUE))
	{
		fwprintf(stderr, L"W could not set privilege SE_BACKUP_NAME\n");
	}

	LPCWSTR dirToMonitor = argv[1];
	LPVOID bufChanges = NULL;
	const DWORD bufChangesSize = 64 * 1024;
	HANDLE hDir = NULL;
	HANDLE hEventReadChanges = NULL;
	HANDLE hRefreshFinished = NULL;
	LastError err;
	Options opts;

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
		err.set(L"CreateFileW", dirToMonitor);
	}
	else if ((hEventReadChanges = CreateEventW(NULL, FALSE, FALSE, NULL)) == NULL)
	{
		err.set(L"CreateEventW", L"hEventReadChanges");
	}
	else if ((hRefreshFinished = CreateEventW(NULL, FALSE, FALSE, NULL)) == NULL)
	{
		err.set(L"CreateEventW", L"hRefreshFinished");
	}
	else
	{
		StartMonitor(dirToMonitor, hDir, hEventReadChanges, hRefreshFinished, bufChanges, bufChangesSize, opts, &err);
	}

	CloseHandle_mayBeNullOrInvalid(hEventReadChanges);
	CloseHandle_mayBeNullOrInvalid(hRefreshFinished);
	CloseHandle_mayBeNullOrInvalid(hDir);
	if (bufChanges != NULL) { HeapFree(GetProcessHeap(), 0, bufChanges); }

	if (err.failed())
	{
		err.print();
	}

	return err.code();
}
