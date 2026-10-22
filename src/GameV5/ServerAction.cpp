#include "StdAfx.h"
#include "ServerAction.h"
#include "hv/requests.h"
#include <bcrypt.h>
#include <mysql.h>
#include <wincrypt.h>
#include <ctime>
#include <fstream>
#include <shlwapi.h>
#include <tlhelp32.h>
#include <thread>
#include <vector>

namespace {

std::wstring ParentOf(const std::wstring& path)
{
	wchar_t buffer[1024] = {};
	wcsncpy_s(buffer, path.c_str(), _TRUNCATE);
	PathRemoveFileSpecW(buffer);
	return buffer;
}

std::wstring FileNameOf(const std::wstring& path)
{
	const wchar_t* name = PathFindFileNameW(path.c_str());
	if( name == NULL || name[0] == 0 )
		return std::wstring();
	return name;
}

std::wstring BriefBody(const std::string& body)
{
	std::wstring text;
	text.reserve(body.size());
	int wide = ::MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, body.data(), (int)body.size(), NULL, 0);
	UINT codePage = CP_UTF8;
	DWORD flags = MB_ERR_INVALID_CHARS;
	if( wide <= 0 ) {
		codePage = CP_ACP;
		flags = 0;
		wide = ::MultiByteToWideChar(codePage, flags, body.data(), (int)body.size(), NULL, 0);
	}
	if( wide > 0 ) {
		text.resize(wide);
		::MultiByteToWideChar(codePage, flags, body.data(), (int)body.size(), &text[0], wide);
	}
	for( size_t i = 0; i < text.size(); ++i ) {
		if( text[i] == L'\r' || text[i] == L'\n' || text[i] == L'\t' )
			text[i] = L' ';
	}
	if( text.size() > 80 )
		text.resize(80);
	return text;
}

void PostStatus(HWND notify, const std::wstring& text, LPARAM extra = 0)
{
	if( !::IsWindow(notify) )
		return;
	std::wstring* message = new std::wstring(text);
	if( !::PostMessageW(notify, WM_SERVER_GM_RESULT, (WPARAM)message, extra) )
		delete message;
}

std::wstring WidenUrl(const std::string& url)
{
	return std::wstring(url.begin(), url.end());
}

bool BuildStart(const ServerConfig& server, std::wstring& exe, std::wstring& workDir, std::wstring& argument, std::wstring& command, std::wstring& error)
{
	const std::wstring configDir = ParentOf(server.filePath);
	const std::wstring root = ParentOf(configDir);
	const std::wstring configName = FileNameOf(configDir);
	if( root.empty() || configName.empty() || server.fileName.empty() ) {
		error = L"起服失败：配置路径无效 " + server.filePath;
		return false;
	}
	workDir = root + L"\\skynet";
	exe = workDir + L"\\skynet.exe";
	argument = L"../" + configName + L"/" + server.fileName;
	command = L"\"" + exe + L"\" " + argument + L"  工作目录 " + workDir;
	if( ::GetFileAttributesW(exe.c_str()) == INVALID_FILE_ATTRIBUTES ) {
		error = L"起服失败：找不到 " + exe;
		return false;
	}
	return true;
}

std::wstring NormalizePathText(std::wstring text)
{
	for( size_t i = 0; i < text.size(); ++i ) {
		if( text[i] == L'\\' )
			text[i] = L'/';
		else
			text[i] = (wchar_t)towlower(text[i]);
	}
	return text;
}

bool HasBoundedText(const std::wstring& haystack, const std::wstring& needle, bool slashBefore)
{
	if( needle.empty() )
		return false;
	size_t pos = 0;
	while( (pos = haystack.find(needle, pos)) != std::wstring::npos ) {
		const size_t end = pos + needle.size();
		const bool leftOk = slashBefore || pos == 0 || haystack[pos - 1] == L' ' || haystack[pos - 1] == L'"' || haystack[pos - 1] == L'\t';
		const bool rightOk = end == haystack.size() || haystack[end] == L' ' || haystack[end] == L'"' || haystack[end] == L'\t';
		if( leftOk && rightOk )
			return true;
		pos = end;
	}
	return false;
}

bool CommandLineMatches(const std::wstring& commandLine, const std::wstring& argument, const std::wstring& filePath)
{
	const std::wstring haystack = NormalizePathText(commandLine);
	const std::wstring relativeArg = NormalizePathText(argument);
	const std::wstring fullPath = NormalizePathText(filePath);
	std::wstring tail = relativeArg;
	const size_t slash = relativeArg.find(L'/');
	if( slash != std::wstring::npos )
		tail = relativeArg.substr(slash);
	return HasBoundedText(haystack, relativeArg, false)
		|| HasBoundedText(haystack, tail, true)
		|| HasBoundedText(haystack, fullPath, false);
}

bool ReadProcessMemoryExact(HANDLE process, LPCVOID address, void* buffer, SIZE_T size)
{
	SIZE_T read = 0;
	return address != NULL && ::ReadProcessMemory(process, address, buffer, size, &read) && read == size;
}

bool ReadProcessCommandLine(HANDLE process, std::wstring& commandLine)
{
	typedef LONG (NTAPI* NtQueryInformationProcessFn)(HANDLE, ULONG, PVOID, ULONG, PULONG);
	static NtQueryInformationProcessFn query = (NtQueryInformationProcessFn)::GetProcAddress(::GetModuleHandleW(L"ntdll.dll"), "NtQueryInformationProcess");
	if( query == NULL )
		return false;

	struct BasicInfo
	{
		PVOID Reserved1;
		PVOID PebBaseAddress;
		PVOID Reserved2[2];
		ULONG_PTR UniqueProcessId;
		PVOID Reserved3;
	} info = {};
	if( query(process, 0, &info, sizeof(info), NULL) < 0 || info.PebBaseAddress == NULL )
		return false;

	ULONG_PTR wowPeb = 0;
	query(process, 26, &wowPeb, sizeof(wowPeb), NULL);

	USHORT length = 0;
	PVOID buffer = NULL;
	if( wowPeb != 0 ) {
		ULONG params = 0;
		if( !ReadProcessMemoryExact(process, (LPCVOID)(wowPeb + 0x10), &params, sizeof(params)) || params == 0 )
			return false;
		struct UnicodeString32
		{
			USHORT Length;
			USHORT MaximumLength;
			ULONG Buffer;
		} text = {};
		if( !ReadProcessMemoryExact(process, (LPCVOID)((ULONG_PTR)params + 0x40), &text, sizeof(text)) || text.Buffer == 0 || text.Length < sizeof(wchar_t) )
			return false;
		length = text.Length;
		buffer = (PVOID)(ULONG_PTR)text.Buffer;
	}
	else {
		PVOID params = NULL;
		if( !ReadProcessMemoryExact(process, (BYTE*)info.PebBaseAddress + 0x20, &params, sizeof(params)) || params == NULL )
			return false;
		struct UnicodeString64
		{
			USHORT Length;
			USHORT MaximumLength;
			ULONG pad;
			PVOID Buffer;
		} text = {};
		if( !ReadProcessMemoryExact(process, (BYTE*)params + 0x70, &text, sizeof(text)) || text.Buffer == NULL || text.Length < sizeof(wchar_t) )
			return false;
		length = text.Length;
		buffer = text.Buffer;
	}
	if( length > 32768 )
		length = 32768;
	commandLine.assign(length / sizeof(wchar_t), L'\0');
	if( !ReadProcessMemoryExact(process, buffer, &commandLine[0], commandLine.size() * sizeof(wchar_t)) )
		return false;
	while( !commandLine.empty() && commandLine.back() == L'\0' )
		commandLine.pop_back();
	return !commandLine.empty();
}

