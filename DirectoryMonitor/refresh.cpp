#include "pch.h"

#include "DirectoryMonitor.h"
#include "LastError.h"
#include "util.h"

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
	if (!refreshCtx->refreshRunning())
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
