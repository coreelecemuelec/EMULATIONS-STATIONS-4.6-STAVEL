#include "ApiSystem.h"
#include <stdlib.h>
#if !defined(WIN32)
#include <sys/statvfs.h>
#endif
#include <sstream>
#include "Settings.h"
#include <iostream>
#include <fstream>
#include "Log.h"
#include "HttpReq.h"
#include <chrono>
#include <thread>

#include "AudioManager.h"
#include "VolumeControl.h"
#include "InputManager.h"
#include <SystemConf.h>

#include <stdio.h>
#include <string.h>
#include <sys/types.h>

#if !defined(WIN32)
#include <ifaddrs.h>
#include <netinet/in.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <arpa/inet.h>
#endif

#include "utils/FileSystemUtil.h"
#include "utils/StringUtil.h"
#include <fstream>
#include <SDL.h>
#include <Sound.h>

#if WIN32
#include <Windows.h>
#define popen _popen
#define pclose _pclose
#define WIFEXITED(x) x
#define WEXITSTATUS(x) x
#include "Win32ApiSystem.h"
#endif

#include <pugixml/src/pugixml.hpp>

ApiSystem::ApiSystem() 
{
}

ApiSystem* ApiSystem::instance = NULL;

ApiSystem *ApiSystem::getInstance() 
{
	if (ApiSystem::instance == NULL)
#if WIN32
		ApiSystem::instance = new Win32ApiSystem();
#else
		ApiSystem::instance = new ApiSystem();
#endif

	return ApiSystem::instance;
}

unsigned long ApiSystem::getFreeSpaceGB(std::string mountpoint) 
{
	LOG(LogDebug) << "ApiSystem::getFreeSpaceGB";

	int free = 0;

#if !WIN32
	struct statvfs fiData;
	if ((statvfs(mountpoint.c_str(), &fiData)) >= 0)
		free = (fiData.f_bfree * fiData.f_bsize) / (1024 * 1024 * 1024);
#endif

	return free;
}

std::string ApiSystem::getFreeSpaceInfo() 
{
	LOG(LogDebug) << "ApiSystem::getFreeSpaceInfo";

	std::ostringstream oss;

#if !WIN32
	struct statvfs fiData;
#ifdef _ENABLEEMUELEC
	if ((statvfs("/storage/", &fiData)) < 0)
		return "";
#else
	if ((statvfs("/userdata/", &fiData)) < 0)
		return "";
#endif		
	unsigned long total = (fiData.f_blocks * (fiData.f_bsize / 1024)) / (1024L * 1024L);
	unsigned long free = (fiData.f_bfree * (fiData.f_bsize / 1024)) / (1024L * 1024L);
	unsigned long used = total - free;
	unsigned long percent = 0;
	
	if (total != 0) 
	{  //for small SD card ;) with share < 1GB
		percent = used * 100 / total;
		oss << used << "GB/" << total << "GB (" << percent << "%)";
	}
	else
		oss << "N/A";	
#endif

	return oss.str();
}

bool ApiSystem::isFreeSpaceLimit() 
{
#ifdef _ENABLEEMUELEC
	return getFreeSpaceGB("/storage/") < 2;
#else
	return getFreeSpaceGB("/userdata/") < 2;
#endif
}

std::string ApiSystem::getVersion() 
{
	LOG(LogDebug) << "ApiSystem::getVersion";
#ifdef _ENABLEEMUELEC
	std::ifstream ifs("/storage/.config/EE_VERSION");
#else
	std::ifstream ifs("/usr/share/batocera/batocera.version");
#endif

	if (ifs.good()) 
	{
		std::string contents;
		std::getline(ifs, contents);
		return contents;
	}

	return "";
}

bool ApiSystem::setOverscan(bool enable) 
{
	return executeScript("batocera-config overscan " + std::string(enable ? "enable" : "disable"));
}

bool ApiSystem::setOverclock(std::string mode) 
{
#ifdef _ENABLEEMUELEC
	return true;
#endif
	if (mode.empty())
		return false;

	return executeScript("batocera-overclock set " + mode);
}