std::wstring BriefCommandLine(const std::wstring& commandLine)
{
	std::wstring text = commandLine;
	for( size_t i = 0; i < text.size(); ++i ) {
		if( text[i] == L'\r' || text[i] == L'\n' || text[i] == L'\t' )
			text[i] = L' ';
	}
	if( text.size() > 180 )
		text.resize(180);
	return text;
}

} // namespace

bool DescribeStart(const ServerConfig& server, std::wstring& display)
{
	std::wstring exe;
	std::wstring workDir;
	std::wstring argument;
	std::wstring error;
	if( !BuildStart(server, exe, workDir, argument, display, error) ) {
		display = error;
		return false;
	}
	display = L"正在启动服务器 " + std::to_wstring(server.hostId) + L"：" + display;
	return true;
}

bool StartSkynet(const ServerConfig& server, std::wstring& message)
{
	std::wstring exe;
	std::wstring workDir;
	std::wstring argument;
	std::wstring command;
	if( !BuildStart(server, exe, workDir, argument, command, message) )
		return false;
	const std::wstring commandLineText = L"\"" + exe + L"\" " + argument;
	std::vector<wchar_t> commandLine(commandLineText.begin(), commandLineText.end());
	commandLine.push_back(0);

	STARTUPINFOW startup = {};
	startup.cb = sizeof(startup);
	startup.dwFlags = STARTF_USESHOWWINDOW;
	startup.wShowWindow = SW_SHOWMAXIMIZED;
	PROCESS_INFORMATION process = {};
	const BOOL started = ::CreateProcessW(
		exe.c_str(),
		commandLine.data(),
		NULL,
		NULL,
		FALSE,
		CREATE_NEW_CONSOLE | CREATE_UNICODE_ENVIRONMENT,
		NULL,
		workDir.c_str(),
		&startup,
		&process);
	if( !started ) {
		message = L"起服失败：错误 " + std::to_wstring(::GetLastError()) + L"：" + command;
		return false;
	}
	::CloseHandle(process.hThread);
	::CloseHandle(process.hProcess);
	message = L"已启动服务器 " + std::to_wstring(server.hostId) + L" 进程 " + std::to_wstring(process.dwProcessId) + L"：" + command;
	return true;
}

bool FindRunningSkynet(const ServerConfig& server, std::wstring& detail)
{
	std::wstring exe;
	std::wstring workDir;
	std::wstring argument;
	std::wstring command;
	std::wstring error;
	BuildStart(server, exe, workDir, argument, command, error);
	if( exe.empty() || argument.empty() ) {
		detail = error.empty() ? L"配置路径无效" : error;
		return false;
	}

	HANDLE snapshot = ::CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
	if( snapshot == INVALID_HANDLE_VALUE ) {
		detail = L"无法遍历进程，错误 " + std::to_wstring(::GetLastError());
		return false;
	}

	PROCESSENTRY32W entry = {};
	entry.dwSize = sizeof(entry);
	std::wstring mismatches;
	int mismatchCount = 0;
	bool found = false;
	DWORD foundPid = 0;
	std::wstring foundCommand;
	if( ::Process32FirstW(snapshot, &entry) ) {
		do {
			if( _wcsicmp(entry.szExeFile, L"skynet.exe") != 0 )
				continue;
			HANDLE process = ::OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_VM_READ, FALSE, entry.th32ProcessID);
			if( process == NULL )
				process = ::OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, entry.th32ProcessID);
			if( process == NULL )
				continue;
			wchar_t image[1024] = {};
			DWORD imageSize = 1024;
			const BOOL hasImage = ::QueryFullProcessImageNameW(process, 0, image, &imageSize);
			if( !hasImage || _wcsicmp(image, exe.c_str()) != 0 ) {
				::CloseHandle(process);
				continue;
			}
			std::wstring commandLine;
			const bool hasCommand = ReadProcessCommandLine(process, commandLine);
			::CloseHandle(process);
			if( !hasCommand ) {
				if( mismatchCount < 3 ) {
					if( !mismatches.empty() )
						mismatches += L" | ";
					mismatches += L"进程 " + std::to_wstring(entry.th32ProcessID) + L" 无法读取命令行";
				}
				++mismatchCount;
				continue;
			}
			if( CommandLineMatches(commandLine, argument, server.filePath) ) {
				found = true;
				foundPid = entry.th32ProcessID;
				foundCommand = commandLine;
				break;
			}
			if( mismatchCount < 3 ) {
				if( !mismatches.empty() )
					mismatches += L" | ";
				mismatches += L"进程 " + std::to_wstring(entry.th32ProcessID) + L" 命令行 " + BriefCommandLine(commandLine);
			}
			++mismatchCount;
		} while( ::Process32NextW(snapshot, &entry) );
	}
	::CloseHandle(snapshot);

	if( found ) {
		detail = L"已找到进程 " + std::to_wstring(foundPid) + L"，命令行 " + BriefCommandLine(foundCommand);
		return true;
	}
	if( mismatchCount > 0 )
		detail = L"未找到启动参数 " + argument + L"。同路径进程：" + mismatches;
	else
		detail = L"未找到进程 " + exe + L"，启动参数应包含 " + argument;
	return false;
}

