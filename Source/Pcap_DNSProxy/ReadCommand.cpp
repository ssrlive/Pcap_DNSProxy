// This code is part of Pcap_DNSProxy
// Pcap_DNSProxy, a local DNS server based on WinPcap and LibPcap
// Copyright (C) 2012-2019 Chengr28
// 
// This program is free software; you can redistribute it and/or
// modify it under the terms of the GNU General Public License
// as published by the Free Software Foundation; either
// version 2 of the License, or (at your option) any later version.
// 
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
// 
// You should have received a copy of the GNU General Public License
// along with this program; if not, write to the Free Software
// Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.


#include "Main.h"

//Read commands from main process
#if defined(PLATFORM_WIN)
bool ReadCommand(
	int argc, 
	wchar_t *argv[])
#elif (defined(PLATFORM_FREEBSD) || defined(PLATFORM_LINUX) || defined(PLATFORM_MACOS))
bool ReadCommand(
	int argc, 
	char *argv[])
#endif
{
//Path initialization
#if defined(PLATFORM_WIN)
	auto FilePathBuffer = std::make_unique<wchar_t[]>(FILE_BUFFER_SIZE + MEMORY_RESERVED_BYTES);
	wmemset(FilePathBuffer.get(), 0, FILE_BUFFER_SIZE + MEMORY_RESERVED_BYTES);
	std::wstring FilePathString;
	size_t BufferSize = FILE_BUFFER_SIZE;
	while (!GlobalRunningStatus.IsNeedExit)
	{
	//Get full module file name which is the location of program and not its working directory.
		const auto ResultValue = GetModuleFileNameW(
			nullptr, 
			FilePathBuffer.get(), 
			static_cast<const DWORD>(BufferSize));
		if (ResultValue == 0)
		{
			std::wstring Message(L"[System Error] Path initialization error");
			if (GetLastError() == 0)
			{
				Message.append(L".\n");
				PrintToScreen(true, false, Message.c_str());
			}
			else {
				ErrorCodeToMessage(LOG_ERROR_TYPE::SYSTEM, GetLastError(), Message);
				Message.append(L".\n");
				PrintToScreen(true, false, Message.c_str(), GetLastError());
			}

			return false;
		}
		else if (ResultValue == BufferSize)
		{
		//Buffer is too small to hold the module name.
		#if defined(PLATFORM_WIN_XP)
			if (GetLastError() == ERROR_SUCCESS)
		#else
			if (GetLastError() == ERROR_INSUFFICIENT_BUFFER)
		#endif
			{
				auto FilePathBufferTemp = std::make_unique<wchar_t[]>(BufferSize + FILE_BUFFER_SIZE);
				wmemset(FilePathBufferTemp.get(), 0, BufferSize + FILE_BUFFER_SIZE);
				std::swap(FilePathBuffer, FilePathBufferTemp);
				BufferSize += FILE_BUFFER_SIZE;
			}
		//Hold the whole module name.
			else {
				FilePathString = FilePathBuffer.get();
				break;
			}
		}
		else {
		//Hold the whole module name.
			FilePathString = FilePathBuffer.get();
			break;
		}
	}

//File name initialization
	FilePathBuffer.reset();
	if (!FileNameInit(FilePathString, true, false))
	{
		PrintToScreen(true, false, L"[System Error] Path initialization error.\n");
		return false;
	}
#elif (defined(PLATFORM_FREEBSD) || defined(PLATFORM_LINUX) || defined(PLATFORM_MACOS))
	auto FilePath = getcwd(nullptr, 0);
	if (FilePath == nullptr)
	{
		std::wstring Message(L"[System Error] Path initialization error");
		if (errno == 0)
		{
			Message.append(L".\n");
			PrintToScreen(true, false, Message.c_str());
		}
		else {
			ErrorCodeToMessage(LOG_ERROR_TYPE::SYSTEM, errno, Message);
			Message.append(L".\n");
			PrintToScreen(true, false, Message.c_str(), errno);
		}

		return false;
	}
	else {
	//Copy to local storage and free pointer.
		std::string FilePathString(FilePath);
		free(FilePath);
		FilePath = nullptr;

	//File name initialization
	// If the current directory is not below the root directory of the current process(e.g., because the process set a new filesystem root
	// using chroot(2) without changing its current directory into the new root), then, since Linux 2.6.36, the returned path will be prefixed
	// with the string "(unreachable)".
		if (FilePathString.back() != ASCII_SLASH)
			FilePathString.append("/");
		if (FilePathString.compare(0, strlen("(unreachable)"), "(unreachable)") == 0 || 
			!FileNameInit(FilePathString, true, false))
		{
			PrintToScreen(true, false, L"[System Error] Path initialization error.\n");
			return false;
		}
	}
#endif

//Screen output buffer settings
	_set_errno(0);
	if (setvbuf(stderr, nullptr, _IONBF, 0) != 0)
	{
		PrintError(LOG_LEVEL_TYPE::LEVEL_1, LOG_ERROR_TYPE::SYSTEM, L"Screen output buffer settings error", errno, nullptr, 0);
		return false;
	}

//Winsock initialization
#if defined(PLATFORM_WIN)
	WSAData WSAInitialization;
	memset(&WSAInitialization, 0, sizeof(WSAInitialization));
	if (WSAStartup(
			MAKEWORD(WINSOCK_VERSION_HIGH_BYTE, WINSOCK_VERSION_LOW_BYTE), 
			&WSAInitialization) != 0 || 
	//Winsock 2.2
		LOBYTE(WSAInitialization.wVersion) != WINSOCK_VERSION_LOW_BYTE || 
		HIBYTE(WSAInitialization.wVersion) != WINSOCK_VERSION_HIGH_BYTE)
	{
		PrintError(LOG_LEVEL_TYPE::LEVEL_1, LOG_ERROR_TYPE::NETWORK, L"Winsock initialization error", WSAGetLastError(), nullptr, 0);
		return false;
	}
	else {
		GlobalRunningStatus.IsInitialized_WinSock = true;
	}
#endif

//Read commands.
	auto IsRewriteLogFile = false;
	for (size_t Index = 1U;static_cast<const int>(Index) < argc;++Index)
	{
	//Case insensitive
	#if defined(PLATFORM_WIN)
		std::wstring Commands(argv[Index]), InsensitiveString(argv[Index]);
	#elif (defined(PLATFORM_FREEBSD) || defined(PLATFORM_LINUX) || defined(PLATFORM_MACOS))
		std::string Commands(argv[Index]), InsensitiveString(argv[Index]);
	#endif
		CaseConvert(InsensitiveString, false);

	//Set working directory from commands.
		if (InsensitiveString == COMMAND_LONG_SET_PATH || InsensitiveString == COMMAND_SHORT_SET_PATH)
		{
		//Commands check
			if (static_cast<const int>(Index) + 1 >= argc)
			{
				PrintError(LOG_LEVEL_TYPE::LEVEL_1, LOG_ERROR_TYPE::SYSTEM, L"Commands error", 0, nullptr, 0);
				return false;
			}
			else {
				++Index;
				Commands = argv[Index];

			//Path, file name check and ddd backslash or slash to the end.
			//Path and file name size limit is removed, visit https://msdn.microsoft.com/en-us/library/windows/desktop/aa365247(v=vs.85).aspx to get more details.
			#if defined(PLATFORM_WIN)
				if (Commands.find(L"\\\\") != std::string::npos)
				{
					PrintError(LOG_LEVEL_TYPE::LEVEL_1, LOG_ERROR_TYPE::SYSTEM, L"Commands error", 0, nullptr, 0);
					return false;
				}
				else if (Commands.back() != ASCII_BACKSLASH)
				{
					Commands.append(L"\\");
				}
			#elif (defined(PLATFORM_FREEBSD) || defined(PLATFORM_LINUX) || defined(PLATFORM_MACOS))
				if (Commands.length() >= PATH_MAX || Commands.find("//") != std::string::npos)
				{
					PrintError(LOG_LEVEL_TYPE::LEVEL_1, LOG_ERROR_TYPE::SYSTEM, L"Commands error", 0, nullptr, 0);
					return false;
				}
				else if (Commands.back() != ASCII_SLASH)
				{
					Commands.append("/");
				}
			#endif

			//Mark path and file name.
				if (!FileNameInit(Commands, false, IsRewriteLogFile))
				{
					PrintToScreen(true, false, L"[System Error] Path initialization error.\n");
					return false;
				}
			}
		}
	//Print help messages.
		else if (InsensitiveString == COMMAND_LONG_HELP || InsensitiveString == COMMAND_SHORT_HELP)
		{
			std::lock_guard<std::mutex> ScreenMutex(ScreenLock);
			PrintToScreen(false, false, L"Pcap_DNSProxy ");
			PrintToScreen(false, false, FULL_VERSION);
		#if defined(PLATFORM_FREEBSD)
			PrintToScreen(false, false, L"(FreeBSD)\n");
		#elif defined(PLATFORM_OPENWRT)
			PrintToScreen(false, false, L"(OpenWrt)\n");
		#elif defined(PLATFORM_LINUX)
			PrintToScreen(false, false, L"(Linux)\n");
		#elif defined(PLATFORM_MACOS)
			PrintToScreen(false, false, L"(macOS)\n");
		#elif defined(PLATFORM_WIN)
			PrintToScreen(false, false, L"(Windows)\n");
		#endif
			PrintToScreen(false, false, COPYRIGHT_MESSAGE);
			PrintToScreen(false, false, L"\nUsage: Please visit ReadMe.. files in Documents folder.\n");
			PrintToScreen(false, false, L"   --version:             Print current version on screen.\n");
			PrintToScreen(false, false, L"   --lib-version:         Print current version of libraries on screen.\n");
			PrintToScreen(false, false, L"   --help:                Print help messages on screen.\n");
			PrintToScreen(false, false, L"   --log-file Path+Name:  Set path and name of log file.\n");
			PrintToScreen(false, false, L"   --log-file stderr/out: Set output log to stderr or stdout.\n");
			PrintToScreen(false, false, L"   --flush-dns:           Flush all domain cache in program and system immediately.\n");
			PrintToScreen(false, false, L"   --flush-dns Domain:    Flush cache of Domain in program and all in system immediately.\n");
		#if defined(PLATFORM_WIN)
			PrintToScreen(false, false, L"   --first-setup:         Test local firewall.\n");
		#endif
			PrintToScreen(false, false, L"   --config-path Path:    Set path of configuration file.\n");
			PrintToScreen(false, false, L"   --keypair-generator:   Generate a DNSCurve(DNSCrypt) keypair.\n");
		#if (defined(PLATFORM_FREEBSD) || defined(PLATFORM_LINUX))
			PrintToScreen(false, false, L"   --disable-daemon:      Disable daemon mode.\n");
		#endif

			return false;
		}
	//Set log file location from commands.
		else if (InsensitiveString == COMMAND_LONG_LOG_FILE || InsensitiveString == COMMAND_SHORT_LOG_FILE)
		{
		//Commands check
			if (static_cast<const int>(Index) + 1 >= argc)
			{
				PrintError(LOG_LEVEL_TYPE::LEVEL_1, LOG_ERROR_TYPE::SYSTEM, L"Commands error", 0, nullptr, 0);
				return false;
			}
			else {
				++Index;
				Commands = argv[Index];

			//Path, file name check and ddd backslash or slash to the end.
			//Path and file name size limit is removed, visit https://msdn.microsoft.com/en-us/library/windows/desktop/aa365247(v=vs.85).aspx to get more details.
			#if defined(PLATFORM_WIN)
				if (Commands.find(L"\\\\") != std::string::npos)
				{
					PrintError(LOG_LEVEL_TYPE::LEVEL_1, LOG_ERROR_TYPE::SYSTEM, L"Commands error", 0, nullptr, 0);
					return false;
				}
			#elif (defined(PLATFORM_FREEBSD) || defined(PLATFORM_LINUX) || defined(PLATFORM_MACOS))
				if (Commands.length() >= PATH_MAX + NAME_MAX || Commands.find("//") != std::string::npos)
				{
					PrintError(LOG_LEVEL_TYPE::LEVEL_1, LOG_ERROR_TYPE::SYSTEM, L"Commands error", 0, nullptr, 0);
					return false;
				}
			#endif

			//Mark log path and name.
			#if defined(PLATFORM_WIN)
				*GlobalRunningStatus.Path_ErrorLog = Commands;
			#elif (defined(PLATFORM_FREEBSD) || defined(PLATFORM_LINUX) || defined(PLATFORM_MACOS))
				*GlobalRunningStatus.Path_ErrorLog_MBS = Commands;
				std::wstring StringTemp;
				if (!MBS_To_WCS_String(reinterpret_cast<const uint8_t *>(Commands.c_str()), PATH_MAX + NAME_MAX + NULL_TERMINATE_LENGTH, StringTemp))
				{
					PrintError(LOG_LEVEL_TYPE::LEVEL_1, LOG_ERROR_TYPE::SYSTEM, L"Commands error", 0, nullptr, 0);
					return false;
				}
				else {
					*GlobalRunningStatus.Path_ErrorLog = StringTemp;
				}
			#endif
				IsRewriteLogFile = true;
			}
		}
	//Print current version.
		else if (InsensitiveString == COMMAND_LONG_PRINT_VERSION || InsensitiveString == COMMAND_SHORT_PRINT_VERSION)
		{
			std::lock_guard<std::mutex> ScreenMutex(ScreenLock);
			PrintToScreen(false, false, L"Pcap_DNSProxy ");
			PrintToScreen(false, false, FULL_VERSION);
			PrintToScreen(false, false, L"\n");

			return false;
		}
	//Flush domain Cache from user.
		else if (InsensitiveString == COMMAND_FLUSH_DOMAIN_CACHE)
		{
		//Remove single domain cache.
			if (argc > 2)
			{
			#if defined(PLATFORM_WIN)
				if (wcsnlen_s(argv[2U], FILE_BUFFER_SIZE) == 0 || 
					wcsnlen_s(argv[2U], FILE_BUFFER_SIZE) + wcslen(FLUSH_DOMAIN_MAILSLOT_MESSAGE_ALL) + wcslen(L": ") >= FILE_BUFFER_SIZE)
			#elif (defined(PLATFORM_FREEBSD) || defined(PLATFORM_LINUX) || defined(PLATFORM_MACOS))
				if (strnlen(argv[2U], FILE_BUFFER_SIZE) == 0 || 
					strnlen(argv[2U], FILE_BUFFER_SIZE) + strlen(FLUSH_DOMAIN_PIPE_MESSAGE_ALL) + strlen(": ") >= FILE_BUFFER_SIZE)
			#endif
				{
					PrintToScreen(true, false, L"[Parameter Error] Domain name parameter error.\n");
				}
				else {
				#if defined(PLATFORM_WIN)
					FlushDomainCache_MailslotSender(argv[2U]);
				#elif (defined(PLATFORM_FREEBSD) || defined(PLATFORM_LINUX) || defined(PLATFORM_MACOS))
					FlushDomainCache_PipeSender(reinterpret_cast<const uint8_t *>(argv[2U]));
				#endif
				}
			}
		//Flush all domain cache.
			else {
			#if defined(PLATFORM_WIN)
				FlushDomainCache_MailslotSender(nullptr);
			#elif (defined(PLATFORM_FREEBSD) || defined(PLATFORM_LINUX) || defined(PLATFORM_MACOS))
				FlushDomainCache_PipeSender(nullptr);
			#endif
			}

			return false;
		}
	//DNSCurve(DNSCrypt) KeyPairGenerator
		else if (InsensitiveString == COMMAND_KEYPAIR_GENERATOR)
		{
		//File handle initialization
		#if defined(ENABLE_LIBSODIUM)
			FILE *FileHandle = nullptr;
		#if defined(PLATFORM_WIN)
			_wfopen_s(&FileHandle, DNSCURVE_KEY_PAIR_FILE_NAME, L"w+,ccs=UTF-8");
		#elif (defined(PLATFORM_FREEBSD) || defined(PLATFORM_LINUX) || defined(PLATFORM_MACOS))
			FileHandle = fopen(DNSCURVE_KEY_PAIR_FILE_NAME, "w+");
		#endif

		//Print keypair to file.
			if (FileHandle != nullptr)
			{
			//Initialization and make keypair.
				const auto Buffer = std::make_unique<uint8_t[]>(DNSCRYPT_KEYPAIR_MESSAGE_LEN + MEMORY_RESERVED_BYTES);
				memset(Buffer.get(), 0, DNSCRYPT_KEYPAIR_MESSAGE_LEN + MEMORY_RESERVED_BYTES);
				DNSCURVE_HEAP_BUFFER_TABLE<uint8_t> SecretKey(crypto_box_SECRETKEYBYTES);
				std::array<uint8_t, crypto_box_PUBLICKEYBYTES> PublicKey{};
				size_t InnerIndex = 0;

			//Generate a random keypair.
				if (crypto_box_keypair(
						PublicKey.data(), 
						SecretKey.Buffer) != 0 || 
					sodium_bin2hex(
						reinterpret_cast<char *>(Buffer.get()), 
						DNSCRYPT_KEYPAIR_MESSAGE_LEN, 
						PublicKey.data(), 
						crypto_box_PUBLICKEYBYTES) == nullptr)
				{
					fclose(FileHandle);
					PrintToScreen(true, false, L"[System Error] Create random key pair failed, please try again.\n");

					return false;
				}
				else {
					CaseConvert(Buffer.get(), DNSCRYPT_KEYPAIR_MESSAGE_LEN, true);
					fwprintf_s(FileHandle, L"Client Public Key = ");
				}

			//Write public key.
				for (InnerIndex = 0;InnerIndex < strnlen_s(reinterpret_cast<const char *>(Buffer.get()), DNSCRYPT_KEYPAIR_MESSAGE_LEN);++InnerIndex)
				{
					if (InnerIndex > 0 && InnerIndex % DNSCRYPT_KEYPAIR_INTERVAL == 0 && 
						InnerIndex + 1U < strnlen_s(reinterpret_cast<const char *>(Buffer.get()), DNSCRYPT_KEYPAIR_MESSAGE_LEN))
							fwprintf_s(FileHandle, L":");

					fwprintf_s(FileHandle, L"%c", Buffer.get()[InnerIndex]);
				}

			//Reset buffer.
				memset(Buffer.get(), 0, DNSCRYPT_KEYPAIR_MESSAGE_LEN);
				fwprintf_s(FileHandle, L"\n");

			//Convert secret key.
				if (sodium_bin2hex(
						reinterpret_cast<char *>(Buffer.get()), 
						DNSCRYPT_KEYPAIR_MESSAGE_LEN, 
						SecretKey.Buffer, 
						crypto_box_SECRETKEYBYTES) == nullptr)
				{
					fclose(FileHandle);
					PrintToScreen(true, false, L"[System Error] Create random key pair failed, please try again.\n");

					return false;
				}
				else {
					CaseConvert(Buffer.get(), DNSCRYPT_KEYPAIR_MESSAGE_LEN, true);
					fwprintf_s(FileHandle, L"Client Secret Key = ");
				}

			//Write secret key.
				for (InnerIndex = 0;InnerIndex < strnlen_s(reinterpret_cast<const char *>(Buffer.get()), DNSCRYPT_KEYPAIR_MESSAGE_LEN);++InnerIndex)
				{
					if (InnerIndex > 0 && InnerIndex % DNSCRYPT_KEYPAIR_INTERVAL == 0 && 
						InnerIndex + 1U < strnlen_s(reinterpret_cast<const char *>(Buffer.get()), DNSCRYPT_KEYPAIR_MESSAGE_LEN))
							fwprintf_s(FileHandle, L":");

					fwprintf_s(FileHandle, L"%c", Buffer.get()[InnerIndex]);
				}
				fwprintf_s(FileHandle, L"\n");

			//Close file.
				fclose(FileHandle);
				PrintToScreen(true, false, L"[Notice] DNSCurve(DNSCrypt) keypair generation is successful.\n");
			}
			else {
				PrintToScreen(true, false, L"[System Error] Cannot create target file(KeyPair.txt).\n");
			}
		#else
			PrintToScreen(true, false, L"[Notice] LibSodium is disabled.\n");
		#endif

			return false;
		}
	//Print library version.
		else if (InsensitiveString == COMMAND_LIB_VERSION)
		{
		//Initialization
			std::wstring LibVersion;
			char *VersionString = nullptr;

		//LibEvent version
			VersionString = const_cast<char *>(event_get_version());
			if (VersionString != nullptr && MBS_To_WCS_String(reinterpret_cast<const uint8_t *>(VersionString), strlen(VersionString), LibVersion))
				PrintToScreen(true, false, L"LibEvent version %ls\n", LibVersion.c_str());
			else 
				PrintToScreen(true, false, L"[System Error] Convert multiple byte or wide char string error.\n");

		//LibSodium version
		#if defined(ENABLE_LIBSODIUM)
			VersionString = const_cast<char *>(sodium_version_string());
			if (VersionString != nullptr && MBS_To_WCS_String(reinterpret_cast<const uint8_t *>(VersionString), strlen(VersionString), LibVersion))
				PrintToScreen(true, false, L"LibSodium version %ls\n", LibVersion.c_str());
			else 
				PrintToScreen(true, false, L"[System Error] Convert multiple byte or wide char string error.\n");
		#endif

		//WinPcap or LibPcap version
		#if defined(ENABLE_PCAP)
			VersionString = const_cast<char *>(pcap_lib_version());
			if (VersionString != nullptr && MBS_To_WCS_String(reinterpret_cast<const uint8_t *>(VersionString), strlen(VersionString), LibVersion))
				PrintToScreen(true, false, L"%ls\n", LibVersion.c_str());
			else 
				PrintToScreen(true, false, L"[System Error] Convert multiple byte or wide char string error.\n");
		#endif

		//OpenSSL version
		#if defined(ENABLE_TLS)
		#if (defined(PLATFORM_FREEBSD) || defined(PLATFORM_LINUX) || defined(PLATFORM_MACOS))
		#if OPENSSL_VERSION_NUMBER >= OPENSSL_VERSION_1_1_0 //OpenSSL version 1.1.0 and above
			VersionString = const_cast<char *>(OpenSSL_version(OPENSSL_VERSION));
		#else //OpenSSL version below 1.1.0
			VersionString = const_cast<char *>(SSLeay_version(SSLEAY_VERSION));
		#endif
			if (VersionString != nullptr && MBS_To_WCS_String(reinterpret_cast<const uint8_t *>(VersionString), strnlen(VersionString, OPENSSL_STATIC_BUFFER_SIZE), LibVersion))
				PrintToScreen(true, false, L"%ls\n", LibVersion.c_str());
			else 
				PrintToScreen(true, false, L"[System Error] Convert multiple byte or wide char string error.\n");
		#endif
		#endif

			return false;
		}
	#if (defined(PLATFORM_FREEBSD) || defined(PLATFORM_LINUX))
	//Set system daemon.
		else if (InsensitiveString == COMMAND_DISABLE_DAEMON)
		{
			GlobalRunningStatus.IsDaemon = false;
		}
	#elif defined(PLATFORM_WIN)
	//Firewall Test in first start.
		else if (InsensitiveString == COMMAND_FIREWALL_TEST)
		{
			ssize_t ErrorCode = 0;
			if (!FirewallTest(AF_INET6, ErrorCode) && !FirewallTest(AF_INET, ErrorCode))
				PrintError(LOG_LEVEL_TYPE::LEVEL_2, LOG_ERROR_TYPE::NETWORK, L"Firewall test error", ErrorCode, nullptr, 0);
			else 
				PrintToScreen(true, false, L"[Notice] Firewall test is successful.\n");

			return false;
		}
	#endif
	}

//Set system daemon.
#if (defined(PLATFORM_FREEBSD) || defined(PLATFORM_LINUX))
	if (GlobalRunningStatus.IsDaemon && daemon(0, 0) == RETURN_ERROR)
	{
		PrintError(LOG_LEVEL_TYPE::LEVEL_1, LOG_ERROR_TYPE::SYSTEM, L"Set system daemon error", 0, nullptr, 0);
		return false;
	}
#endif

	return true;
}