// BusyComponent* ui
std::pair<std::string, int> ApiSystem::updateSystem(const std::function<void(const std::string)>& func)
{
	LOG(LogDebug) << "ApiSystem::updateSystem";

#ifdef _ENABLEEMUELEC	
	std::string updatecommand = "emuelec-upgrade";
	std::string updatelog = "/storage/.config/emuelec/logs/emuelec-upgrade.log";
#else
	std::string updatecommand = "batocera-upgrade";
	std::string updatelog = "/userdata/system/logs/batocera-upgrade.log";
#endif

	FILE *pipe = popen(updatecommand.c_str(), "r");
	if (pipe == nullptr)
		return std::pair<std::string, int>(std::string("Cannot call update command"), -1);

	char line[1024] = "";
	FILE *flog = fopen(updatelog.c_str(), "w");
	while (fgets(line, 1024, pipe)) 
	{
		strtok(line, "\n");
		if (flog != nullptr) 
			fprintf(flog, "%s\n", line);

		if (func != nullptr)
			func(std::string(line));		
	}

	int exitCode = pclose(pipe);

	if (flog != NULL)
	{
		fprintf(flog, "Exit code : %d\n", exitCode);
		fclose(flog);
	}

	return std::pair<std::string, int>(std::string(line), exitCode);
}

std::pair<std::string, int> ApiSystem::backupSystem(BusyComponent* ui, std::string device) 
{
	LOG(LogDebug) << "ApiSystem::backupSystem";

	std::string updatecommand = "batocera-sync sync " + device;
	FILE* pipe = popen(updatecommand.c_str(), "r");
	if (pipe == NULL)
		return std::pair<std::string, int>(std::string("Cannot call sync command"), -1);

	char line[1024] = "";

	FILE* flog = fopen("/userdata/system/logs/batocera-sync.log", "w");
	while (fgets(line, 1024, pipe)) 
	{
		strtok(line, "\n");

		if (flog != NULL) 
			fprintf(flog, "%s\n", line);

		ui->setText(std::string(line));
	}

	if (flog != NULL) 
		fclose(flog);

	int exitCode = pclose(pipe);
	return std::pair<std::string, int>(std::string(line), exitCode);
}

std::pair<std::string, int> ApiSystem::installSystem(BusyComponent* ui, std::string device, std::string architecture) 
{
	LOG(LogDebug) << "ApiSystem::installSystem";

	std::string updatecommand = "batocera-install install " + device + " " + architecture;
	FILE *pipe = popen(updatecommand.c_str(), "r");
	if (pipe == NULL)
		return std::pair<std::string, int>(std::string("Cannot call install command"), -1);

	char line[1024] = "";

	FILE *flog = fopen("/userdata/system/logs/batocera-install.log", "w");
	while (fgets(line, 1024, pipe)) 
	{
		strtok(line, "\n");
		if (flog != NULL) fprintf(flog, "%s\n", line);
		ui->setText(std::string(line));
	}

	int exitCode = pclose(pipe);

	if (flog != NULL)
	{
		fprintf(flog, "Exit code : %d\n", exitCode);
		fclose(flog);
	}

	return std::pair<std::string, int>(std::string(line), exitCode);
}

std::pair<std::string, int> ApiSystem::scrape(BusyComponent* ui) 
{
	LOG(LogDebug) << "ApiSystem::scrape";

	FILE* pipe = popen("batocera-scraper", "r");
	if (pipe == nullptr)
		return std::pair<std::string, int>(std::string("Cannot call scrape command"), -1);
	char line[1024] = "";
#ifdef _ENABLEEMUELEC
	FILE* flog = fopen("/storage/.config/emuelec/logs/emuelec-scraper.log", "w");
#else	
	FILE* flog = fopen("/userdata/system/logs/batocera-scraper.log", "w");
#endif
	while (fgets(line, 1024, pipe)) 
	{
		strtok(line, "\n");

		if (flog != NULL) 
			fprintf(flog, "%s\n", line);

		if (ui != nullptr && Utils::String::startsWith(line, "GAME: "))
			ui->setText(std::string(line));	
	}

	if (flog != nullptr)
		fclose(flog);

	int exitCode = pclose(pipe);
	return std::pair<std::string, int>(std::string(line), exitCode);
}

bool ApiSystem::ping() 
{
	return executeScript("timeout 1 ping -c 1 -t 1000 google.com");
}

