#pragma once

#include "ServerConfig.h"
#include <string>

// 后台 HTTP 完成后把结果文本发回主窗口。wParam 是 new std::wstring，由窗口释放。
#define WM_SERVER_GM_RESULT (WM_APP + 180)

// 拼出即将执行的命令。路径无效时 ok 为 false，display 是失败原因。
bool DescribeStart(const ServerConfig& server, std::wstring& display);

// 工作目录为配置目录上一级的 skynet，参数为 ../配置目录名/文件名。
bool StartSkynet(const ServerConfig& server, std::wstring& message);

// 用 skynet.exe 的完整路径和配置参数判断这个服是否已经启动。
bool FindRunningSkynet(const ServerConfig& server, std::wstring& detail);

struct SkynetProcess
{
	std::wstring image;
	std::wstring commandLine;
};

void EnumerateSkynetProcesses(std::vector<SkynetProcess>& processes);
bool IsSkynetServerRunning(const ServerConfig& server, const std::vector<SkynetProcess>& processes);

// hotUpdate 为 false 时请求 /shutdown，为 true 时依次请求热更和回收。
void RequestGm(HWND notify, int hostId, int mcsPort, bool hotUpdate);

// 按服务器 id 删除该库中的角色及相关表数据。结果通过状态消息发回主窗口。
void ClearServerDatabase(HWND notify, const ServerConfig& server);

// 明文原样返回，Base64 的 AES 密文返回明文。
std::string DecryptDbPassword(const std::string& stored);

// 导入一份玩家 SQL：重写角色 id 和 online 账号，写入所选服务器的库。
void ImportActorSql(HWND notify, const ServerConfig& server, const std::wstring& sqlPath);

// 按 dbk 把整份数据库备份导入所选服务器。会删除备份中出现的表和存储过程。
void ImportServerDump(HWND notify, const ServerConfig& server, const std::wstring& sqlPath);
