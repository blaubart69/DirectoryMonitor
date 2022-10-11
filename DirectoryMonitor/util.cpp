#include "pch.h"

template<class T, class C>
void walk_NextEntryOffset_buffer(void* buf, size_t length, C onEntry)
{
	T* info = (T*)buf;

	for (;;)
	{
		onEntry(info);

		if (info->NextEntryOffset == 0)
		{
			break;
		}
		else
		{
			info = (T*)((char*)info + info->NextEntryOffset);
		}
	}
}

void walk_FILE_NOTIFY_INFORMATION(void* buf, size_t length, std::function<void(DWORD,std::wstring_view)> onEntry)
{
	walk_NextEntryOffset_buffer<FILE_NOTIFY_INFORMATION>(
		buf
		, length
		, [&](FILE_NOTIFY_INFORMATION* info)
		{
			onEntry(
				info->Action
				, std::wstring_view(
					&(info->FileName[0])
					, (size_t)(info->FileNameLength / sizeof(WCHAR))));
		}
	);
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
	GetLocalTime(&localTime);

	return
		swprintf_s(
			buf
			, cchBuf
			, L"%04u-%02u-%02u %02u:%02u:%02u"
			, localTime.wYear, localTime.wMonth, localTime.wDay
			, localTime.wHour, localTime.wMinute, localTime.wSecond);
}

bool ReadConsoleKey(HANDLE hStdin, WCHAR* key)
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
		*key = record.Event.KeyEvent.uChar.UnicodeChar;
		return true;
	}

	return false;
}