bool ApiSystem::canUpdate(std::vector<std::string>& output) 
{
	LOG(LogDebug) << "ApiSystem::canUpdate";

	FILE *pipe = popen("batocera-config canupdate", "r");
	if (pipe == NULL)
		return false;

	char line[1024];
	while (fgets(line, 1024, pipe)) 
	{
		strtok(line, "\n");
		output.push_back(std::string(line));
	}

	int res = WEXITSTATUS(pclose(pipe));
	if (res == 0) 
	{
		LOG(LogInfo) << "Can update ";
		return true;
	}

	LOG(LogInfo) << "Cannot update ";
	return false;
}

void ApiSystem::launchExternalWindow_before(Window *window) 
{
	LOG(LogDebug) << "ApiSystem::launchExternalWindow_before";

	AudioManager::getInstance()->deinit();
	VolumeControl::getInstance()->deinit();
	window->deinit();

	LOG(LogDebug) << "ApiSystem::launchExternalWindow_before OK";
}

void ApiSystem::launchExternalWindow_after(Window *window) 
{
	LOG(LogDebug) << "ApiSystem::launchExternalWindow_after";

	window->init();
	VolumeControl::getInstance()->init();
	AudioManager::getInstance()->init();
	window->normalizeNextUpdate();
	window->reactivateGui();

	AudioManager::getInstance()->playRandomMusic();

	LOG(LogDebug) << "ApiSystem::launchExternalWindow_after OK";
}

bool ApiSystem::launchKodi(Window *window) 
{
	LOG(LogDebug) << "ApiSystem::launchKodi";

	std::string commandline = InputManager::getInstance()->configureEmulators();
	std::string command = "python /usr/lib/python2.7/site-packages/configgen/emulatorlauncher.py -system kodi -rom '' " + commandline;

	ApiSystem::launchExternalWindow_before(window);

	int exitCode = system(command.c_str());

	// WIFEXITED returns a nonzero value if the child process terminated normally with exit or _exit.
	// https://www.gnu.org/software/libc/manual/html_node/Process-Completion-Status.html
	if (WIFEXITED(exitCode))
		exitCode = WEXITSTATUS(exitCode);

	ApiSystem::launchExternalWindow_after(window);

	// handle end of kodi
	switch (exitCode) 
	{
	case 10: // reboot code
		reboot();
		return true;
		break;
	case 11: // shutdown code
		shutdown();
		return true;
		break;
	}

	return exitCode == 0;
}

bool ApiSystem::launchFileManager(Window *window) 
{
	LOG(LogDebug) << "ApiSystem::launchFileManager";

	std::string command = "filemanagerlauncher";

	ApiSystem::launchExternalWindow_before(window);

	int exitCode = system(command.c_str());
	if (WIFEXITED(exitCode))
		exitCode = WEXITSTATUS(exitCode);

	ApiSystem::launchExternalWindow_after(window);

	return exitCode == 0;
}

bool ApiSystem::enableWifi(std::string ssid, std::string key) 
{
#ifdef _ENABLEEMUELEC
	return executeScript("batocera-config wifi enable \"" + ssid + "\" \"" + key + "\"");
#else
	return executeScript("batocera-wifi enable \"" + ssid + "\" \"" + key + "\"");
#endif
}

bool ApiSystem::disableWifi() 
{
#ifdef _ENABLEEMUELEC
	return executeScript("batocera-config wifi disable");
#else
	return executeScript("batocera-wifi disable");
#endif
}

bool ApiSystem::halt(bool reboot, bool fast) 
{
	LOG(LogDebug) << "ApiSystem::halt";

	SDL_Event *quit = new SDL_Event();
	quit->type = (fast ? SDL_FAST_QUIT : SDL_QUIT) | (reboot ? SDL_SYS_REBOOT : SDL_SYS_SHUTDOWN);
	SDL_PushEvent(quit);

	return 0;
}

