#pragma once

#include <string>
#include <vector>

struct ServerConfig
{
	std::wstring fileName;
	std::wstring filePath;
	int hostId;
	int loginPort;
	int mcsPort;
	std::wstring dbHost;
	int dbPort;
	std::wstring dbName;
	std::wstring dbUser;
	std::wstring dbPassword;
};

struct ServerFolder
{
	std::wstring path;
	std::vector<ServerConfig> servers;
};

// 用分类目录、配置文件名和服务器 id 一起定位。不同分类里的 id 可以相同。
struct ServerPick
{
	std::wstring folder;
	std::wstring fileName;
	int hostId;
	bool valid;

	ServerPick() : hostId(0), valid(false) {}
};

// 只读该目录下的文件，不进入子目录。没有 host_id 的文件会被跳过。
bool LoadServerFolder(const std::wstring& folder, ServerFolder& out);

// exe 同目录的 GameV5.conf。showNav 缺省为 true。语法错误时 Load 返回 false。
struct AppSettings
{
	bool showNav;
	std::vector<std::wstring> serverFolders;
	std::vector<std::wstring> collapsedFolders;
	ServerPick exportActor;
	ServerPick exportServer;

	AppSettings() : showNav(true) {}
};

// 在已加载的分类里查找。folder、fileName、hostId 必须同时一致。
bool FindServerPick(const std::vector<ServerFolder>& folders, const ServerPick& pick, size_t& group, size_t& index);

std::wstring GetAppConfigPath();
bool LoadAppSettings(AppSettings& settings);
bool SaveAppSettings(const AppSettings& settings);