void EnumerateSkynetProcesses(std::vector<SkynetProcess>& processes)
{
	processes.clear();
	HANDLE snapshot = ::CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
	if( snapshot == INVALID_HANDLE_VALUE )
		return;
	PROCESSENTRY32W entry = {};
	entry.dwSize = sizeof(entry);
	if( ::Process32FirstW(snapshot, &entry) ) {
		do {
			if( _wcsicmp(entry.szExeFile, L"skynet.exe") != 0 )
				continue;
			HANDLE process = ::OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_VM_READ, FALSE, entry.th32ProcessID);
			if( process == NULL )
				continue;
			wchar_t image[1024] = {};
			DWORD imageSize = 1024;
			const BOOL hasImage = ::QueryFullProcessImageNameW(process, 0, image, &imageSize);
			std::wstring commandLine;
			const bool hasCommand = hasImage && ReadProcessCommandLine(process, commandLine);
			::CloseHandle(process);
			if( !hasCommand )
				continue;
			SkynetProcess item;
			item.image = image;
			item.commandLine = commandLine;
			processes.push_back(item);
		} while( ::Process32NextW(snapshot, &entry) );
	}
	::CloseHandle(snapshot);
}

bool IsSkynetServerRunning(const ServerConfig& server, const std::vector<SkynetProcess>& processes)
{
	std::wstring exe;
	std::wstring workDir;
	std::wstring argument;
	std::wstring command;
	std::wstring error;
	BuildStart(server, exe, workDir, argument, command, error);
	if( exe.empty() || argument.empty() )
		return false;
	for( size_t i = 0; i < processes.size(); ++i ) {
		if( _wcsicmp(processes[i].image.c_str(), exe.c_str()) != 0 )
			continue;
		if( CommandLineMatches(processes[i].commandLine, argument, server.filePath) )
			return true;
	}
	return false;
}

void RequestGm(HWND notify, int hostId, int mcsPort, bool hotUpdate)
{
	const long long stamp = (long long)::time(NULL);
	std::vector<std::string> urls;
	char url[256] = {};
	if( hotUpdate ) {
		sprintf_s(url, "http://127.0.0.1:%d/manage/update_auto.lua?t=%lld", mcsPort, stamp);
		urls.push_back(url);
		sprintf_s(url, "http://127.0.0.1:%d/manage/collectgarbage.lua?t=%lld", mcsPort, stamp);
		urls.push_back(url);
	}
	else {
		sprintf_s(url, "http://127.0.0.1:%d/shutdown?t=%lld", mcsPort, stamp);
		urls.push_back(url);
	}
	const std::wstring action = hotUpdate ? L"热更" : L"停服";
	std::thread([notify, hostId, urls, action]() {
		const std::wstring prefix = action + L" " + std::to_wstring(hostId) + L"：";
		for( size_t i = 0; i < urls.size(); ++i ) {
			const std::wstring url = WidenUrl(urls[i]);
			const std::wstring step = std::to_wstring(i + 1) + L"/" + std::to_wstring(urls.size());
			PostStatus(notify, prefix + L"正在请求 " + step + L" " + url);
			auto request = std::make_shared<HttpRequest>();
			request->method = HTTP_GET;
			request->url = urls[i];
			request->timeout = 15;
			request->connect_timeout = 3;
			request->retry_count = 0;
			auto response = std::make_shared<HttpResponse>();
			const int ret = http_client_send(request.get(), response.get());
			if( ret != 0 ) {
				PostStatus(notify, prefix + step + L" 连接失败 错误 " + std::to_wstring(ret) + L" " + url);
				continue;
			}
			std::wstring text = prefix + step + L" 状态 " + std::to_wstring((int)response->status_code);
			const std::wstring body = BriefBody(response->body);
			if( !body.empty() )
				text += L" 返回 " + body;
			text += L" " + url;
			PostStatus(notify, text);
		}
	}).detach();
}

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

} // namespace

// Base64 后按 16 字节 AES-ECB、零填充。明文密码原样返回。
std::string DecryptDbPassword(const std::string& stored)
{
	if( stored.empty() )
		return stored;
	DWORD cipherLen = 0;
	if( !::CryptStringToBinaryA(stored.c_str(), (DWORD)stored.size(), CRYPT_STRING_BASE64, NULL, &cipherLen, NULL, NULL) )
		return stored;
	if( cipherLen == 0 || (cipherLen % 16) != 0 )
		return stored;
	std::vector<BYTE> cipher(cipherLen);
	if( !::CryptStringToBinaryA(stored.c_str(), (DWORD)stored.size(), CRYPT_STRING_BASE64, cipher.data(), &cipherLen, NULL, NULL) )
		return stored;

	const char keyText[] = "9@pL2#jFcy@K9$wS";
	BCRYPT_ALG_HANDLE algorithm = NULL;
	BCRYPT_KEY_HANDLE key = NULL;
	std::string plain = stored;
	if( BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_AES_ALGORITHM, NULL, 0) != 0 )
		return stored;
	if( BCryptSetProperty(algorithm, BCRYPT_CHAINING_MODE, (PUCHAR)BCRYPT_CHAIN_MODE_ECB, sizeof(BCRYPT_CHAIN_MODE_ECB), 0) != 0 ) {
		BCryptCloseAlgorithmProvider(algorithm, 0);
		return stored;
	}
	if( BCryptGenerateSymmetricKey(algorithm, &key, NULL, 0, (PUCHAR)keyText, 16, 0) != 0 ) {
		BCryptCloseAlgorithmProvider(algorithm, 0);
		return stored;
	}
	ULONG plainLen = 0;
	if( BCryptDecrypt(key, cipher.data(), cipherLen, NULL, NULL, 0, NULL, 0, &plainLen, 0) == 0 && plainLen > 0 ) {
		std::vector<BYTE> decoded(plainLen);
		ULONG written = 0;
		if( BCryptDecrypt(key, cipher.data(), cipherLen, NULL, NULL, 0, decoded.data(), plainLen, &written, 0) == 0 && written > 0 ) {
			while( written > 0 && decoded[written - 1] == 0 )
				--written;
			plain.assign(reinterpret_cast<const char*>(decoded.data()), written);
			if( plain.empty() )
				plain = stored;
		}
	}
	BCryptDestroyKey(key);
	BCryptCloseAlgorithmProvider(algorithm, 0);
	return plain;
}

namespace {

std::wstring FromUtf8(const char* text)
{
	if( text == NULL || text[0] == 0 )
		return std::wstring();
	const int chars = ::MultiByteToWideChar(CP_UTF8, 0, text, -1, NULL, 0);
	if( chars <= 0 )
		return std::wstring();
	std::wstring out((size_t)chars, L'\0');
	::MultiByteToWideChar(CP_UTF8, 0, text, -1, &out[0], chars);
	while( !out.empty() && out.back() == L'\0' )
		out.pop_back();
	return out;
}

bool RunSql(MYSQL* db, const std::string& sql, std::wstring& error)
{
	if( mysql_query(db, sql.c_str()) == 0 )
		return true;
	error = FromUtf8(mysql_error(db));
	if( error.empty() )
		error = L"执行失败";
	return false;
}

} // namespace

