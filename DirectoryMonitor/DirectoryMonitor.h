#pragma once

class RefreshCtx
{
private:

	std::atomic<size_t>	_fileCounter{ 0 };
	HANDLE _hEventRefreshFinished;

public:
	const std::wstring								rootDir;
	std::mutex										mutex_notify_vs_enum;
	std::atomic<std::unordered_set<std::wstring>*>	files;

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

	void setFileCount(size_t value) 
	{ 
		_fileCounter.store(value); 
	}

	size_t getFileCount() const 
	{ 
		return _fileCounter.load(); 
	}

	void numberFilesIncrement() 
	{ 
		_fileCounter++; 
	}

	void numberFilesDecrement()
	{
		size_t value = _fileCounter.load();
		
		for (;;)
		{
			if (value == 0)
			{
				break;
			}

			if (_fileCounter.compare_exchange_weak(
				  value		    // expected
				, value - 1 ))	// desired
			{
				break;
			}
			else
			{
				// value has new _fileCounter loaded
			}
		}
	}
};

struct Changes
{
public:
	size_t	added = 0;
	size_t	removed = 0;
	size_t	modified = 0;
	size_t  renamed = 0;

	size_t  largest_change_bytes = 0;
	size_t  largest_change_files = 0;
	size_t  overall_notify_bytes = 0;

	size_t  Notifications() const
	{
		return added + removed + modified + renamed;
	}
};

struct Options
{
	size_t	printStatsEveryMillis = 1000;
	LPCWSTR dirToMonitor = nullptr;
	DWORD dwNotifyFilter;
};