std::string ApiSystem::getIpAdress() 
{
	LOG(LogDebug) << "ApiSystem::getIpAdress";

	std::string result = "NOT CONNECTED";

#if !WIN32
	struct ifaddrs *ifAddrStruct = NULL;
	struct ifaddrs *ifa = NULL;
	void *tmpAddrPtr = NULL;
	
	getifaddrs(&ifAddrStruct);

	for (ifa = ifAddrStruct; ifa != NULL; ifa = ifa->ifa_next) {
		if (!ifa->ifa_addr) {
			continue;
		}
		if (ifa->ifa_addr->sa_family == AF_INET) { // check it is IP4
			// is a valid IP4 Address
			tmpAddrPtr = &((struct sockaddr_in *) ifa->ifa_addr)->sin_addr;
			char addressBuffer[INET_ADDRSTRLEN];
			inet_ntop(AF_INET, tmpAddrPtr, addressBuffer, INET_ADDRSTRLEN);
			printf("%s IP Address %s\n", ifa->ifa_name, addressBuffer);
			if (std::string(ifa->ifa_name).find("eth") != std::string::npos ||
				std::string(ifa->ifa_name).find("wlan") != std::string::npos) {
				result = std::string(addressBuffer);
			}
		}
	}
	// Seeking for ipv6 if no IPV4
	if (result.compare("NOT CONNECTED") == 0) {
		for (ifa = ifAddrStruct; ifa != NULL; ifa = ifa->ifa_next) {
			if (!ifa->ifa_addr) {
				continue;
			}
			if (ifa->ifa_addr->sa_family == AF_INET6) { // check it is IP6
				// is a valid IP6 Address
				tmpAddrPtr = &((struct sockaddr_in6 *) ifa->ifa_addr)->sin6_addr;
				char addressBuffer[INET6_ADDRSTRLEN];
				inet_ntop(AF_INET6, tmpAddrPtr, addressBuffer, INET6_ADDRSTRLEN);
				printf("%s IP Address %s\n", ifa->ifa_name, addressBuffer);
				if (std::string(ifa->ifa_name).find("eth") != std::string::npos ||
					std::string(ifa->ifa_name).find("wlan") != std::string::npos) {
					return std::string(addressBuffer);
				}
			}
		}
	}
	if (ifAddrStruct != NULL) 
		freeifaddrs(ifAddrStruct);
#endif

	return result;

}

bool ApiSystem::scanNewBluetooth(const std::function<void(const std::string)>& func)
{
	return executeScript("batocera-bluetooth trust");
}

std::vector<std::string> ApiSystem::getBluetoothDeviceList()
{
	return executeEnumerationScript("batocera-bluetooth list");
}

bool ApiSystem::removeBluetoothDevice(const std::string deviceName)
{
	return executeScript("batocera-bluetooth remove "+ deviceName);
}

std::vector<std::string> ApiSystem::getAvailableStorageDevices() 
{
	return executeEnumerationScript("batocera-config storage list");
}

std::vector<std::string> ApiSystem::getVideoModes() 
{
	return executeEnumerationScript("batocera-resolution listModes");
}

std::vector<std::string> ApiSystem::getAvailableBackupDevices() 
{
	return executeEnumerationScript("batocera-sync list");
}

std::vector<std::string> ApiSystem::getAvailableInstallDevices() 
{
	return executeEnumerationScript("batocera-install listDisks");
}

std::vector<std::string> ApiSystem::getAvailableInstallArchitectures() 
{
	return executeEnumerationScript("batocera-install listArchs");
}

std::vector<std::string> ApiSystem::getAvailableOverclocking() 
{
#ifdef _ENABLEEMUELEC
	return executeEnumerationScript("echo no");
#else
	return executeEnumerationScript("batocera-overclock list");
#endif
}

std::vector<std::string> ApiSystem::getSystemInformations() 
{
	return executeEnumerationScript("batocera-info");
}

std::vector<BiosSystem> ApiSystem::getBiosInformations() 
{
	std::vector<BiosSystem> res;
	BiosSystem current;
	bool isCurrent = false;

	auto systems = executeEnumerationScript("batocera-systems");
	for (auto line : systems)
	{
		if (Utils::String::startsWith(line, "> ")) 
		{
			if (isCurrent)
				res.push_back(current);

			isCurrent = true;
			current.name = std::string(std::string(line).substr(2));
			current.bios.clear();
		}
		else 
		{
			BiosFile biosFile;
			std::vector<std::string> tokens = Utils::String::split(line, ' ');
			if (tokens.size() >= 3) 
			{
				biosFile.status = tokens.at(0);
				biosFile.md5 = tokens.at(1);

				// concatenat the ending words
				std::string vname = "";
				for (unsigned int i = 2; i < tokens.size(); i++) 
				{
					if (i > 2) vname += " ";
					vname += tokens.at(i);
				}
				biosFile.path = vname;

				current.bios.push_back(biosFile);
			}
		}
	}

	if (isCurrent)
		res.push_back(current);

	return res;
}

bool ApiSystem::generateSupportFile() 
{
	return executeScript("batocera-support");
}