void ClearServerDatabase(HWND notify, const ServerConfig& server)
{
	const int hostId = server.hostId;
	const int port = server.dbPort > 0 ? server.dbPort : 3306;
	const std::string host = ToUtf8(server.dbHost);
	const std::string user = ToUtf8(server.dbUser);
	const std::string password = DecryptDbPassword(ToUtf8(server.dbPassword));
	const std::string dbName = ToUtf8(server.dbName);
	const std::wstring prefix = L"清档 " + std::to_wstring(hostId) + L"：";
	std::thread([notify, hostId, port, host, user, password, dbName, prefix]() {
		PostStatus(notify, prefix + L"正在连接 " + FromUtf8(host.c_str()) + L":" + std::to_wstring(port) + L" / " + FromUtf8(dbName.c_str()));
		MYSQL* db = mysql_init(NULL);
		if( db == NULL ) {
			PostStatus(notify, prefix + L"连接失败，无法创建数据库客户端");
			return;
		}
		unsigned int connectTimeout = 5;
		unsigned int ioTimeout = 600;
		mysql_options(db, MYSQL_OPT_CONNECT_TIMEOUT, &connectTimeout);
		mysql_options(db, MYSQL_OPT_READ_TIMEOUT, &ioTimeout);
		mysql_options(db, MYSQL_OPT_WRITE_TIMEOUT, &ioTimeout);
		mysql_options(db, MYSQL_SET_CHARSET_NAME, "utf8mb4");
		if( mysql_real_connect(db, host.c_str(), user.c_str(), password.c_str(), dbName.c_str(), (unsigned int)port, NULL, 0) == NULL ) {
			PostStatus(notify, prefix + L"连接失败 " + FromUtf8(mysql_error(db)));
			mysql_close(db);
			return;
		}

		const std::string serverId = std::to_string(hostId);
		const std::string steps[][2] = {
			{ "actor_money", "DELETE m FROM actor_money AS m INNER JOIN actors AS a ON m.actorid=a.actorid WHERE a.serverid=" + serverId },
			{ "actorvariable", "DELETE v FROM actorvariable AS v INNER JOIN actors AS a ON v.actorid=a.actorid WHERE a.serverid=" + serverId },
			{ "globaluser", "DELETE g FROM globaluser AS g INNER JOIN actors AS a ON g.account=a.accountname WHERE a.serverid=" + serverId },
			{ "actors", "DELETE FROM actors WHERE serverid=" + serverId },
			{ "module", "DELETE FROM module WHERE serverid=" + serverId },
			{ "servercmd", "DELETE FROM servercmd WHERE serverid=" + serverId },
		};
		if( mysql_autocommit(db, 0) != 0 ) {
			PostStatus(notify, prefix + L"无法开启事务 " + FromUtf8(mysql_error(db)));
			mysql_close(db);
			return;
		}
		for( size_t i = 0; i < sizeof(steps) / sizeof(steps[0]); ++i ) {
			const std::wstring table = FromUtf8(steps[i][0].c_str());
			PostStatus(notify, prefix + L"正在按服务器 " + std::to_wstring(hostId) + L" 删除 " + table);
			std::wstring error;
			if( !RunSql(db, steps[i][1], error) ) {
				mysql_rollback(db);
				PostStatus(notify, prefix + L"删除 " + table + L" 失败，已回滚 " + error);
				mysql_close(db);
				return;
			}
			const unsigned long long affected = (unsigned long long)mysql_affected_rows(db);
			PostStatus(notify, prefix + table + L" 已删除 " + std::to_wstring(affected) + L" 行");
		}
		if( mysql_commit(db) != 0 ) {
			const std::wstring error = FromUtf8(mysql_error(db));
			mysql_rollback(db);
			PostStatus(notify, prefix + L"提交失败，已回滚 " + error);
			mysql_close(db);
			return;
		}
		mysql_close(db);
		PostStatus(notify, prefix + L"完成");
	}).detach();
}

