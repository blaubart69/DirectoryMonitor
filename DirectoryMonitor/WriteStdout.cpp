#include "pch.h"

std::vector<char> _codepageConvertBuffer;

BOOL WriteStdout(LPCWSTR str, const DWORD len)
{
	const HANDLE _fp = GetStdHandle(STD_OUTPUT_HANDLE);

	DWORD written;
	if (GetFileType(_fp) == FILE_TYPE_CHAR)
	{
		return WriteConsoleW(_fp, str, len, &written, NULL);
	}
	else
	{
		const size_t reserved_capacity = len / sizeof(WCHAR) * 4;
		_codepageConvertBuffer.reserve(reserved_capacity);

		if (int multiBytesWritten; (multiBytesWritten = WideCharToMultiByte(
			GetConsoleOutputCP()
			, 0									// dwFlags [in]
			, str								// lpWideCharStr [in]
			, (int)len							// cchWideChar [in]
			, _codepageConvertBuffer.data()		// lpMultiByteStr [out, optional]
			, reserved_capacity					// cbMultiByte [in]
			, NULL								// lpDefaultChar[in, optional]
			, NULL								// lpUsedDefaultChar[out, optional]
		)) == 0)
		{
			return FALSE;
		}
		else
		{
			return WriteFile(
				_fp
				, _codepageConvertBuffer.data()
				, multiBytesWritten
				, &written
				, NULL);
		}
	}
}

bool WriteStdout(const std::wstring & str)
{
	return WriteStdout(str.data(), str.length());
}