//Get path of program from the main function parameter and Winsock initialization
#if defined(PLATFORM_WIN)
bool FileNameInit(
	const std::wstring &OriginalPath, 
	const bool IsStartupLoad, 
	const bool IsRewriteLogFile)
#elif (defined(PLATFORM_FREEBSD) || defined(PLATFORM_LINUX) || defined(PLATFORM_MACOS))
bool FileNameInit(
	const std::string &OriginalPath, 
	const bool IsStartupLoad, 
	const bool IsRewriteLogFile)
#endif
{
#if defined(PLATFORM_WIN)
//Path process
//The path is location path with backslash not including module name at the end of this process, like "System:\\xxx\\".
//The path is full path name including module name from file name initialization.
//The path is location path not including module name from set path command.
	GlobalRunningStatus.Path_Global->clear();
	GlobalRunningStatus.Path_Global->push_back(OriginalPath);
	if (GlobalRunningStatus.Path_Global->front().rfind(L"\\") == std::wstring::npos)
		return false;
	else if (GlobalRunningStatus.Path_Global->front().rfind(L"\\") + 1U < GlobalRunningStatus.Path_Global->front().length())
		GlobalRunningStatus.Path_Global->front().erase(GlobalRunningStatus.Path_Global->front().rfind(L"\\") + 1U);
	for (size_t Index = 0;Index < GlobalRunningStatus.Path_Global->front().length();++Index)
	{
		if (GlobalRunningStatus.Path_Global->front().at(Index) == (L'\\'))
		{
			GlobalRunningStatus.Path_Global->front().insert(Index, L"\\");
			++Index;
		}
	}
#elif (defined(PLATFORM_FREEBSD) || defined(PLATFORM_LINUX) || defined(PLATFORM_MACOS))
//Path process
//The path is location path with slash not including module name at the end of this process, like "/xxx/".
	GlobalRunningStatus.Path_Global_MBS->clear();
	GlobalRunningStatus.Path_Global_MBS->push_back(OriginalPath);
	std::wstring StringTemp;
	if (!MBS_To_WCS_String(reinterpret_cast<const uint8_t *>(OriginalPath.c_str()), PATH_MAX + NULL_TERMINATE_LENGTH, StringTemp))
		return false;
	GlobalRunningStatus.Path_Global->clear();
	GlobalRunningStatus.Path_Global->push_back(StringTemp);
#endif

//Get path of log file.
	if (IsStartupLoad)
	{
	#if defined(PLATFORM_OPENWRT)
	//Set log file to stderr.
		*GlobalRunningStatus.Path_ErrorLog = L"stderr";
		*GlobalRunningStatus.Path_ErrorLog_MBS = "stderr";
	#else
	//Set log file to program location.
		*GlobalRunningStatus.Path_ErrorLog = GlobalRunningStatus.Path_Global->front();
		GlobalRunningStatus.Path_ErrorLog->append(ERROR_LOG_FILE_NAME_WCS);
	#if (defined(PLATFORM_FREEBSD) || defined(PLATFORM_LINUX) || defined(PLATFORM_MACOS))
		*GlobalRunningStatus.Path_ErrorLog_MBS = GlobalRunningStatus.Path_Global_MBS->front();
		GlobalRunningStatus.Path_ErrorLog_MBS->append(ERROR_LOG_FILE_NAME_MBS);
	#endif
	#endif
	}
	else if (!IsRewriteLogFile)
	{
	//Set log file to program location.
		*GlobalRunningStatus.Path_ErrorLog = GlobalRunningStatus.Path_Global->front();
		GlobalRunningStatus.Path_ErrorLog->append(ERROR_LOG_FILE_NAME_WCS);
	#if (defined(PLATFORM_FREEBSD) || defined(PLATFORM_LINUX) || defined(PLATFORM_MACOS))
		*GlobalRunningStatus.Path_ErrorLog_MBS = GlobalRunningStatus.Path_Global_MBS->front();
		GlobalRunningStatus.Path_ErrorLog_MBS->append(ERROR_LOG_FILE_NAME_MBS);
	#endif
	}

//Mark startup time.
#if defined(PLATFORM_WIN)
	GlobalRunningStatus.IsConsole = true;
#endif
	GlobalRunningStatus.StartupTime = time(nullptr);
	if (GlobalRunningStatus.StartupTime <= 0)
		return false;

	return true;
}
