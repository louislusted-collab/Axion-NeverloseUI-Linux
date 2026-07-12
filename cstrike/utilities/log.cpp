// used: [crt] time_t, time, localtime_s
#include <ctime>

#include "log.h"
// using: mem_stackalloc, mem_stackfree
#include "memory.h"
// used: IsPowerOfTwo
#include "math.h"
// used: GetWorkingPath
#include "../core.h"

// console write stream
static HANDLE hConsoleStream = INVALID_HANDLE_VALUE;
// file write stream
static HANDLE hFileStream = INVALID_HANDLE_VALUE;

#pragma region log_main
bool L::AttachConsole(const wchar_t* wszWindowTitle)
{
#ifdef _WIN32
	// allocate memory for console
	if (::AllocConsole() != TRUE)
		return false;

	// open console output stream
	if (hConsoleStream = ::CreateFileW(L"CONOUT$", GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr); hConsoleStream == INVALID_HANDLE_VALUE)
		return false;

	if (::SetStdHandle(STD_OUTPUT_HANDLE, hConsoleStream) != TRUE)
		return false;

	if (::SetConsoleTitleW(wszWindowTitle) != TRUE)
		return false;
#else
	(void)wszWindowTitle;
	hConsoleStream = (HANDLE)stdout; // Linux: use stdout directly
#endif
	return true;
}

void L::DetachConsole()
{
#ifdef _WIN32
	::CloseHandle(hConsoleStream);
	if (::FreeConsole() != TRUE)
		return;
	if (const HWND hConsoleWindow = ::GetConsoleWindow(); hConsoleWindow != nullptr)
		::PostMessageW(hConsoleWindow, WM_CLOSE, 0U, 0L);
#else
	hConsoleStream = INVALID_HANDLE_VALUE;
#endif
}

bool L::OpenFile(const wchar_t* wszFileName)
{
	wchar_t wszFilePath[MAX_PATH];
	if (!CORE::GetWorkingPath(wszFilePath))
		return false;

	CRT::StringCat(wszFilePath, wszFileName);

	// @todo: append time/date to filename and always keep up to 3 files, otherwise delete with lowest date
	// open file output stream
	if (hFileStream = ::CreateFileW(wszFilePath, GENERIC_WRITE, FILE_SHARE_READ, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr); hFileStream == INVALID_HANDLE_VALUE)
		return false;

	// insert UTF-8 BOM
	::WriteFile(hFileStream, "\xEF\xBB\xBF", 3UL, nullptr, nullptr);

	return true;
}

void L::CloseFile()
{
	::CloseHandle(hFileStream);
}

void L::WriteMessage(const char* szMessage, const std::size_t nMessageLength)
{
#ifdef CS_LOG_CONSOLE
#ifdef _WIN32
	::WriteConsoleA(hConsoleStream, szMessage, nMessageLength, nullptr, nullptr);
#else
	fwrite(szMessage, 1, nMessageLength, (FILE*)hConsoleStream);
	fflush((FILE*)hConsoleStream);
#endif
#endif
#ifdef CS_LOG_FILE
	::WriteFile(hFileStream, szMessage, nMessageLength, nullptr, nullptr);
#endif
}
#pragma endregion

#pragma region log_stream_control
L::Stream_t::ColorMarker_t L::SetColor(const LogColorFlags_t nColorFlags)
{
	return { nColorFlags };
}

L::Stream_t::PrecisionMarker_t L::SetPrecision(const int iPrecision)
{
	return { iPrecision };
}

L::Stream_t::ModeMarker_t L::AddFlags(const LogModeFlags_t nModeFlags)
{
	return { nModeFlags };
}

L::Stream_t::ModeMarker_t L::RemoveFlags(const LogModeFlags_t nModeFlags)
{
	return { static_cast<LogModeFlags_t>(nModeFlags | LOG_MODE_REMOVE) };
}
#pragma endregion

