#include "StdAfx.h"
#include "ServerConfig.h"

#include "lua.hpp"

#include <shlwapi.h>

#include <algorithm>

namespace {

	extern "C" int LuaNoop(lua_State*)
	{
		return 0;
	}

	struct LuaState
	{
		lua_State* L;
		LuaState() : L(luaL_newstate()) {}
		~LuaState() { if( L != NULL ) lua_close(L); }
	};

	std::wstring ToWide(const std::string& text)
	{
		if( text.empty() )
			return std::wstring();
		int n = ::MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(), (int)text.size(), NULL, 0);
		UINT cp = CP_UTF8;
		if( n <= 0 ) {
			cp = CP_ACP;
			n = ::MultiByteToWideChar(CP_ACP, 0, text.data(), (int)text.size(), NULL, 0);
		}
		if( n <= 0 )
			return std::wstring();
		std::wstring wide(n, L'\0');
		::MultiByteToWideChar(cp, 0, text.data(), (int)text.size(), &wide[0], n);
		return wide;
	}

	bool ReadTextFile(const std::wstring& path, std::string& text)
	{
		HANDLE file = ::CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
		if( file == INVALID_HANDLE_VALUE )
			return false;
		LARGE_INTEGER size;
		if( !::GetFileSizeEx(file, &size) || size.QuadPart <= 0 || size.QuadPart > 2 * 1024 * 1024 ) {
			::CloseHandle(file);
			return false;
		}
		std::string buf((size_t)size.QuadPart, '\0');
		DWORD read = 0;
		const BOOL ok = ::ReadFile(file, &buf[0], (DWORD)buf.size(), &read, NULL);
		::CloseHandle(file);
		if( !ok || read == 0 )
			return false;
		buf.resize(read);
		if( buf.size() >= 3 && (unsigned char)buf[0] == 0xEF && (unsigned char)buf[1] == 0xBB && (unsigned char)buf[2] == 0xBF )
			buf.erase(0, 3);
		if( buf.find('\0') != std::string::npos )
			return false;
		text.swap(buf);
		return true;
	}

	void OpenSandbox(lua_State* L)
	{
		static const luaL_Reg kLibs[] = {
			{ LUA_GNAME, luaopen_base },
			{ LUA_STRLIBNAME, luaopen_string },
			{ LUA_MATHLIBNAME, luaopen_math },
			{ LUA_TABLIBNAME, luaopen_table },
			{ LUA_UTF8LIBNAME, luaopen_utf8 },
			{ NULL, NULL }
		};
		for( int i = 0; kLibs[i].name != NULL; ++i ) {
			luaL_requiref(L, kLibs[i].name, kLibs[i].func, 1);
			lua_pop(L, 1);
		}
		const char* blocked[] = { "dofile", "loadfile", "load", "require", NULL };
		for( int i = 0; blocked[i] != NULL; ++i ) {
			lua_pushnil(L);
			lua_setglobal(L, blocked[i]);
		}
		lua_pushcfunction(L, LuaNoop);
		lua_setglobal(L, "print");
	}

	bool ReadGlobalInt(lua_State* L, const char* key, int& value)
	{
		lua_getglobal(L, key);
		int isnum = 0;
		const lua_Integer n = lua_tointegerx(L, -1, &isnum);
		lua_pop(L, 1);
		if( !isnum )
			return false;
		value = (int)n;
		return true;
	}

	bool ReadIntField(lua_State* L, int idx, const char* key, int& value)
	{
		lua_getfield(L, idx, key);
		int isnum = 0;
		const lua_Integer n = lua_tointegerx(L, -1, &isnum);
		lua_pop(L, 1);
		if( !isnum )
			return false;
		value = (int)n;
		return true;
	}

	std::wstring ReadStringField(lua_State* L, int idx, const char* key)
	{
		lua_getfield(L, idx, key);
		std::wstring value;
		if( lua_type(L, -1) == LUA_TSTRING ) {
			size_t len = 0;
			const char* text = lua_tolstring(L, -1, &len);
			if( text != NULL )
				value = ToWide(std::string(text, len));
		}
		lua_pop(L, 1);
		return value;
	}

	void ReadDatabase(lua_State* L, int idx, ServerConfig& server)
	{
		idx = lua_absindex(L, idx);
		lua_getfield(L, idx, "database");
		const int db = lua_istable(L, -1) ? lua_gettop(L) : idx;
		if( db == idx )
			lua_pop(L, 1);
		server.dbHost = ReadStringField(L, db, "dbhost");
		ReadIntField(L, db, "dbport", server.dbPort);
		server.dbName = ReadStringField(L, db, "dbname");
		server.dbUser = ReadStringField(L, db, "dbuser");
		server.dbPassword = ReadStringField(L, db, "dbpasswd");
		if( db != idx )
			lua_pop(L, 1);
	}

	bool LoadAsTable(lua_State* L, const std::string& body)
	{
		const std::string chunk = "return " + body;
		if( luaL_loadbuffer(L, chunk.data(), chunk.size(), "=value") != LUA_OK ) {
			lua_pop(L, 1);
			return false;
		}
		if( lua_pcall(L, 0, 1, 0) != LUA_OK ) {
			lua_pop(L, 1);
			return false;
		}
		if( !lua_istable(L, -1) ) {
			lua_pop(L, 1);
			return false;
		}
		return true;
	}

	void AppendServersFromTable(lua_State* L, int idx, const ServerConfig& base, std::vector<ServerConfig>& servers)
	{
		idx = lua_absindex(L, idx);
		int hostId = 0;
		if( ReadIntField(L, idx, "host_id", hostId) ) {
			ServerConfig server = base;
			server.hostId = hostId;
			server.dbPort = -1;
			ReadIntField(L, idx, "login_port", server.loginPort);
			ReadIntField(L, idx, "mcs_port", server.mcsPort);
			ReadDatabase(L, idx, server);
			servers.push_back(server);
			return;
		}
		lua_pushnil(L);
		while( lua_next(L, idx) != 0 ) {
			if( lua_istable(L, -1) )
				AppendServersFromTable(L, -1, base, servers);
			lua_pop(L, 1);
		}
	}

	void ParseEmbeddedServers(lua_State* L, const ServerConfig& base, std::vector<ServerConfig>& servers)
	{
		lua_pushglobaltable(L);
		const int globals = lua_gettop(L);
		lua_pushnil(L);
		while( lua_next(L, globals) != 0 ) {
			if( lua_type(L, -2) == LUA_TSTRING && lua_type(L, -1) == LUA_TSTRING ) {
				size_t len = 0;
				const char* text = lua_tolstring(L, -1, &len);
				const std::string body = text != NULL ? std::string(text, len) : std::string();
				lua_pop(L, 1);
				if( LoadAsTable(L, body) ) {
					AppendServersFromTable(L, -1, base, servers);
					lua_pop(L, 1);
				}
			}
			else {
				lua_pop(L, 1);
			}
		}
		lua_pop(L, 1);
	}

	void ParseServerFile(const std::wstring& path, std::vector<ServerConfig>& servers)
	{
		std::string raw;
		if( !ReadTextFile(path, raw) )
			return;

		LuaState state;
		if( state.L == NULL )
			return;
		OpenSandbox(state.L);
		if( luaL_loadbuffer(state.L, raw.data(), raw.size(), "=config") != LUA_OK ) {
			lua_pop(state.L, 1);
			return;
		}
		if( lua_pcall(state.L, 0, 0, 0) != LUA_OK ) {
			lua_pop(state.L, 1);
			return;
		}

		ServerConfig base;
		base.loginPort = -1;
		base.mcsPort = -1;
		base.dbPort = -1;
		const wchar_t* name = ::PathFindFileNameW(path.c_str());
		base.fileName = name != NULL ? name : path;
		base.filePath = path;

		int hostId = 0;
		if( ReadGlobalInt(state.L, "host_id", hostId) ) {
			ServerConfig server = base;
			server.hostId = hostId;
			ReadGlobalInt(state.L, "login_port", server.loginPort);
			ReadGlobalInt(state.L, "mcs_port", server.mcsPort);
			lua_getglobal(state.L, "database_info");
			if( lua_type(state.L, -1) == LUA_TSTRING ) {
				size_t len = 0;
				const char* text = lua_tolstring(state.L, -1, &len);
				const std::string body = text != NULL ? std::string(text, len) : std::string();
				lua_pop(state.L, 1);
				if( LoadAsTable(state.L, body) ) {
					ReadDatabase(state.L, -1, server);
					lua_pop(state.L, 1);
				}
			}
			else {
				lua_pop(state.L, 1);
			}
			servers.push_back(server);
			return;
		}

		ParseEmbeddedServers(state.L, base, servers);
	}

	bool ServerLess(const ServerConfig& a, const ServerConfig& b)
	{
		if( a.hostId != b.hostId )
			return a.hostId < b.hostId;
		return _wcsicmp(a.fileName.c_str(), b.fileName.c_str()) < 0;
	}

} // namespace

