// cy_psdk/utils/integrity/IntegrityCheck.h

#pragma once

namespace plane::utils
{
	// 部署完整性自检: 校验 cy_psdk 及其同目录 libs/ 下所有文件的 SHA256,
	// 哈希值来自构建时由 bash/gen_checksum.sh 生成的 "<文件>.sha256"。
	// 不依赖目标板上的任何外部工具 (sha256sum/awk/coreutils 等)。
	//
	// argv0: 主程序的 argv[0]。注意: 经打包 loader 启动时 /proc/self/exe 指向
	//        解释器本身而非 cy_psdk, 因此必须以 argv[0] 定位交付目录。
	// 返回 false 表示校验失败, 调用方应拒绝继续运行。
	// 可通过环境变量 CY_PSDK_SKIP_INTEGRITY=1 临时跳过 (仅用于开发调试)。
	bool verifyDeploymentIntegrity(const char* argv0);
} // namespace plane::utils
