#include "pch.h"

//------------------------------------------------------------
BOOL IsDotDir(LPCWSTR cFileName, const DWORD dwFileAttributes) {
//------------------------------------------------------------

	if ((dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) == 0) return FALSE;
	if (cFileName[0] != L'.')	return FALSE;
	if (cFileName[1] == L'\0')	return TRUE;
	if (cFileName[1] != L'.')	return FALSE;
	if (cFileName[2] == L'\0')	return TRUE;

	return FALSE;
}

LastError* EnumDirRecurse(std::wstring* dir, WIN32_FIND_DATA* findData, const std::function<void(const std::wstring& fullEntryname, WIN32_FIND_DATA* findData)> &onFileEntry, LastError *err)
{
	HANDLE hSearch;
	DWORD dwError = 0;

	dir->append(L"\\*");
	hSearch = FindFirstFileW(dir->c_str(), findData);
	dir->resize(dir->size() - 2);

	if (hSearch == INVALID_HANDLE_VALUE)
	{
		err->set(L"FindFirstFileW");
		return err;
	}

	do
	{
		if (!IsDotDir(findData->cFileName, findData->dwFileAttributes))
		{
			const size_t lastSizeDirectory = dir->length();
			dir->push_back(L'\\');
			dir->append(findData->cFileName);

			if ( (findData->dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0 )
			{
				EnumDirRecurse(dir, findData, onFileEntry, err);
			}
			else
			{
				onFileEntry(*dir, findData);
			}

			dir->resize(lastSizeDirectory);
		}
	} while (FindNextFileW(hSearch, findData));

	dwError = GetLastError();
	if (dwError == ERROR_NO_MORE_FILES)
	{
		dwError = NO_ERROR;
	}
	else
	{
		err->set(L"FindNextFileW", dwError);
	}

	if (hSearch != INVALID_HANDLE_VALUE)
	{
		FindClose(hSearch);
	}

	return err;
}