bool LoadServerFolder(const std::wstring& folder, ServerFolder& out)
{
	wchar_t full[MAX_PATH];
	DWORD n = ::GetFullPathNameW(folder.c_str(), MAX_PATH, full, NULL);
	if( n == 0 || n >= MAX_PATH )
		return false;
	std::wstring path = full;
	while( path.size() > 3 && (path.back() == L'\\' || path.back() == L'/') )
		path.pop_back();

	const DWORD attr = ::GetFileAttributesW(path.c_str());
	if( attr == INVALID_FILE_ATTRIBUTES || (attr & FILE_ATTRIBUTE_DIRECTORY) == 0 )
		return false;

	out.path = path;
	out.servers.clear();

	const std::wstring pattern = path + L"\\*";
	WIN32_FIND_DATAW data;
	HANDLE find = ::FindFirstFileW(pattern.c_str(), &data);
	if( find == INVALID_HANDLE_VALUE )
		return true;
	do {
		if( (data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0 )
			continue;
		ParseServerFile(path + L"\\" + data.cFileName, out.servers);
	} while( ::FindNextFileW(find, &data) );
	::FindClose(find);
	std::sort(out.servers.begin(), out.servers.end(), ServerLess);
	return true;
}

namespace {

std::wstring NormalizeDir(const std::wstring& folder)
{
	if( folder.empty() )
		return std::wstring();
	wchar_t full[MAX_PATH];
	const DWORD n = ::GetFullPathNameW(folder.c_str(), MAX_PATH, full, NULL);
	std::wstring path = (n == 0 || n >= MAX_PATH) ? folder : std::wstring(full);
	while( path.size() > 3 && (path.back() == L'\\' || path.back() == L'/') )
		path.pop_back();
	return path;
}

} // namespace

bool FindServerPick(const std::vector<ServerFolder>& folders, const ServerPick& pick, size_t& group, size_t& index)
{
	group = 0;
	index = 0;
	if( !pick.valid || pick.folder.empty() || pick.fileName.empty() )
		return false;
	const std::wstring folder = NormalizeDir(pick.folder);
	for( size_t g = 0; g < folders.size(); ++g ) {
		if( _wcsicmp(NormalizeDir(folders[g].path).c_str(), folder.c_str()) != 0 )
			continue;
		for( size_t i = 0; i < folders[g].servers.size(); ++i ) {
			const ServerConfig& server = folders[g].servers[i];
			if( server.hostId != pick.hostId )
				continue;
			if( _wcsicmp(server.fileName.c_str(), pick.fileName.c_str()) != 0 )
				continue;
			group = g;
			index = i;
			return true;
		}
	}
	return false;
}

namespace {

	std::string ToUtf8(const std::wstring& text)
	{
		if( text.empty() )
			return std::string();
		int n = ::WideCharToMultiByte(CP_UTF8, 0, text.data(), (int)text.size(), NULL, 0, NULL, NULL);
		if( n <= 0 )
			return std::string();
		std::string utf8(n, '\0');
		::WideCharToMultiByte(CP_UTF8, 0, text.data(), (int)text.size(), &utf8[0], n, NULL, NULL);
		return utf8;
	}

	std::string EscapeLuaString(const std::string& text)
	{
		std::string out;
		out.reserve(text.size());
		for( size_t i = 0; i < text.size(); ++i ) {
			const unsigned char c = (unsigned char)text[i];
			if( c == '\\' || c == '"' ) {
				out.push_back('\\');
				out.push_back((char)c);
			}
			else if( c == '\r' || c == '\n' || c == '\t' ) {
				out.push_back(' ');
			}
			else {
				out.push_back((char)c);
			}
		}
		return out;
	}

	std::wstring LuaStringField(lua_State* state, int table, const char* key)
	{
		lua_getfield(state, table, key);
		std::wstring value;
		if( lua_type(state, -1) == LUA_TSTRING ) {
			size_t len = 0;
			const char* text = lua_tolstring(state, -1, &len);
			if( text != NULL && len > 0 )
				value = ToWide(std::string(text, len));
		}
		lua_pop(state, 1);
		return value;
	}

	void LoadServerPick(lua_State* state, const char* name, ServerPick& pick)
	{
		pick = ServerPick();
		lua_getglobal(state, name);
		if( !lua_istable(state, -1) ) {
			lua_pop(state, 1);
			return;
		}
		const int table = lua_gettop(state);
		pick.folder = LuaStringField(state, table, "folder");
		pick.fileName = LuaStringField(state, table, "file");
		lua_getfield(state, table, "server_id");
		const bool hasId = lua_type(state, -1) == LUA_TNUMBER;
		if( hasId )
			pick.hostId = (int)lua_tointeger(state, -1);
		lua_pop(state, 1);
		lua_pop(state, 1);
		pick.valid = hasId && !pick.folder.empty() && !pick.fileName.empty();
	}

	void AppendServerPick(std::string& text, const char* name, const ServerPick& pick)
	{
		if( !pick.valid )
			return;
		text += name;
		text += " = {\r\n\tfolder = \"";
		text += EscapeLuaString(ToUtf8(pick.folder));
		text += "\",\r\n\tfile = \"";
		text += EscapeLuaString(ToUtf8(pick.fileName));
		text += "\",\r\n\tserver_id = ";
		text += std::to_string(pick.hostId);
		text += ",\r\n}\r\n";
	}

} // namespace

std::wstring GetAppConfigPath()
{
	wchar_t exe[MAX_PATH];
	const DWORD n = ::GetModuleFileNameW(NULL, exe, MAX_PATH);
	if( n == 0 || n >= MAX_PATH )
		return L"GameV5.conf";
	::PathRemoveFileSpecW(exe);
	return std::wstring(exe) + L"\\GameV5.conf";
}

bool LoadAppSettings(AppSettings& settings)
{
	settings = AppSettings();
	const std::wstring path = GetAppConfigPath();
	std::string text;
	if( !ReadTextFile(path, text) )
		return true;

	LuaState state;
	if( state.L == NULL )
		return false;
	OpenSandbox(state.L);
	if( luaL_loadbuffer(state.L, text.data(), text.size(), "=GameV5.conf") != LUA_OK ) {
		lua_pop(state.L, 1);
		return false;
	}
	if( lua_pcall(state.L, 0, 0, 0) != LUA_OK ) {
		lua_pop(state.L, 1);
		return false;
	}
	lua_getglobal(state.L, "show_nav");
	if( lua_isboolean(state.L, -1) )
		settings.showNav = lua_toboolean(state.L, -1) != 0;
	lua_pop(state.L, 1);

	lua_getglobal(state.L, "server_folders");
	if( lua_istable(state.L, -1) ) {
		const int table = lua_gettop(state.L);
		const lua_Integer count = luaL_len(state.L, table);
		for( lua_Integer i = 1; i <= count; ++i ) {
			lua_geti(state.L, table, i);
			if( lua_type(state.L, -1) == LUA_TSTRING ) {
				size_t len = 0;
				const char* value = lua_tolstring(state.L, -1, &len);
				if( value != NULL && len > 0 )
					settings.serverFolders.push_back(ToWide(std::string(value, len)));
			}
			lua_pop(state.L, 1);
		}
	}
	lua_pop(state.L, 1);

	lua_getglobal(state.L, "collapsed_folders");
	if( lua_istable(state.L, -1) ) {
		const int table = lua_gettop(state.L);
		const lua_Integer count = luaL_len(state.L, table);
		for( lua_Integer i = 1; i <= count; ++i ) {
			lua_geti(state.L, table, i);
			if( lua_type(state.L, -1) == LUA_TSTRING ) {
				size_t len = 0;
				const char* value = lua_tolstring(state.L, -1, &len);
				if( value != NULL && len > 0 )
					settings.collapsedFolders.push_back(ToWide(std::string(value, len)));
			}
			lua_pop(state.L, 1);
		}
	}
	lua_pop(state.L, 1);

	LoadServerPick(state.L, "export_actor", settings.exportActor);
	LoadServerPick(state.L, "export_server", settings.exportServer);
	return true;
}

bool SaveAppSettings(const AppSettings& settings)
{
	std::string text = "-- GameV5 配置，与程序放在同一目录。\r\n"
		"-- show_nav 为 false 时启动后隐藏左侧功能菜单，默认 true。\r\n"
		"show_nav = ";
	text += settings.showNav ? "true\r\n" : "false\r\n";
	text += "-- server_folders 中的目录会在启动时自动加载。\r\n"
		"server_folders = {\r\n";
	for( size_t i = 0; i < settings.serverFolders.size(); ++i ) {
		text += "\t\"";
		text += EscapeLuaString(ToUtf8(settings.serverFolders[i]));
		text += "\",\r\n";
	}
	text += "}\r\n";
	text += "-- collapsed_folders 中的分类在服务器列表里保持折叠。\r\n"
		"collapsed_folders = {\r\n";
	for( size_t i = 0; i < settings.collapsedFolders.size(); ++i ) {
		text += "\t\"";
		text += EscapeLuaString(ToUtf8(settings.collapsedFolders[i]));
		text += "\",\r\n";
	}
	text += "}\r\n";
	text += "-- export_actor / export_server 分别记住导号、导服上次选择的服务器。\r\n"
		"-- folder 是分类目录，file 是该目录下的配置文件名，server_id 在不同分类里可以相同。\r\n";
	AppendServerPick(text, "export_actor", settings.exportActor);
	AppendServerPick(text, "export_server", settings.exportServer);

	const std::wstring path = GetAppConfigPath();
	HANDLE file = ::CreateFileW(path.c_str(), GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
	if( file == INVALID_HANDLE_VALUE )
		return false;
	DWORD written = 0;
	const BOOL ok = ::WriteFile(file, text.data(), (DWORD)text.size(), &written, NULL);
	::CloseHandle(file);
	return ok && written == text.size();
}