L::Stream_t& L::Stream_t::operator()(const ELogLevel nLevel, const char* szFileBlock)
{
#if defined(CS_LOG_CONSOLE) || defined(CS_LOG_FILE)
	// reset previous flags
	nModeFlags = LOG_MODE_NONE;

	const char* szTypeBlock = nullptr;
	[[maybe_unused]] LogColorFlags_t nTypeColorFlags = LOG_COLOR_DEFAULT;

	switch (nLevel)
	{
	case LOG_INFO:
		szTypeBlock = "[info] ";
		nTypeColorFlags = LOG_COLOR_FORE_CYAN;
		break;
	case LOG_WARNING:
		szTypeBlock = "[warning] ";
		nTypeColorFlags = LOG_COLOR_FORE_YELLOW;
		break;
	case LOG_ERROR:
		szTypeBlock = "[error] ";
		nTypeColorFlags = LOG_COLOR_FORE_RED;
		break;
	default:
		break;
	}

	const std::time_t time = std::time(nullptr);
	std::tm timePoint;
#ifdef _WIN32
	localtime_s(&timePoint, &time);
#else
	localtime_r(&time, &timePoint);
#endif

	// @todo: no new line at first use / ghetto af but cheap enough but still ghetto uhhh
	char szTimeBuffer[32];
	const std::size_t nTimeSize = CRT::TimeToString(szTimeBuffer, sizeof(szTimeBuffer), "\n[%d-%m-%Y %T] ", &timePoint) - bFirstPrint;

#ifdef CS_LOG_CONSOLE
#ifdef _WIN32
	::SetConsoleTextAttribute(hConsoleStream, FOREGROUND_GREEN | FOREGROUND_INTENSITY);
	::WriteConsoleA(hConsoleStream, szTimeBuffer + bFirstPrint, nTimeSize, nullptr, nullptr);

	if (szFileBlock != nullptr)
	{
		::SetConsoleTextAttribute(hConsoleStream, FOREGROUND_INTENSITY);
		::WriteConsoleA(hConsoleStream, szFileBlock, CRT::StringLength(szFileBlock), nullptr, nullptr);
	}

	if (szTypeBlock != nullptr)
	{
		::SetConsoleTextAttribute(hConsoleStream, static_cast<WORD>(nTypeColorFlags));
		::WriteConsoleA(hConsoleStream, szTypeBlock, CRT::StringLength(szTypeBlock), nullptr, nullptr);
	}

	::SetConsoleTextAttribute(hConsoleStream, FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE);
#else // Linux — use ANSI escape codes
	FILE* out = (FILE*)hConsoleStream;
	if (!out) out = stdout;
	fprintf(out, "\033[1;32m"); // bright green for timestamp
	fwrite(szTimeBuffer + bFirstPrint, 1, nTimeSize, out);
	if (szFileBlock) { fprintf(out, "\033[1m"); fwrite(szFileBlock, 1, CRT::StringLength(szFileBlock), out); }
	if (szTypeBlock) {
		// pick color by level
		const char* ansi = "\033[0m";
		if (nLevel == LOG_INFO)    ansi = "\033[1;36m";
		else if (nLevel == LOG_WARNING) ansi = "\033[1;33m";
		else if (nLevel == LOG_ERROR)   ansi = "\033[1;31m";
		fprintf(out, "%s", ansi);
		fwrite(szTypeBlock, 1, CRT::StringLength(szTypeBlock), out);
	}
	fprintf(out, "\033[0m"); // reset
#endif
#endif
#ifdef CS_LOG_FILE
	::WriteFile(hFileStream, szTimeBuffer + bFirstPrint, nTimeSize, nullptr, nullptr);

	char szBlockBuffer[MAX_PATH] = { '\0' };
	char* szCurrentBlock = szBlockBuffer;

	if (szFileBlock != nullptr)
		szCurrentBlock = CRT::StringCat(szCurrentBlock, szFileBlock);

	if (szTypeBlock != nullptr)
		szCurrentBlock = CRT::StringCat(szCurrentBlock, szTypeBlock);

	if (szBlockBuffer[0] != '\0')
		::WriteFile(hFileStream, szBlockBuffer, static_cast<DWORD>(szCurrentBlock - szBlockBuffer), nullptr, nullptr);
#endif

	bFirstPrint = false;
#endif
	return *this;
}