namespace {

struct SqlTable
{
	std::string name;
	std::vector<std::string> cols;
	std::vector<std::string> vals;
};

std::string TrimLeft(const std::string& text)
{
	size_t i = 0;
	while( i < text.size() && (text[i] == ' ' || text[i] == '\t') )
		++i;
	return text.substr(i);
}

bool StartsWithTable(const std::string& line, const char* table)
{
	const std::string text = TrimLeft(line);
	const std::string insert = std::string("INSERT INTO `") + table + "`";
	const std::string replace = std::string("REPLACE INTO `") + table + "`";
	return text.compare(0, insert.size(), insert) == 0 || text.compare(0, replace.size(), replace) == 0;
}

std::vector<std::string> ParseColumns(const std::string& colPart)
{
	std::vector<std::string> cols;
	const size_t paren = colPart.find('(');
	const size_t begin = paren == std::string::npos ? 0 : paren + 1;
	for( size_t i = begin; i < colPart.size(); ++i ) {
		if( colPart[i] != '`' )
			continue;
		const size_t end = colPart.find('`', i + 1);
		if( end == std::string::npos )
			break;
		cols.push_back(colPart.substr(i, end - i + 1));
		i = end;
	}
	return cols;
}

std::vector<std::string> ParseValues(const std::string& valStr)
{
	std::vector<std::string> vals;
	int quotes = 0;
	size_t start = 0;
	bool started = false;
	for( size_t i = 0; i < valStr.size(); ++i ) {
		const char c = valStr[i];
		if( !started ) {
			if( c == '(' && quotes == 0 ) {
				start = i + 1;
				started = true;
			}
			continue;
		}
		if( c == '\'' ) {
			++quotes;
			if( quotes == 2 ) {
				vals.push_back(valStr.substr(start, i - start + 1));
				start = i + 2;
				quotes = 0;
			}
			continue;
		}
		if( c == ',' && quotes == 0 ) {
			if( i > start )
				vals.push_back(valStr.substr(start, i - start));
			start = i + 1;
			continue;
		}
		if( c == ')' && quotes == 0 && i > start ) {
			vals.push_back(valStr.substr(start, i - start));
			break;
		}
	}
	return vals;
}

bool ParseInsertLine(const std::string& line, const char* table, SqlTable& out)
{
	const size_t valuesPos = line.find("VALUES");
	if( valuesPos == std::string::npos )
		return false;
	out.name = table;
	out.cols = ParseColumns(line.substr(0, valuesPos));
	out.vals = ParseValues(line.substr(valuesPos));
	return !out.cols.empty() && out.cols.size() == out.vals.size();
}

std::string StripTicks(const std::string& name)
{
	if( name.size() >= 2 && name.front() == '`' && name.back() == '`' )
		return name.substr(1, name.size() - 2);
	return name;
}

std::string UnquoteSql(const std::string& value)
{
	size_t begin = 0;
	size_t end = value.size();
	while( begin < end && (value[begin] == ' ' || value[begin] == '\t') )
		++begin;
	while( end > begin && (value[end - 1] == ' ' || value[end - 1] == '\t') )
		--end;
	if( end - begin >= 2 && value[begin] == '\'' && value[end - 1] == '\'' )
		return value.substr(begin + 1, end - begin - 2);
	return value.substr(begin, end - begin);
}

bool LoadSqlTables(const std::wstring& path, SqlTable& actors, SqlTable& variables, std::wstring& error)
{
	std::ifstream file(path, std::ios::binary);
	if( !file ) {
		error = L"无法读取 SQL 文件";
		return false;
	}
	int actorCount = 0;
	int variableCount = 0;
	std::string line;
	bool first = true;
	while( std::getline(file, line) ) {
		if( !line.empty() && line.back() == '\r' )
			line.pop_back();
		if( first && line.size() >= 3 && (unsigned char)line[0] == 0xEF && (unsigned char)line[1] == 0xBB && (unsigned char)line[2] == 0xBF )
			line.erase(0, 3);
		first = false;
		if( StartsWithTable(line, "actors") ) {
			++actorCount;
			if( actorCount == 1 && !ParseInsertLine(line, "actors", actors) ) {
				error = L"actors 语句无法解析";
				return false;
			}
		}
		else if( StartsWithTable(line, "actorvariable") ) {
			++variableCount;
			if( variableCount == 1 && !ParseInsertLine(line, "actorvariable", variables) ) {
				error = L"actorvariable 语句无法解析";
				return false;
			}
		}
	}
	if( actorCount != 1 || variableCount != 1 ) {
		error = L"SQL 文件需要且只能包含一条 actors 和一条 actorvariable";
		return false;
	}
	if( actors.vals.size() < 3 || variables.vals.empty() ) {
		error = L"SQL 列数不足";
		return false;
	}
	return true;
}

bool QueryCell(MYSQL* db, const std::string& sql, std::string& value, bool& hasRow, std::wstring& error)
{
	value.clear();
	hasRow = false;
	if( mysql_query(db, sql.c_str()) != 0 ) {
		error = FromUtf8(mysql_error(db));
		return false;
	}
	MYSQL_RES* result = mysql_store_result(db);
	if( result == NULL ) {
		error = FromUtf8(mysql_error(db));
		if( error.empty() )
			error = L"查询没有结果";
		return false;
	}
	MYSQL_ROW row = mysql_fetch_row(result);
	if( row != NULL ) {
		hasRow = true;
		if( row[0] != NULL )
			value = row[0];
	}
	mysql_free_result(result);
	return true;
}

bool DescribeColumns(MYSQL* db, const std::string& table, std::vector<std::string>& columns, std::wstring& error)
{
	columns.clear();
	const std::string sql = "DESCRIBE `" + table + "`";
	if( mysql_query(db, sql.c_str()) != 0 ) {
		error = FromUtf8(mysql_error(db));
		return false;
	}
	MYSQL_RES* result = mysql_store_result(db);
	if( result == NULL ) {
		error = FromUtf8(mysql_error(db));
		return false;
	}
	const unsigned int fieldCount = mysql_num_fields(result);
	MYSQL_ROW row;
	while( (row = mysql_fetch_row(result)) != NULL ) {
		if( fieldCount > 0 && row[0] != NULL )
			columns.push_back(row[0]);
	}
	mysql_free_result(result);
	return !columns.empty();
}

bool ColumnExists(const std::vector<std::string>& columns, const std::string& name)
{
	for( size_t i = 0; i < columns.size(); ++i ) {
		if( _stricmp(columns[i].c_str(), name.c_str()) == 0 )
			return true;
	}
	return false;
}

void KeepExistingColumns(MYSQL* db, SqlTable& table, std::wstring& error)
{
	std::vector<std::string> live;
	if( !DescribeColumns(db, table.name, live, error) )
		return;
	std::vector<std::string> cols;
	std::vector<std::string> vals;
	for( size_t i = 0; i < table.cols.size() && i < table.vals.size(); ++i ) {
		if( !ColumnExists(live, StripTicks(table.cols[i])) )
			continue;
		cols.push_back(table.cols[i]);
		vals.push_back(table.vals[i]);
	}
	table.cols.swap(cols);
	table.vals.swap(vals);
	error.clear();
}

std::string JoinComma(const std::vector<std::string>& items)
{
	std::string out;
	for( size_t i = 0; i < items.size(); ++i ) {
		if( i > 0 )
			out += ",";
		out += items[i];
	}
	return out;
}

bool FindFreeAccount(MYSQL* db, std::string& account, std::wstring& error)
{
	std::string countText;
	bool hasRow = false;
	if( !QueryCell(db, "SELECT COUNT(1) FROM `globaluser` WHERE `account` LIKE 'online%'", countText, hasRow, error) )
		return false;
	long long count = countText.empty() ? 1 : _atoi64(countText.c_str());
	if( count < 1 )
		count = 1;
	for( long long n = count; n < count + 1001; ++n ) {
		const std::string candidate = "online" + std::to_string(n);
		std::string found;
		if( !QueryCell(db, "SELECT `account` FROM `globaluser` WHERE `account`='" + candidate + "'", found, hasRow, error) )
			return false;
		if( !hasRow ) {
			account = candidate;
			return true;
		}
	}
	error = L"没有可用的 online 账号";
	return false;
}

std::string ColumnValue(const SqlTable& table, const char* column)
{
	for( size_t i = 0; i < table.cols.size() && i < table.vals.size(); ++i ) {
		if( _stricmp(StripTicks(table.cols[i]).c_str(), column) == 0 )
			return UnquoteSql(table.vals[i]);
	}
	return std::string();
}

bool SetColumnValue(SqlTable& table, const char* column, const std::string& value)
{
	for( size_t i = 0; i < table.cols.size() && i < table.vals.size(); ++i ) {
		if( _stricmp(StripTicks(table.cols[i]).c_str(), column) != 0 )
			continue;
		table.vals[i] = value;
		return true;
	}
	return false;
}

long long ReadActorId(const SqlTable& table)
{
	std::string text = ColumnValue(table, "actorid");
	if( text.empty() && !table.vals.empty() )
		text = UnquoteSql(table.vals[0]);
	return _atoi64(text.c_str());
}

bool ActorIdUsed(MYSQL* db, long long actorId, bool& used, std::wstring& error)
{
	used = false;
	const std::string idText = std::to_string(actorId);
	const char* tables[] = { "actors", "actorvariable" };
	for( size_t i = 0; i < sizeof(tables) / sizeof(tables[0]); ++i ) {
		std::string found;
		bool hasRow = false;
		const std::string sql = std::string("SELECT `actorid` FROM `") + tables[i] + "` WHERE `actorid`=" + idText + " LIMIT 1";
		if( !QueryCell(db, sql, found, hasRow, error) )
			return false;
		if( hasRow ) {
			used = true;
			return true;
		}
	}
	return true;
}

bool FindFreeActorId(MYSQL* db, long long originalId, long long& actorId, std::wstring& error)
{
	const long long limit = originalId + 100000;
	for( long long id = originalId; id < limit; ++id ) {
		bool used = false;
		if( !ActorIdUsed(db, id, used, error) )
			return false;
		if( !used ) {
			actorId = id;
			return true;
		}
	}
	error = L"没有可用的角色 id";
	return false;
}

} // namespace

