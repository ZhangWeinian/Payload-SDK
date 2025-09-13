#ifndef APPLICATION_H
	#define APPLICATION_H

	#include <fstream>
	#include <iostream>

	#include "dji_core.h"
	#include "dji_typedef.h"

	#ifdef __cplusplus
extern "C"
{
	#endif

	using namespace std;

	class Application
	{
	public:
		Application(int argc, char** argv);
		~Application();

	private:
		static void			   DjiUser_SetupEnvironment(int argc, char** argv);
		static void			   DjiUser_ApplicationStart();
		static T_DjiReturnCode DjiUser_PrintConsole(const uint8_t* data, uint16_t dataLen);
		static T_DjiReturnCode DjiUser_LocalWrite(const uint8_t* data, uint16_t dataLen);
		static T_DjiReturnCode DjiUser_FillInUserInfo(T_DjiUserInfo* userInfo);
		static T_DjiReturnCode DjiUser_LocalWriteFsInit(const char* path);
	};

	#ifdef __cplusplus
}
	#endif

#endif // APPLICATION_H

