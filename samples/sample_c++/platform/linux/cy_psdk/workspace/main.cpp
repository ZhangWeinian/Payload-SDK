// cy_psdk/workspace/main.cpp

#include "dji_demo.h"
#include "my_dji.h"

#include <CLI/CLI.hpp>

int main(int argc, char* argv[])
{
	_CLI App app { "DJI PSDK 应用, 集成自定义 MQTT 服务" };
	app.set_help_all_flag("--help-all", "显示所有帮助信息");

	bool run_dji_interactive_mode { false };
	app.add_flag("-d,--dji-interactive", run_dji_interactive_mode, "运行 DJI 官方的交互式示例");

	CLI11_PARSE(app, argc, argv);

	if (run_dji_interactive_mode)
	{
		plane::dji_demo::runDjiApplication(argc, argv);
	}
	else
	{
		plane::my_dji::runMyApplication(argc, argv);
	}

	return 0;
}
