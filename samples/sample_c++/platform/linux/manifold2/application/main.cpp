// manifold2/application/main.cpp

#include "CLI/CLI.hpp"

#include "dji_demo.h"
#include "my_dji.h"

int main(int argc, char** argv)
{
	CLI::App app { "DJI PSDK 应用, 集成自定义 MQTT 服务" };
	app.set_help_all_flag("--help-all", "显示所有帮助信息");

	bool runDjiInteractiveMode { false };
	app.add_flag("-d,--dji-interactive", runDjiInteractiveMode, "运行 DJI 官方的交互式示例菜单");

	CLI11_PARSE(app, argc, argv);

	if (runDjiInteractiveMode)
	{
		plane::dji_demo::runDjiApplication(argc, argv);
	}
	else
	{
		plane::my_dji::runMyApplication(argc, argv);
	}

	return 0;
}
