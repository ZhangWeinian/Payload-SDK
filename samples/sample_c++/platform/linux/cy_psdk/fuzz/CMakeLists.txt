# cy_psdk 模糊测试 (libFuzzer, 由 PSDK_BUILD_FUZZERS 控制, 仅 x86 Clang)
#
# 用法:
#   cmake --preset fuzz
#   cmake --build --preset fuzz
#   build/x86_64-linux/fuzz/bin/fuzz_probe_packet fuzz/corpus/probe_packet -runs=100000
#   build/x86_64-linux/fuzz/bin/fuzz_json_codec  fuzz/corpus/json_codec  -runs=100000
#
# 生产代码经 ASan/UBSan 插桩 (来自 fuzz 预设); 模糊器目标额外链接 -fsanitize=fuzzer
# (含驱动 main 与 SanitizerCoverage 覆盖率反馈)。

find_package (fmt CONFIG REQUIRED)
find_package (spdlog CONFIG REQUIRED)
find_package (nlohmann_json CONFIG REQUIRED)

set (FUZZ_PROD_SOURCES
	../src/manager/catalog/client/internal/codec/JsonCodec.cpp
	../src/manager/catalog/client/internal/codec/ProbePacketCodec.cpp
)

function (cy_add_fuzzer TARGET FUZZ_SOURCE)
	add_executable (${TARGET} ${FUZZ_SOURCE} ${FUZZ_PROD_SOURCES})
	target_include_directories (
		${TARGET} PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/../src"
						  "${CMAKE_CURRENT_SOURCE_DIR}/../src/workspace"
	)
	target_compile_options (${TARGET} PRIVATE -fsanitize=fuzzer-no-link)
	target_link_options (${TARGET} PRIVATE -fsanitize=fuzzer)
	target_link_libraries (
		${TARGET}
		PRIVATE fmt::fmt
				spdlog::spdlog
				nlohmann_json::nlohmann_json
				Threads::Threads
	)
	set_target_properties (${TARGET} PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${MAIN_OUTPUT_PATH}")
endfunction ()

cy_add_fuzzer (fuzz_probe_packet fuzz_probe_packet.cpp)
cy_add_fuzzer (fuzz_json_codec fuzz_json_codec.cpp)
