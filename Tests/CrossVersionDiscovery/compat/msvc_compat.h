#pragma once

#include <cstdarg>
#include <cstdio>

#if !defined(_MSC_VER)
inline int sprintf_s(char* buffer, size_t bufferCount, const char* format, ...)
{
	va_list arguments;
	va_start(arguments, format);
	const int written = std::vsnprintf(buffer, bufferCount, format, arguments);
	va_end(arguments);
	return written;
}

// MSVC also supports sprintf_s(buffer, format, ...) using _TRUNCATE as implicit size.
inline int sprintf_s(char* buffer, const char* format, ...)
{
	va_list arguments;
	va_start(arguments, format);
	const int written = std::vsnprintf(buffer, 32, format, arguments);
	va_end(arguments);
	return written;
}
#endif