std::string ApiSystem::getCurrentStorage() 
{
	LOG(LogDebug) << "ApiSystem::getCurrentStorage";

#if WIN32
	return "DEFAULT";
#endif

	std::ostringstream oss;
	oss << "batocera-config storage current";
	FILE *pipe = popen(oss.str().c_str(), "r");
	char line[1024];

	if (pipe == NULL)
		return "";	

	if (fgets(line, 1024, pipe)) {
		strtok(line, "\n");
		pclose(pipe);
		return std::string(line);
	}
	return "INTERNAL";
}

bool ApiSystem::setStorage(std::string selected) 
{
	return executeScript("batocera-config storage " + selected);
}

bool ApiSystem::forgetBluetoothControllers() 
{
	return executeScript("batocera-config forgetBT");
}

std::string ApiSystem::getRootPassword() 
{
	LOG(LogDebug) << "ApiSystem::getRootPassword";

	std::ostringstream oss;
	oss << "batocera-config getRootPassword";
	FILE *pipe = popen(oss.str().c_str(), "r");
	char line[1024];

	if (pipe == NULL) {
		return "";
	}

	if (fgets(line, 1024, pipe)) {
		strtok(line, "\n");
		pclose(pipe);
		return std::string(line);
	}
	return oss.str().c_str();
}

std::vector<std::string> ApiSystem::getAvailableAudioOutputDevices() 
{
#if WIN32
	std::vector<std::string> res;
	res.push_back("auto");
	return res;
#endif

	return executeEnumerationScript("batocera-audio list");
}

std::vector<std::string> ApiSystem::getAvailableVideoOutputDevices() 
{
	return executeEnumerationScript("batocera-config lsoutputs");
}

std::string ApiSystem::getCurrentAudioOutputDevice() 
{
#if WIN32
	return "auto";
#endif

	LOG(LogDebug) << "ApiSystem::getCurrentAudioOutputDevice";

	std::ostringstream oss;
	oss << "batocera-audio get";
	FILE *pipe = popen(oss.str().c_str(), "r");
	char line[1024];

	if (pipe == NULL)
		return "";	

	if (fgets(line, 1024, pipe)) 
	{
		strtok(line, "\n");
		pclose(pipe);
		return std::string(line);
	}

	return "";
}

bool ApiSystem::setAudioOutputDevice(std::string selected) 
{
	LOG(LogDebug) << "ApiSystem::setAudioOutputDevice";

	std::ostringstream oss;

	AudioManager::getInstance()->deinit();
	VolumeControl::getInstance()->deinit();

	oss << "batocera-audio set" << " '" << selected << "'";
	int exitcode = system(oss.str().c_str());

	VolumeControl::getInstance()->init();
	AudioManager::getInstance()->init();
	Sound::get("/usr/share/emulationstation/resources/checksound.ogg")->play();

	return exitcode == 0;
}

