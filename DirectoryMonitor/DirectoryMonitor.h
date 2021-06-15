#pragma once

class RefreshCtx
{
private:

	std::atomic<size_t>	_fileCounter{ 0 };
	HANDLE _hEventRefreshFinished;

public:
	const std::wstring rootDir;
	
	std::mutex mutex_notify_vs_enum;
	std::atomic<std::unordered_set<std::wstring>*> files;

	bool printChangedFiles = true;
	bool printStats = false;

	RefreshCtx(LPCWSTR dir, HANDLE hEventRefreshFinished) : rootDir(dir)
	{
		files = nullptr;
		_hEventRefreshFinished = hEventRefreshFinished;
	}

	bool refreshRunning()
	{
		return files.load() != nullptr;
	}

	void SetFinishedEvent()
	{
		if (_hEventRefreshFinished != NULL)
		{
			SetEvent(_hEventRefreshFinished);
		}
	}

	void    setFileCount(size_t value) { _fileCounter.store(value); }
	size_t	getFileCount() const { return _fileCounter.load(); }
	void numberFilesIncrement() { _fileCounter++; }
	void numberFilesDecrement()
	{
		size_t value = _fileCounter.load();
		if (value == 0)
		{
			return;
		}

		for (;;)
		{
			size_t newValue = value - 1;
			if (_fileCounter.compare_exchange_weak(
				value		    // expected
				, newValue))	// desired
			{
				break;
			}
			else
			{
				value = _fileCounter.load();
			}
		}
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
};

class Options
{
public:
	size_t	printStatsEveryMillis = 1000;
};