void ImportActorSql(HWND notify, const ServerConfig& server, const std::wstring& sqlPath)
{
	const int hostId = server.hostId;
	const int port = server.dbPort > 0 ? server.dbPort : 3306;
	const std::string host = ToUtf8(server.dbHost);
	const std::string user = ToUtf8(server.dbUser);
	const std::string password = DecryptDbPassword(ToUtf8(server.dbPassword));
	const std::string dbName = ToUtf8(server.dbName);
	const std::wstring prefix = L"导号 " + std::to_wstring(hostId) + L"：";
	std::thread([notify, hostId, port, host, user, password, dbName, sqlPath, prefix]() {
		auto fail = [&](const std::wstring& text) {
			PostStatus(notify, prefix + text, 1);
		};
		PostStatus(notify, prefix + L"正在检查 " + sqlPath);
		SqlTable actors;
		SqlTable variables;
		std::wstring error;
		if( !LoadSqlTables(sqlPath, actors, variables, error) ) {
			fail(error);
			return;
		}
		const std::string actorName = ColumnValue(actors, "actorname");
		const long long originalActorId = ReadActorId(actors);
		if( originalActorId <= 0 ) {
			fail(L"SQL 中的角色 id 无效");
			return;
		}

		PostStatus(notify, prefix + L"正在连接 " + FromUtf8(host.c_str()) + L":" + std::to_wstring(port) + L" / " + FromUtf8(dbName.c_str()));
		MYSQL* db = mysql_init(NULL);
		if( db == NULL ) {
			fail(L"连接失败，无法创建数据库客户端");
			return;
		}
		unsigned int connectTimeout = 5;
		unsigned int ioTimeout = 600;
		mysql_options(db, MYSQL_OPT_CONNECT_TIMEOUT, &connectTimeout);
		mysql_options(db, MYSQL_OPT_READ_TIMEOUT, &ioTimeout);
		mysql_options(db, MYSQL_OPT_WRITE_TIMEOUT, &ioTimeout);
		mysql_options(db, MYSQL_SET_CHARSET_NAME, "utf8mb4");
		if( mysql_real_connect(db, host.c_str(), user.c_str(), password.c_str(), dbName.c_str(), (unsigned int)port, NULL, 0) == NULL ) {
			fail(L"连接失败 " + FromUtf8(mysql_error(db)));
			mysql_close(db);
			return;
		}
		PostStatus(notify, prefix + L"正在分配角色 id，原角色 id " + std::to_wstring(originalActorId));
		long long actorId = 0;
		if( !FindFreeActorId(db, originalActorId, actorId, error) ) {
			fail(L"分配角色 id 失败 " + error);
			mysql_close(db);
			return;
		}
		const std::string actorIdText = std::to_string(actorId);
		if( !SetColumnValue(actors, "actorid", actorIdText) )
			actors.vals[0] = actorIdText;
		if( !SetColumnValue(variables, "actorid", actorIdText) )
			variables.vals[0] = actorIdText;
		if( actorId == originalActorId )
			PostStatus(notify, prefix + L"角色 id " + std::to_wstring(actorId) + L" 未占用");
		else
			PostStatus(notify, prefix + L"角色 id " + std::to_wstring(originalActorId) + L" 已存在，改用 " + std::to_wstring(actorId));

		PostStatus(notify, prefix + L"正在分配登录账号");
		std::string account;
		if( !FindFreeAccount(db, account, error) ) {
			fail(L"分配账号失败 " + error);
			mysql_close(db);
			return;
		}
		std::string escaped(account.size() * 2 + 1, '\0');
		const unsigned long escapedLen = mysql_real_escape_string(db, &escaped[0], account.c_str(), (unsigned long)account.size());
		escaped.resize(escapedLen);
		actors.vals[2] = "'" + escaped + "'";

		KeepExistingColumns(db, actors, error);
		if( !error.empty() || actors.cols.empty() ) {
			fail(L"读取 actors 字段失败 " + error);
			mysql_close(db);
			return;
		}
		KeepExistingColumns(db, variables, error);
		if( !error.empty() || variables.cols.empty() ) {
			fail(L"读取 actorvariable 字段失败 " + error);
			mysql_close(db);
			return;
		}
		if( mysql_autocommit(db, 0) != 0 ) {
			fail(L"无法开启事务 " + FromUtf8(mysql_error(db)));
			mysql_close(db);
			return;
		}
		const std::string actorSql = "INSERT INTO `actors` (" + JoinComma(actors.cols) + ") VALUES (" + JoinComma(actors.vals) + ")";
		PostStatus(notify, prefix + L"正在写入 actors，登录账号 " + FromUtf8(account.c_str()));
		if( !RunSql(db, actorSql, error) ) {
			mysql_rollback(db);
			fail(L"写入 actors 失败，已回滚 " + error);
			mysql_close(db);
			return;
		}
		const std::string variableSql = "INSERT INTO `actorvariable` (" + JoinComma(variables.cols) + ") VALUES (" + JoinComma(variables.vals) + ")";
		PostStatus(notify, prefix + L"正在写入 actorvariable");
		if( !RunSql(db, variableSql, error) ) {
			mysql_rollback(db);
			fail(L"写入 actorvariable 失败，已回滚 " + error);
			mysql_close(db);
			return;
		}
		const std::string userSql = "INSERT INTO `globaluser` (`account`,`gmlevel`) VALUES ('" + escaped + "',10)";
		PostStatus(notify, prefix + L"正在写入 globaluser");
		if( !RunSql(db, userSql, error) ) {
			mysql_rollback(db);
			fail(L"写入 globaluser 失败，已回滚 " + error);
			mysql_close(db);
			return;
		}
		const std::string updateSql = "UPDATE `actors` SET `serverid`=" + std::to_string(hostId) + ",`bornserverid`=" + std::to_string(hostId) + " WHERE `actorid`=" + std::to_string(actorId);
		PostStatus(notify, prefix + L"正在设置服务器 id");
		if( !RunSql(db, updateSql, error) ) {
			mysql_rollback(db);
			fail(L"设置服务器 id 失败，已回滚 " + error);
			mysql_close(db);
			return;
		}
		if( mysql_commit(db) != 0 ) {
			error = FromUtf8(mysql_error(db));
			mysql_rollback(db);
			fail(L"提交失败，已回滚 " + error);
			mysql_close(db);
			return;
		}
		mysql_close(db);
		std::wstring done = prefix + L"完成 登录账号 " + FromUtf8(account.c_str()) + L" 角色 id " + std::to_wstring(actorId);
		if( !actorName.empty() )
			done += L" 角色名 " + FromUtf8(actorName.c_str());
		PostStatus(notify, done, 1);
	}).detach();
}

