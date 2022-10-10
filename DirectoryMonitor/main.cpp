#include "pch.h"

#include "DirectoryMonitor.h"
#include "LastError.h"
#include "util.h"

void StartRefresh(RefreshCtx* refreshCtx);

void printStats(const Changes& stats, size_t fileCount, bool refreshRunning, size_t everyMilliseconds)
{
	static size_t last_added = 0;
	static size_t last_removed = 0;
	static size_t last_modified = 0;
	static size_t last_renamed = 0;

	static ULONGLONG last_ticks = 0;

	ULONGLONG currentTicks = GetTickCount64();
	if ((currentTicks - last_ticks) < everyMilliseconds)
	{
		return;
	}

	wprintf(L"files%s: %zu | +/-/mod/ren: %zu(%zu)/%zu(%zu)/%zu(%zu)/%zu(%zu) | notify records/bytes: %zu/%s | max files/bytes: %zu/%s\n"
		, refreshRunning ? L"(refresh running)" : L""
		, fileCount
		, stats.added, (stats.added - last_added)
		, stats.removed, (stats.removed - last_removed)
		, stats.modified, (stats.modified - last_modified)
		, stats.renamed, (stats.renamed - last_renamed)
		, stats.Countchanges()
		, FormatByteSize(stats.overall_notify_bytes).c_str()
		, stats.largest_change_files
		, FormatByteSize(stats.largest_change_bytes).c_str()
	);

	last_added = stats.added;
	last_removed = stats.removed;
	last_modified = stats.modified;
	last_renamed = stats.renamed;
	last_ticks = currentTicks;
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
	WCHAR localtime_string[20];
	localtime_as_str(localtime_string, sizeof(localtime_string) / sizeof(WCHAR));

	str->clear();

	walk_FILE_NOTIFY_INFORMATION(buf, bytesReturned,
		[&](DWORD action, std::wstring_view filename)
		{
			str->append(localtime_string);
			str->push_back(L'\t');
			str->append(getActionname(action));
			str->append(root_dir);
			str->append(filename);
			str->push_back(L'\r');
			str->push_back(L'\n');
		});

	wprintf(L"%s", str->c_str());
}

void processChanges(RefreshCtx* ctx, LPVOID buf, DWORD bytesReturned, Changes *changes)
{
	size_t countChanges = 0;
	{
		const std::lock_guard<std::mutex> lock_hashtable(ctx->mutex_notify_vs_enum);
		std::unordered_set<std::wstring>* set_of_files = ctx->files.load();
		
		walk_FILE_NOTIFY_INFORMATION(buf, bytesReturned,
			[&](DWORD action, std::wstring_view filename)
			{
				countChanges += 1;
				if      (action == FILE_ACTION_ADDED)            { changes->added    += 1; }
				else if (action == FILE_ACTION_REMOVED)          { changes->removed  += 1; }
				else if (action == FILE_ACTION_MODIFIED)         { changes->modified += 1; }
				else if (action == FILE_ACTION_RENAMED_NEW_NAME) { changes->renamed  += 1; }

				if (set_of_files == nullptr)
				{
					if      (action == FILE_ACTION_ADDED)   { ctx->numberFilesIncrement(); }
					else if (action == FILE_ACTION_REMOVED) { ctx->numberFilesDecrement(); }
				}
				else
				{
					if (action == FILE_ACTION_ADDED)
					{
						set_of_files->emplace(filename);
					}
					else if (action == FILE_ACTION_REMOVED)
					{
						std::wstring tmpFilename(filename.data(), filename.length());
						set_of_files->erase(tmpFilename);
					}
				}
			});
	}
	changes->largest_change_files = std::max<size_t>(countChanges, changes->largest_change_files);
	changes->largest_change_bytes = std::max<size_t>((size_t)bytesReturned, changes->largest_change_bytes);
}