// Batocera
RetroAchievementInfo ApiSystem::getRetroAchievements()
{
	RetroAchievementInfo info;

	LOG(LogDebug) << "ApiSystem::getRetroAchievements";
	
	auto res = executeEnumerationScript("batocera-retroachievements-info " + SystemConf::getInstance()->get("global.retroachievements.username"));
	std::string data = Utils::String::join(res, "\n");
	if (data.empty())
	{
		info.error = "Error accessing 'batocera-retroachievements-info' script";
		return info;
	}

	pugi::xml_document doc;
	pugi::xml_parse_result result = doc.load(data.c_str()); // doc.load_buffer(data.c_str(), data.size);

	if (!result)
	{
		// Temporary retrocompatibility mode
		auto lines = Utils::String::split(data, '\n');
		if (lines.size() == 1)
		{
			info.error = lines[0];
			return info;
		}

		for (auto line : lines)
		{
			std::vector<std::string> tokens = Utils::String::split(line, '@');
			if (tokens.size() == 0)
				continue;

			if (tokens.size() == 1)
			{					
				if (info.username.empty())
				{
					auto userParse = Utils::String::split(line, ' ');
					if (userParse.size() > 2)
						info.username = userParse[1];

					auto infoParsePoints = Utils::String::splitAny(line, "()");
					if (infoParsePoints.size() > 3)
					{
						info.totalpoints = infoParsePoints[1];
						info.rank = Utils::String::replace(infoParsePoints[2], " is ", "") + " (" + infoParsePoints[3] + ")";
					}
				}

				continue;
			}

			RetroAchievementGame rg;
			rg.name = tokens[0];
			rg.achievements = Utils::String::replace(tokens[1], " achievements", "");

			if (tokens.size() >= 4)
			{
				rg.points = Utils::String::replace(tokens[2], " points", ""); 
				rg.lastplayed = Utils::String::replace(tokens[3], "Last played ", "");
			}

			info.games.push_back(rg);
		}

		return info;
	}

	pugi::xml_node root = doc.child("retroachievements");
	if (!root)
	{
		LOG(LogError) << "Could not find <retroachievements> node";
		return info;
	}

	for (pugi::xml_node node : root.children())
	{
		std::string tag = node.name();

		if (tag == "error")
		{
			info.error = node.text().get();
			break;
		}

		if (tag == "username")
			info.username = node.text().get();
		else if (tag == "totalpoints")
			info.totalpoints = node.text().get();
		else if (tag == "rank")
			info.rank = node.text().get();
		else if (tag == "userpic")
		{
			std::string userpic = node.text().get();
			if (!userpic.empty())
			{
				std::string localPath = Utils::FileSystem::getGenericPath(Utils::FileSystem::getEsConfigPath() + "/tmp");
				if (!Utils::FileSystem::exists(localPath))
					Utils::FileSystem::createDirectory(localPath);

				std::string localFile = localPath + "/" + Utils::FileSystem::getFileName(userpic);
				if (!Utils::FileSystem::exists(localFile))
				{
					HttpReq httpreq(userpic, localFile);
					httpreq.wait();
				}

				if (Utils::FileSystem::exists(localFile))
					info.userpic = localFile;
			}
		}
		else if (tag == "registered")
			info.registered = node.text().get();
		else if (tag == "game")
		{
			RetroAchievementGame rg;

			for (pugi::xml_node game : node.children())
			{
				tag = game.name();

				if (tag == "name")
					rg.name = game.text().get();
				else if (tag == "achievements")
					rg.achievements = game.text().get();
				else if (tag == "points")
					rg.points = game.text().get();
				else if (tag == "lastplayed")
					rg.lastplayed = game.text().get();
				else if (tag == "badge")
				{
					std::string badge = game.text().get();

					if (!badge.empty())
					{
						std::string localPath = Utils::FileSystem::getGenericPath(Utils::FileSystem::getEsConfigPath() + "/tmp");
						if (!Utils::FileSystem::exists(localPath))
							Utils::FileSystem::createDirectory(localPath);

						std::string localFile = localPath + "/" + Utils::FileSystem::getFileName(badge);
						if (!Utils::FileSystem::exists(localFile))
						{
							HttpReq httpreq(badge, localFile);
							httpreq.wait();
						}

						if (Utils::FileSystem::exists(localFile))
							rg.badge = localFile;
					}
				}
			}

			if (!rg.name.empty())
				info.games.push_back(rg);
		}
	}

	return info;
}

std::vector<std::string> ApiSystem::getBatoceraThemesList() 
{
	LOG(LogDebug) << "ApiSystem::getBatoceraThemesList";

	std::vector<std::string> res;

	std::string command = "batocera-es-theme list";
	FILE *pipe = popen(command.c_str(), "r");
	if (pipe == NULL)
		return res;

	char line[1024];
	char *pch;

	while (fgets(line, 1024, pipe)) 
	{
		strtok(line, "\n");
		// provide only themes that are [A]vailable or [I]nstalled as a result
		// (Eliminate [?] and other non-installable lines of text)
		if ((strncmp(line, "[A]", 3) == 0) || (strncmp(line, "[I]", 3) == 0))
			res.push_back(std::string(line));
	}
	pclose(pipe);
	return res;
}

std::pair<std::string, int> ApiSystem::installBatoceraTheme(std::string thname, const std::function<void(const std::string)>& func)
{
	LOG(LogInfo) << "ApiSystem::installBatoceraTheme " << thname;

	std::string command = "batocera-es-theme install " + thname;

	FILE *pipe = popen(command.c_str(), "r");
	char line[1024] = "";
	if (pipe == NULL)
		return std::pair<std::string, int>(std::string("Error starting `batocera-es-theme` command."), -1);	

	while (fgets(line, 1024, pipe)) 
	{
		strtok(line, "\n");	
		if (strlen(line) > 48)
			line[47] = '\0';

		if (func != nullptr)
			func(std::string(line));		
	}

	int exitCode = pclose(pipe);
	return std::pair<std::string, int>(std::string(line), exitCode);
}