namespace {

std::string QuoteIdent(const std::string& name)
{
	std::string out = "`";
	for( size_t i = 0; i < name.size(); ++i ) {
		if( name[i] == '`' )
			out += "``";
		else
			out.push_back(name[i]);
	}
	out.push_back('`');
	return out;
}

struct DumpInfo
{
	std::vector<std::string> tables;
	std::vector<std::string> procedures;
	std::vector<std::string> createTables;
	std::vector<std::string> createProcedures;
};

std::string FirstBacktickName(const std::string& line)
{
	const size_t begin = line.find('`');
	if( begin == std::string::npos )
		return std::string();
	const size_t end = line.find('`', begin + 1);
	if( end == std::string::npos )
		return std::string();
	return line.substr(begin, end - begin + 1);
}

bool LoadDumpInfo(const std::wstring& path, DumpInfo& info, std::wstring& error)
{
	std::ifstream file(path, std::ios::binary);
	if( !file ) {
		error = L"无法读取备份";
		return false;
	}
	bool inTable = false;
	bool inProcedure = false;
	std::string tableSql;
	std::string procedureSql;
	std::string line;
	bool first = true;
	while( std::getline(file, line) ) {
		if( !line.empty() && line.back() == '\r' )
			line.pop_back();
		if( first && line.size() >= 3 && (unsigned char)line[0] == 0xEF && (unsigned char)line[1] == 0xBB && (unsigned char)line[2] == 0xBF )
			line.erase(0, 3);
		first = false;
		const std::string text = TrimLeft(line);
		if( !inTable && !inProcedure && text.compare(0, 14, "CREATE TABLE `") == 0 ) {
			inTable = true;
			const std::string name = FirstBacktickName(text);
			if( !name.empty() )
				info.tables.push_back(name);
		}
		else if( !inTable && !inProcedure && (text.compare(0, 15, "CREATE DEFINER=") == 0 || text.compare(0, 16, "CREATE PROCEDURE") == 0) ) {
			inProcedure = true;
		}
		if( inTable ) {
			if( !tableSql.empty() )
				tableSql.push_back('\n');
			tableSql += line;
			if( !text.empty() && text.back() == ';' ) {
				info.createTables.push_back(tableSql);
				tableSql.clear();
				inTable = false;
			}
		}
		else if( inProcedure ) {
			if( !procedureSql.empty() )
				procedureSql.push_back('\n');
			procedureSql += line;
			if( text.size() >= 2 && text.compare(text.size() - 2, 2, ";;") == 0 ) {
				if( !procedureSql.empty() && procedureSql.back() == ';' )
					procedureSql.pop_back();
				const size_t mark = procedureSql.find("PROCEDURE `");
				if( mark != std::string::npos ) {
					const std::string name = FirstBacktickName(procedureSql.substr(mark + 10));
					if( !name.empty() )
						info.procedures.push_back(name);
				}
				info.createProcedures.push_back(procedureSql);
				procedureSql.clear();
				inProcedure = false;
			}
		}
	}
	if( info.createTables.empty() ) {
		error = L"备份里没有建表语句";
		return false;
	}
	return true;
}

std::string EscapeSqlText(MYSQL* db, const std::string& text)
{
	std::string out(text.size() * 2 + 1, '\0');
	const unsigned long length = mysql_real_escape_string(db, &out[0], text.c_str(), (unsigned long)text.size());
	out.resize(length);
	return out;
}

} // namespace