LastError* StartMonitor(LPCWSTR dirToMonitor, const HANDLE hDir, const HANDLE hEventReadChanges, const HANDLE hRefreshFinished, const LPVOID bufChanges, const DWORD bufChangesSize, const Options& opts, LastError* err)
{
	HANDLE hStdin = GetStdHandle(STD_INPUT_HANDLE);
	SetConsoleMode(hStdin, ENABLE_PROCESSED_INPUT);

	OVERLAPPED ovlReadDirectoryChanges = { 0 };
	ovlReadDirectoryChanges.hEvent = hEventReadChanges;
	HANDLE waitHandles[3];
	const DWORD WAIT_IDX_ReadChanges		= 0;
	const DWORD WAIT_IDX_stdin				= 1;
	const DWORD WAIT_IDX_refreshFinished	= 2;
	waitHandles[WAIT_IDX_ReadChanges]		= hEventReadChanges;
	waitHandles[WAIT_IDX_stdin]				= hStdin;
	waitHandles[WAIT_IDX_refreshFinished]	= hRefreshFinished;

	std::wstring root_dir_for_print(dirToMonitor);
	if (!root_dir_for_print.ends_with(L'\\'))
	{
		root_dir_for_print.push_back(L'\\');
	}

	DWORD bytesReturned;
	std::wstring tmpStr;
	Changes changes;
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
				err->set(L"GetOverlappedResult");
				break;
			}
			else if (bytesReturned == 0)
			{
				// this happens at the first time entering the loop.
				// hEventReadChanges has an initial state of TRUE
				// so we can save a "first call" to ReadDirectoryChangesW() on the outside of the loop
			}
			else
			{
				changes.overall_notify_bytes += bytesReturned;
				                                    processChanges(&refreshCtx, bufChanges, bytesReturned, &changes);
				if (refreshCtx.printChangedFiles) { printChanges  (bufChanges, bytesReturned, root_dir_for_print, &tmpStr);									    }
				if (refreshCtx.printStats)        { printStats    (changes, refreshCtx.getFileCount(), refreshCtx.refreshRunning(), opts.printStatsEveryMillis);  }
			}

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
				err->set(L"ReadDirectoryChangesW");
				break;
			}
		}
		else if (wait == WAIT_IDX_stdin)
		{
			WCHAR key;
			if (ReadConsoleKey(hStdin, &key))
			{
				if (key == L'r')
				{
					StartRefresh(&refreshCtx);
				}
				else if (key == L'f')
				{
					refreshCtx.printChangedFiles = !refreshCtx.printChangedFiles;
					printf("printing changed files is now %s\n", refreshCtx.printChangedFiles ? "ON" : "OFF");
				}
				else if (key == L's')
				{
					refreshCtx.printStats = !refreshCtx.printStats;
					printf("printing statistics is now %s\n", refreshCtx.printStats ? "ON" : "OFF");
				}
				else if (key == L'S')
				{
					printStats(changes, refreshCtx.getFileCount(), refreshCtx.refreshRunning(), opts.printStatsEveryMillis);
				}
			}
		}
		else if (wait == WAIT_IDX_refreshFinished)
		{
			if (refreshCtx.printStats)
			{
				printStats(changes, refreshCtx.getFileCount(), refreshCtx.refreshRunning(), opts.printStatsEveryMillis);
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

	if ( (hDir=CreateFileW(
		dirToMonitor
		, FILE_LIST_DIRECTORY
		, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE
		, NULL
		, OPEN_EXISTING
		, FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OVERLAPPED
		, NULL)) == INVALID_HANDLE_VALUE)
	{
		err.set(L"CreateFileW", dirToMonitor);
	}
	else if ((bufChanges = HeapAlloc(GetProcessHeap(), 0, bufChangesSize)) == NULL)
	{
		err.set(L"HeapAlloc", ERROR_NOT_ENOUGH_MEMORY);
	}
	else if ((hEventReadChanges = CreateEventW(NULL, FALSE, TRUE, NULL)) == NULL)
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