std::vector<std::string> ApiSystem::getBatoceraBezelsList() 
{
	LOG(LogInfo) << "ApiSystem::getBatoceraBezelsList";

	std::vector<std::string> res;

	std::string command = "batocera-es-thebezelproject list";
	FILE *pipe = popen(command.c_str(), "r");
	if (pipe == NULL)
		return res;

	char line[1024];
	char *pch;

	while (fgets(line, 1024, pipe)) 
	{
		strtok(line, "\n");
		// provide only themes that are [A]vailable or [I]nstalled as a result
		// (Eliminate [?] and other non-installable lines of text)
		if ((strncmp(line, "[A]", 3) == 0) || (strncmp(line, "[I]", 3) == 0))
			res.push_back(std::string(line));
	}
	pclose(pipe);
	return res;
}

std::pair<std::string, int> ApiSystem::installBatoceraBezel(std::string bezelsystem, const std::function<void(const std::string)>& func)
{
	LOG(LogDebug) << "ApiSystem::installBatoceraBezel";

	std::string updatecommand = std::string("batocera-es-thebezelproject install ") + bezelsystem;
	LOG(LogWarning) << "Installing bezels for " << bezelsystem;
	FILE *pipe = popen(updatecommand.c_str(), "r");
	if (pipe == NULL)
		return std::pair<std::string, int>(std::string("Error starting `batocera-es-thebezelproject install` command."), -1);

	char line[1024] = "";

	while (fgets(line, 1024, pipe)) 
	{
		strtok(line, "\n");
		// Long theme names/URL can crash the GUI MsgBox
		// "48" found by trials and errors. Ideally should be fixed
		// in es-core MsgBox -- FIXME
		if (strlen(line) > 48)
			line[47] = '\0';

		if (func != nullptr)
			func(std::string(line));			
	}

	int exitCode = pclose(pipe);
	return std::pair<std::string, int>(std::string(line), exitCode);
}

std::pair<std::string, int> ApiSystem::uninstallBatoceraBezel(BusyComponent* ui, std::string bezelsystem) 
{
	LOG(LogInfo) << "ApiSystem::uninstallBatoceraBezel " << bezelsystem;

	std::string updatecommand = "batocera-es-thebezelproject remove " + bezelsystem;
	
	FILE *pipe = popen(updatecommand.c_str(), "r");
	if (pipe == NULL)
		return std::pair<std::string, int>(std::string("Error starting 'batocera-es-thebezelproject remove' command."), -1);

	char line[1024] = "";

	while (fgets(line, 1024, pipe)) 
	{
		strtok(line, "\n");
		// Long theme names/URL can crash the GUI MsgBox
		// "48" found by trials and errors. Ideally should be fixed
		// in es-core MsgBox -- FIXME
		if (strlen(line) > 48)
			line[47] = '\0';
		ui->setText(std::string(line));
	}

	int exitCode = pclose(pipe);
	return std::pair<std::string, int>(std::string(line), exitCode);
}

std::string ApiSystem::getCRC32(std::string fileName, bool fromZipContents)
{
	std::string cmd = "7zr h \"" + fileName + "\"";
	
	std::string ext = Utils::String::toLower(Utils::FileSystem::getExtension(fileName));
	if (fromZipContents && (ext == ".7z" || ext == ".zip"))
		cmd = "7zr l -slt \"" + fileName + "\"";

	std::string crc;
	std::string fn = Utils::FileSystem::getFileName(fileName);

	FILE *pipe = popen(cmd.c_str(), "r");
	if (pipe == NULL)
		return "";

	char line[1024];
	while (fgets(line, 1024, pipe)) 
	{
		strtok(line, "\n");
		std::string all = line;

		int idx = all.find("CRC = ");
		if (idx != std::string::npos)
			crc = all.substr(idx + 6);
		else if (all.find(fn) == (all.size() - fn.size()) && all.length() > 8 && all[9] == ' ')
			crc = all.substr(0, 8);
	}
	
	pclose(pipe);

	return crc;
}

const char* BACKLIGHT_BRIGHTNESS_NAME = "/sys/class/backlight/backlight/brightness";
const char* BACKLIGHT_BRIGHTNESS_MAX_NAME = "/sys/class/backlight/backlight/max_brightness";
#define BACKLIGHT_BUFFER_SIZE 127