L::Stream_t& L::Stream_t::operator<<(const ColorMarker_t colorMarker)
{
#ifdef CS_LOG_CONSOLE
#ifdef _WIN32
	::SetConsoleTextAttribute(hConsoleStream, static_cast<WORD>(colorMarker.nColorFlags));
#else
	(void)colorMarker;
#endif
#endif
	return *this;
}

L::Stream_t& L::Stream_t::operator<<(const PrecisionMarker_t precisionMarker)
{
#if defined(CS_LOG_CONSOLE) || defined(CS_LOG_FILE)
	this->iPrecision = precisionMarker.iPrecision;
#endif
	return *this;
}

L::Stream_t& L::Stream_t::operator<<(const ModeMarker_t modeMarker)
{
#if defined(CS_LOG_CONSOLE) || defined(CS_LOG_FILE)
	CS_ASSERT(nModeFlags == 0U || MATH::IsPowerOfTwo(nModeFlags & LOG_MODE_INT_FORMAT_MASK)); // used conflicting format flags

	if (modeMarker.nModeFlags & LOG_MODE_REMOVE)
		nModeFlags &= ~modeMarker.nModeFlags;
	else
		nModeFlags |= modeMarker.nModeFlags;
#endif
	return *this;
}

L::Stream_t& L::Stream_t::operator<<(const char* szMessage)
{
#if defined(CS_LOG_CONSOLE) || defined(CS_LOG_FILE)
	WriteMessage(szMessage, CRT::StringLength(szMessage));
#endif
	return *this;
}

L::Stream_t& L::Stream_t::operator<<(const wchar_t* wszMessage)
{
#if defined(CS_LOG_CONSOLE) || defined(CS_LOG_FILE)
	/*
	 * to keep stream orientation always same, convert message to UTF-8
	 *
	 * regarding to C++ standard:
	 * [C++11: 27.4.1/3]:
	 *    mixing operations on corresponding wide- and narrow-character streams follows the same semantics as mixing such operations on 'FILE's, as specified in amendation [1] of the ISO C standard
	 *
	 * [1]:
	 *    the definition of a stream was changed to include the concept of an orientation for both text and binary streams.
	 *    after a stream is associated with a file, but before any operations are performed on the stream, the stream is without orientation.
	 *    if a wide-character input or output function is applied to a stream without orientation, the stream becomes wide-oriented.
	 *    likewise, if a byte input or output operation is applied to a stream with orientation, the stream becomes byte-oriented.
	 *    thereafter, only the 'fwide()' or 'freopen()' functions can alter the orientation of a stream.
	 *    byte input/output functions shall not be applied to a wide-oriented stream and wide-character input/output functions shall not be applied to a byte-oriented stream.
	 */
	const std::size_t nMessageLength = CRT::StringLengthMultiByte(wszMessage);
	char* szMessage = static_cast<char*>(MEM_STACKALLOC(nMessageLength + 1U));
	CRT::StringUnicodeToMultiByte(szMessage, nMessageLength + 1U, wszMessage);

	WriteMessage(szMessage, nMessageLength);

	MEM_STACKFREE(szMessage);
#endif
	return *this;
}

L::Stream_t& L::Stream_t::operator<<(const bool bValue)
{
#if defined(CS_LOG_CONSOLE) || defined(CS_LOG_FILE)
	const char* szBoolean = ((nModeFlags & LOG_MODE_BOOL_ALPHA) ? (bValue ? "true" : "false") : (bValue ? "1" : "0"));
	const std::size_t nBooleanLength = CRT::StringLength(szBoolean);

	WriteMessage(szBoolean, nBooleanLength);
#endif
	return *this;
}