void ImportServerDump(HWND notify, const ServerConfig& server, const std::wstring& sqlPath)
{
	const int hostId = server.hostId;
	const int port = server.dbPort > 0 ? server.dbPort : 3306;
	const std::string host = ToUtf8(server.dbHost);
	const std::string user = ToUtf8(server.dbUser);
	const std::string password = DecryptDbPassword(ToUtf8(server.dbPassword));
	const std::string dbName = ToUtf8(server.dbName);
	const std::wstring prefix = L"导服 " + std::to_wstring(hostId) + L"：";
	std::thread([notify, hostId, port, host, user, password, dbName, sqlPath, prefix]() {
		auto fail = [&](const std::wstring& text) {
			PostStatus(notify, prefix + text, 2);
		};
		std::wstring error;
		PostStatus(notify, prefix + L"正在分析备份 " + sqlPath);
		DumpInfo info;
		if( !LoadDumpInfo(sqlPath, info, error) ) {
			fail(error);
			return;
		}
		PostStatus(notify, prefix + L"备份包含 " + std::to_wstring(info.createTables.size()) + L" 张表，" + std::to_wstring(info.createProcedures.size()) + L" 个存储过程");

		PostStatus(notify, prefix + L"正在连接 " + FromUtf8(host.c_str()) + L":" + std::to_wstring(port) + L" / " + FromUtf8(dbName.c_str()));
		MYSQL* db = mysql_init(NULL);
		if( db == NULL ) {
			fail(L"连接失败，无法创建数据库客户端");
			return;
		}
		unsigned int connectTimeout = 5;
		unsigned int ioTimeout = 600;
		mysql_options(db, MYSQL_OPT_CONNECT_TIMEOUT, &connectTimeout);
		mysql_options(db, MYSQL_OPT_READ_TIMEOUT, &ioTimeout);
		mysql_options(db, MYSQL_OPT_WRITE_TIMEOUT, &ioTimeout);
		mysql_options(db, MYSQL_SET_CHARSET_NAME, "utf8mb4");
		if( mysql_real_connect(db, host.c_str(), user.c_str(), password.c_str(), NULL, (unsigned int)port, NULL, 0) == NULL ) {
			fail(L"连接失败 " + FromUtf8(mysql_error(db)));
			mysql_close(db);
			return;
		}
		const std::string quotedDb = QuoteIdent(dbName);
		PostStatus(notify, prefix + L"正在创建数据库");
		if( !RunSql(db, "CREATE DATABASE IF NOT EXISTS " + quotedDb + " DEFAULT CHARACTER SET utf8", error) ) {
			fail(L"创建数据库失败 " + error);
			mysql_close(db);
			return;
		}
		if( mysql_select_db(db, dbName.c_str()) != 0 ) {
			fail(L"选择数据库失败 " + FromUtf8(mysql_error(db)));
			mysql_close(db);
			return;
		}
		if( !RunSql(db, "RESET MASTER", error) )
			PostStatus(notify, prefix + L"RESET MASTER 未执行 " + error);
		RunSql(db, "ALTER DATABASE " + quotedDb + " CHARACTER SET utf8", error);
		RunSql(db, "ALTER DATABASE " + quotedDb + " COLLATE utf8_general_ci", error);

		for( size_t i = 0; i < info.tables.size(); ++i ) {
			PostStatus(notify, prefix + L"正在删除表 " + std::to_wstring(i + 1) + L"/" + std::to_wstring(info.tables.size()) + L" " + FromUtf8(info.tables[i].c_str()));
			if( !RunSql(db, "DROP TABLE IF EXISTS " + info.tables[i], error) ) {
				fail(L"删除表失败 " + FromUtf8(info.tables[i].c_str()) + L" " + error);
				mysql_close(db);
				return;
			}
		}
		for( size_t i = 0; i < info.procedures.size(); ++i ) {
			PostStatus(notify, prefix + L"正在删除存储过程 " + FromUtf8(info.procedures[i].c_str()));
			RunSql(db, "DROP PROCEDURE IF EXISTS " + info.procedures[i], error);
		}
		for( size_t i = 0; i < info.createTables.size(); ++i ) {
			PostStatus(notify, prefix + L"正在建表 " + std::to_wstring(i + 1) + L"/" + std::to_wstring(info.createTables.size()));
			if( !RunSql(db, info.createTables[i], error) ) {
				fail(L"建表失败 " + std::to_wstring(i + 1) + L" " + error);
				mysql_close(db);
				return;
			}
		}
		for( size_t i = 0; i < info.createProcedures.size(); ++i ) {
			PostStatus(notify, prefix + L"正在创建存储过程 " + std::to_wstring(i + 1) + L"/" + std::to_wstring(info.createProcedures.size()));
			if( !RunSql(db, info.createProcedures[i], error) )
				PostStatus(notify, prefix + L"存储过程 " + std::to_wstring(i + 1) + L" 未创建 " + error);
		}

		PostStatus(notify, prefix + L"正在导入数据");
		std::ifstream file(sqlPath, std::ios::binary);
		if( !file ) {
			fail(L"无法再次读取备份");
			mysql_close(db);
			return;
		}
		std::string line;
		unsigned long long inserts = 0;
		bool first = true;
		while( std::getline(file, line) ) {
			if( !line.empty() && line.back() == '\r' )
				line.pop_back();
			if( first && line.size() >= 3 && (unsigned char)line[0] == 0xEF && (unsigned char)line[1] == 0xBB && (unsigned char)line[2] == 0xBF )
				line.erase(0, 3);
			first = false;
			const std::string text = TrimLeft(line);
			if( text.compare(0, 11, "INSERT INTO") != 0 )
				continue;
			if( !RunSql(db, line, error) ) {
				fail(L"导入数据失败，已写入 " + std::to_wstring(inserts) + L" 条 " + error);
				mysql_close(db);
				return;
			}
			++inserts;
			if( inserts % 200 == 0 )
				PostStatus(notify, prefix + L"正在导入数据，已写入 " + std::to_wstring(inserts) + L" 条");
		}
		PostStatus(notify, prefix + L"数据写入 " + std::to_wstring(inserts) + L" 条");

		const std::string serverId = std::to_string(hostId);
		PostStatus(notify, prefix + L"正在设置服务器 id");
		if( !RunSql(db, "UPDATE `actors` SET `serverid`=" + serverId + ",`bornserverid`=" + serverId, error) ) {
			fail(L"设置角色服务器 id 失败 " + error);
			mysql_close(db);
			return;
		}
		if( !RunSql(db, "UPDATE `module` SET `serverid`=" + serverId, error) )
			PostStatus(notify, prefix + L"设置 module 服务器 id 未执行 " + error);
		if( !RunSql(db, "UPDATE `globaluser` SET `gmlevel`=10", error) )
			PostStatus(notify, prefix + L"设置 GM 等级未执行 " + error);
		if( !RunSql(db, "UPDATE `globaluser` SET `passwd`=''", error) )
			PostStatus(notify, prefix + L"清空登录密码未执行 " + error);
		if( !RunSql(db, "UPDATE `globaluser` SET `tgpasswd`=''", error) )
			PostStatus(notify, prefix + L"清空 tgpasswd 未执行 " + error);

		PostStatus(notify, prefix + L"正在按战力重命名登录账号");
		if( mysql_query(db, "SELECT `accountname` FROM `actors` ORDER BY `fightvalue` DESC") != 0 ) {
			PostStatus(notify, prefix + L"读取角色战力失败，账号保持原名 " + FromUtf8(mysql_error(db)));
		}
		else {
			MYSQL_RES* result = mysql_store_result(db);
			if( result == NULL ) {
				PostStatus(notify, prefix + L"读取角色列表失败，账号保持原名");
			}
			else {
				int index = 1;
				MYSQL_ROW row;
				while( (row = mysql_fetch_row(result)) != NULL ) {
					if( row[0] == NULL || row[0][0] == 0 ) {
						++index;
						continue;
					}
					const std::string oldName = row[0];
					const std::string newName = "mytest" + std::to_string(index);
					const std::string oldSql = EscapeSqlText(db, oldName);
					const std::string renameUser = "UPDATE `globaluser` SET `account`='" + newName + "' WHERE `account`='" + oldSql + "'";
					const std::string renameActor = "UPDATE `actors` SET `accountname`='" + newName + "' WHERE `accountname`='" + oldSql + "'";
					if( !RunSql(db, renameUser, error) || !RunSql(db, renameActor, error) ) {
						mysql_free_result(result);
						fail(L"重命名账号失败 " + FromUtf8(oldName.c_str()) + L" " + error);
						mysql_close(db);
						return;
					}
					++index;
				}
				mysql_free_result(result);
				PostStatus(notify, prefix + L"登录账号已改为 mytest1 起，共 " + std::to_wstring(index - 1) + L" 个");
			}
		}
		mysql_close(db);
		PostStatus(notify, prefix + L"完成", 2);
	}).detach();
}
