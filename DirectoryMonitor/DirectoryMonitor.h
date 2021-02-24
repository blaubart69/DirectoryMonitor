#pragma once

class RefreshCtx
{
public:
	const std::wstring rootDir;
	std::mutex mutex_notify_vs_enum;
	std::atomic<bool> refreshRunning;
	std::atomic<std::unordered_set<std::wstring>*> files;

	RefreshCtx(LPCWSTR dir) : rootDir(dir)
	{
		refreshRunning = false;
		files = nullptr;
	}
};

class Stats
{
public:
	size_t	changes = 0;

	size_t	added = 0;
	size_t	removed = 0;
	size_t	modified = 0;
	size_t  renamed = 0;

	size_t  largest_change_bytes = 0;
	size_t  largest_change_files = 0;
	size_t  overall_notify_bytes = 0;

	std::atomic<size_t>	numberFiles{ 0 };
};

