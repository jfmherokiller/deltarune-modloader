// append_log.hpp - line logger for loader modules, without the C runtime's FILE*.
//
// Modules built against the shared CRT (/MD) share its file-descriptor table with the game.
// A module that opens its log with fopen before the game's CRT has set up stdio can get
// descriptor 1, and the game's stdout then lands in that log (seen with Dwarf Eats Mountain).
// Win32 handles avoid that. Several modules and processes may append to the same file.
#pragma once
#include <windows.h>
#include <cstdarg>
#include <cstdio>

namespace modloader
{
	class AppendLog
	{
	public:
		// `tag` is written after the timestamp, e.g. "[loosemods] "; it must outlive the log.
		bool Open(const wchar_t* path, const char* tag = "")
		{
			m_Tag = tag;
			m_File = CreateFileW(path, FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
				nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
			return m_File != INVALID_HANDLE_VALUE;
		}

		bool IsOpen() const { return m_File != INVALID_HANDLE_VALUE; }

		void Write(const char* fmt, ...)
		{
			va_list ap;
			va_start(ap, fmt);
			WriteV(fmt, ap);
			va_end(ap);
		}

		void WriteV(const char* fmt, va_list ap)
		{
			if (m_File == INVALID_HANDLE_VALUE)
				return;
			char line[4096];
			SYSTEMTIME t;
			GetLocalTime(&t);
			int n = snprintf(line, sizeof line, "[%02d:%02d:%02d.%03d pid %lu] %s", t.wHour, t.wMinute, t.wSecond,
				t.wMilliseconds, (unsigned long)GetCurrentProcessId(), m_Tag);
			if (n < 0 || n >= (int)sizeof line)
				return;
			int m = vsnprintf(line + n, sizeof line - n - 2, fmt, ap);
			if (m < 0)
				return;
			n = n + m < (int)sizeof line - 2 ? n + m : (int)sizeof line - 3;
			line[n++] = '\r';
			line[n++] = '\n';
			DWORD written;
			WriteFile(m_File, line, (DWORD)n, &written, nullptr);   // one append per line
		}

	private:
		HANDLE m_File = INVALID_HANDLE_VALUE;
		const char* m_Tag = "";
	};
}
