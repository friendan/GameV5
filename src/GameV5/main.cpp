#include "StdAfx.h"
#include "ServerAction.h"
#include "duilib.h"
#include "resource.h"
#include <shellapi.h>
#include <string>
#include <vector>

namespace {

std::string ToUtf8(const std::wstring& text)
{
	if( text.empty() )
		return std::string();
	const int bytes = ::WideCharToMultiByte(CP_UTF8, 0, text.c_str(), (int)text.size(), NULL, 0, NULL, NULL);
	if( bytes <= 0 )
		return std::string();
	std::string out((size_t)bytes, '\0');
	::WideCharToMultiByte(CP_UTF8, 0, text.c_str(), (int)text.size(), &out[0], bytes, NULL, NULL);
	return out;
}

std::wstring FromUtf8(const std::string& text)
{
	if( text.empty() )
		return std::wstring();
	const int chars = ::MultiByteToWideChar(CP_UTF8, 0, text.data(), (int)text.size(), NULL, 0);
	if( chars <= 0 )
		return std::wstring();
	std::wstring out((size_t)chars, L'\0');
	::MultiByteToWideChar(CP_UTF8, 0, text.data(), (int)text.size(), &out[0], chars);
	return out;
}

void WriteText(HANDLE output, const std::wstring& text)
{
	if( output == NULL || output == INVALID_HANDLE_VALUE )
		return;
	DWORD written = 0;
	if( ::WriteConsoleW(output, text.c_str(), (DWORD)text.size(), &written, NULL) )
		return;
	const std::string utf8 = ToUtf8(text);
	::WriteFile(output, utf8.data(), (DWORD)utf8.size(), &written, NULL);
}

void WriteLine(HANDLE console, HANDLE redirected, const std::wstring& line)
{
	const std::wstring text = line + L"\r\n";
	WriteText(console, text);
	if( redirected != console )
		WriteText(redirected, text);
}

bool OpenTestOutput(HANDLE& console, HANDLE& redirected, bool& ownConsole)
{
	ownConsole = false;
	STARTUPINFOW startup = {};
	startup.cb = sizeof(startup);
	::GetStartupInfoW(&startup);
	redirected = INVALID_HANDLE_VALUE;
	const HANDLE stdOut = ::GetStdHandle(STD_OUTPUT_HANDLE);
	const DWORD stdKind = (stdOut == NULL || stdOut == INVALID_HANDLE_VALUE) ? FILE_TYPE_UNKNOWN : ::GetFileType(stdOut);
	if( stdKind == FILE_TYPE_DISK || stdKind == FILE_TYPE_PIPE )
		redirected = stdOut;
	else if( (startup.dwFlags & STARTF_USESTDHANDLES) != 0 )
		redirected = startup.hStdOutput;

	if( !::AttachConsole(ATTACH_PARENT_PROCESS) ) {
		if( !::AllocConsole() )
			return redirected != NULL && redirected != INVALID_HANDLE_VALUE;
		ownConsole = true;
		::SetConsoleTitleW(L"GameV5");
	}
	console = ::CreateFileW(L"CONOUT$", GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0, NULL);
	return console != INVALID_HANDLE_VALUE || (redirected != NULL && redirected != INVALID_HANDLE_VALUE);
}

void WriteUsage(HANDLE console, HANDLE redirected)
{
	WriteLine(console, redirected, L"GameV5_mtd.exe --decpw <dbpasswd>");
	WriteLine(console, redirected, L"可重复指定。明文原样输出，密文输出解密后的密码。不带参数时打开窗口。");
}

// 没有测试参数时返回 -1，由窗口继续启动。
int RunTestArgs()
{
	int argc = 0;
	LPWSTR* argv = ::CommandLineToArgvW(::GetCommandLineW(), &argc);
	if( argv == NULL || argc <= 1 )
		return -1;

	std::vector<std::wstring> passwords;
	bool help = false;
	bool bad = false;
	for( int i = 1; i < argc; ++i ) {
		if( wcscmp(argv[i], L"--decpw") == 0 ) {
			if( i + 1 >= argc ) {
				bad = true;
				break;
			}
			passwords.push_back(argv[++i]);
		}
		else if( wcscmp(argv[i], L"--help") == 0 || wcscmp(argv[i], L"-h") == 0 ) {
			help = true;
		}
		else {
			bad = true;
		}
	}
	::LocalFree(argv);
	if( !help && !bad && passwords.empty() )
		return -1;

	HANDLE console = INVALID_HANDLE_VALUE;
	HANDLE redirected = INVALID_HANDLE_VALUE;
	bool ownConsole = false;
	if( !OpenTestOutput(console, redirected, ownConsole) )
		return 1;
	int code = 0;
	if( help || bad ) {
		WriteUsage(console, redirected);
		code = help && !bad ? 0 : 1;
	}
	else {
		for( size_t i = 0; i < passwords.size(); ++i ) {
			const std::string stored = ToUtf8(passwords[i]);
			const std::string plain = DecryptDbPassword(stored);
			WriteLine(console, redirected, L"in=" + passwords[i]);
			WriteLine(console, redirected, L"out=" + FromUtf8(plain));
			WriteLine(console, redirected, plain == stored ? L"same=1" : L"same=0");
			if( i + 1 < passwords.size() )
				WriteLine(console, redirected, L"");
		}
	}
	if( ownConsole ) {
		WriteLine(console, redirected, L"按回车关闭");
		HANDLE input = ::CreateFileW(L"CONIN$", GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0, NULL);
		if( input != INVALID_HANDLE_VALUE ) {
			wchar_t key[4] = {};
			DWORD read = 0;
			::ReadConsoleW(input, key, 1, &read, NULL);
			::CloseHandle(input);
		}
	}
	if( console != INVALID_HANDLE_VALUE )
		::CloseHandle(console);
	::FreeConsole();
	return code;
}

} // namespace

int APIENTRY _tWinMain(HINSTANCE hInstance, HINSTANCE /*hPrevInstance*/, LPTSTR /*lpCmdLine*/, int /*nCmdShow*/)
{
	const int testCode = RunTestArgs();
	if( testCode >= 0 )
		return testCode;

	HRESULT hr = ::CoInitialize(NULL);
	if( FAILED(hr) ) return 0;
	::OleInitialize(NULL);

	CPaintManagerUI::SetInstance(hInstance);
	CPaintManagerUI::SetResourceType(UILIB_ZIPRESOURCE);

	CDuiString path = CPaintManagerUI::GetInstancePath();
	path += _T("skin\\GameV5\\");
	CPaintManagerUI::SetResourcePath(path.GetData());

	HRSRC hRes = ::FindResource(hInstance, MAKEINTRESOURCE(IDR_GAMEV5_SKIN), _T("ZIPRES"));
	if( hRes != NULL ) {
		HGLOBAL hGlobal = ::LoadResource(hInstance, hRes);
		DWORD size = ::SizeofResource(hInstance, hRes);
		if( hGlobal != NULL && size > 0 )
			CPaintManagerUI::SetResourceZip((LPBYTE)::LockResource(hGlobal), size);
	}

	if( CThemeManager* tm = CThemeManager::GetInstance() ) {
		tm->SetDefaultThemeId(_T("dark"));
		tm->ApplyTheme(_T("dark"));
	}

	CMainWnd* pMain = new CMainWnd();
	pMain->Create(NULL, _T("GameV5"), UI_WNDSTYLE_FRAME, 0L, 0, 0, 1280, 800);
	pMain->ShowWindow(true);

	CPaintManagerUI::MessageLoop();

	delete pMain;
	::OleUninitialize();
	::CoUninitialize();
	return 0;
}