bool ApiSystem::getBrighness(int& value)
{
#if WIN32
	return false;
#else
	value = 0;

	int fd;
	int max = 255;	
	char buffer[BACKLIGHT_BUFFER_SIZE + 1];
	ssize_t count;

	fd = open(BACKLIGHT_BRIGHTNESS_MAX_NAME, O_RDONLY);
	if (fd < 0)
		return false;

	memset(buffer, 0, BACKLIGHT_BUFFER_SIZE + 1);

	count = read(fd, buffer, BACKLIGHT_BUFFER_SIZE);
	if (count > 0)
		max = atoi(buffer);

	close(fd);

	if (max == 0) 
		return 0;

	fd = open(BACKLIGHT_BRIGHTNESS_NAME, O_RDONLY);
	if (fd < 0)
		return false;

	memset(buffer, 0, BACKLIGHT_BUFFER_SIZE + 1);

	count = read(fd, buffer, BACKLIGHT_BUFFER_SIZE);
	if (count > 0)
		value = atoi(buffer);

	close(fd);

	value = (uint32_t) (value / (float)max * 100.0f);
	return true;
#endif
}

void ApiSystem::setBrighness(int value)
{
#if !WIN32	
	if (value < 5)
		value = 5;

	if (value > 100)
		value = 100;

	int fd;
	int max = 255;
	char buffer[BACKLIGHT_BUFFER_SIZE + 1];
	ssize_t count;

	fd = open(BACKLIGHT_BRIGHTNESS_MAX_NAME, O_RDONLY);
	if (fd < 0)
		return;

	memset(buffer, 0, BACKLIGHT_BUFFER_SIZE + 1);

	count = read(fd, buffer, BACKLIGHT_BUFFER_SIZE);
	if (count > 0)
		max = atoi(buffer);

	close(fd);

	if (max == 0) 
		return;

	fd = open(BACKLIGHT_BRIGHTNESS_NAME, O_WRONLY);
	if (fd < 0)
		return;
	
	float percent = value / 100.0f * (float)max;
	sprintf(buffer, "%d\n", (uint32_t)percent);

	count = write(fd, buffer, strlen(buffer));
	if (count < 0)
		LOG(LogError) << "ApiSystem::setBrighness failed";

	close(fd);
#endif
}

std::vector<std::string> ApiSystem::getWifiNetworks(bool scan)
{
	return executeEnumerationScript(scan ? "batocera-wifi scanlist" : "batocera-wifi list");
}

std::vector<std::string> ApiSystem::executeEnumerationScript(const std::string command)
{
	LOG(LogDebug) << "ApiSystem::executeEnumerationScript -> " << command;

	std::vector<std::string> res;

	FILE *pipe = popen(command.c_str(), "r");
	if (pipe == NULL)
		return res;

	char line[1024];
	while (fgets(line, 1024, pipe))
	{
		strtok(line, "\n");
		res.push_back(std::string(line));
	}

	pclose(pipe);
	return res;
}

bool ApiSystem::executeScript(const std::string command)
{	
	LOG(LogInfo) << "Running " << command;

	if (system(command.c_str()) == 0)
		return true;
	
	LOG(LogError) << "Error executing " << command;
	return false;
}

bool ApiSystem::isScriptingSupported(ScriptId script)
{
	return true;
}

bool ApiSystem::downloadFile(const std::string url, const std::string fileName, const std::string label, const std::function<void(const std::string)>& func)
{
	if (func != nullptr)
		func("Downloading " + label);

	HttpReq httpreq(url, fileName);
	while (httpreq.status() == HttpReq::REQ_IN_PROGRESS)
	{
		if (func != nullptr)
			func(std::string("Downloading " + label + " >>> " + std::to_string(httpreq.getPercent()) + " %"));

		std::this_thread::sleep_for(std::chrono::milliseconds(20));
	}

	if (httpreq.status() != HttpReq::REQ_SUCCESS)
		return false;

	return true;
}

void ApiSystem::setReadyFlag(bool ready)
{
	if (!ready)
	{
		Utils::FileSystem::removeFile("/tmp/emulationstation.ready");
		return;
	}

	FILE* fd = fopen("/tmp/emulationstation.ready", "w");
	if (fd != NULL) 
		fclose(fd);
}

bool ApiSystem::isReadyFlagSet()
{
	return Utils::FileSystem::exists("/tmp/emulationstation.ready");
}