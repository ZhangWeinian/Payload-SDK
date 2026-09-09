// cy_psdk/manager/catalog/client/CatalogModels.h
//
// 业务模型值类型 (由 swarm-catalog-client-java model 包转译, 字段语义一一对应)。

#pragma once

#include <chrono>
#include <map>
#include <string>
#include <vector>

#include "define.h"
#include "manager/catalog/client/CatalogTypes.h"

namespace plane::catalog
{
	// 暴露端口声明。url 为可选完整访问地址 (如 RTSP 推流 rtsp://user:pass@ip:port/path,
	// 含凭证与路径); 为空时服务端按 protocol/port 自行拼接。
	// ip/path 为端点业务访问地址与路径; ip 为空时服务端用实例来源 IP。
	struct ExposedPort
	{
		_STD string name {};
		_STD string protocol {};
		int			port { 0 };
		_STD string url {}; // 可选完整访问地址
		_STD string ip {};	// 端点业务访问地址 (为空时服务端用实例来源 IP)
		_STD string path {};
	};

	// 允许服务目录查询的日志路径。path 必须为绝对路径。
	struct LogPath
	{
		_STD string name {};
		_STD string path {};
		bool		recursive { true };
		_STD string file_pattern {};
	};

	// 当前业务服务的期望注册信息
	struct ServiceRegistration
	{
		_STD string namespace_name {}; // 为空时归一化为 public
		_STD string group_name {};	   // 为空时归一化为 DEFAULT_GROUP
		_STD string service_id {};	   // 稳定且唯一的技术标识
		_STD string service_name {};   // 供页面和接口展示的服务名称
		_STD string version {};		   // 必填, 去除首尾空白后最长 128 字符
		_STD vector<ExposedPort> exposed_ports {};
		_STD vector<LogPath> log_paths {};
		_STD string			 metadata_json {}; // 对象 JSON 或空
	};

	// 单个业务组件健康状态
	struct ServiceComponentStatus
	{
		_STD string name {};
		_STD string status {}; // UP / DEGRADED / DOWN / UNKNOWN
		_STD string code {};
		_STD string message {};
		_STD map<_STD string, _STD string> details {};
	};

	// 业务健康状态。运行时只保留最新一份。
	struct ServiceStatus
	{
		bool		healthy { true };
		_STD string overall_status {}; // 为空时根据 healthy 生成 UP/DOWN
		_STD string code {};
		_STD string message {};
		_STD map<_STD string, _STD string> details {};
		_STD vector<ServiceComponentStatus> components {};
		_STD_CHRONO system_clock::time_point occurred_at {};
	};

	// 服务查询条件。为空字段继承注册作用域。
	struct ServiceQuery
	{
		_STD string namespace_name {};
		_STD string group_name {};
		_STD string service_id {};
		_STD string service_name {}; // service_id 为空时作为技术标识
	};

	// 服务三元组标识 (namespace / group / serviceId)
	struct ServiceKey
	{
		_STD string namespace_name {};
		_STD string group_name {};
		_STD string service_id {};
	};

	// 已注册服务 (不含实例)
	struct RegisteredService
	{
		ServiceKey	key {};
		_STD string service_name {};
		_STD string source {};
	};

	// 服务摘要: 服务信息 + 实例统计 + 运行状态
	struct ServiceSummary
	{
		RegisteredService service {};
		long long		  total_instances { 0 };
		long long		  healthy_instances { 0 };
		_STD string		  runtime_status {};
	};

	// 服务分页查询结果 (GET /api/registry/services)
	struct ServicePage
	{
		_STD vector<ServiceSummary> items {};
		long long					total_elements { 0 };
		int							page { 1 };
		int							page_size { 0 };
	};

	// 服务实例端点。primary=服务端选定的主实例 (旧服务端缺省 false);
	// enabled=是否启用 (缺省 true)。消费方应优先选择 primary=true 的实例。
	struct ServiceEndpoint
	{
		_STD string instance_id {};
		_STD string address {}; // 实例身份 IP (业务访问地址应从 exposed_ports 中选择)
		_STD string version {};
		_STD vector<ExposedPort> exposed_ports {};
		_STD string				 metadata_json {};
		bool					 primary { false };
		bool					 enabled { true };
	};

	// 服务解析结果: 全部健康且启用的实例
	struct ResolvedService
	{
		_STD vector<ServiceEndpoint> endpoints {};
	};

	// 配置三元组标识 (namespace / group / dataId)
	struct ConfigKey
	{
		_STD string namespace_name {};
		_STD string group_name {};
		_STD string data_id {};

		bool		operator<(const ConfigKey& other) const noexcept
		{
			if (namespace_name != other.namespace_name)
			{
				return namespace_name < other.namespace_name;
			}
			if (group_name != other.group_name)
			{
				return group_name < other.group_name;
			}
			return data_id < other.data_id;
		}
	};

	// 批量配置查询。一次请求中的配置必须属于同一个 namespace/group。
	struct ConfigQuery
	{
		_STD string namespace_name {};
		_STD string group_name {};
		_STD vector<_STD string> data_ids {};
	};

	// 通用配置上传请求。SDK 按 key 原样保存完整 content, 不解析业务配置内容。
	struct ConfigUploadRequest
	{
		ConfigKey	key {};
		_STD string content {};
		_STD string format {};
	};

	// 配置文档。from_cache=true 表示最后一次有效缓存, 不能当成目录最新数据。
	struct ConfigDocument
	{
		ConfigKey	key {};
		_STD string content {};
		_STD string format {};
		_STD string version {};
		_STD string updated_at {};
		bool		from_cache { false };
	};

	// 配置变更事件。只在首次获取或内容真实变化时投递。
	struct ConfigChangeEvent
	{
		ConfigDocument previous {};
		ConfigDocument current {};
		bool		   initial_load { false };
	};

	// Catalog 服务端主动广播的公告信息 (SWMP command=0x82)
	struct CatalogAnnouncement
	{
		_STD string node_id {};
		_STD string ip {}; // 为空时由接收方回退为 UDP 源地址
		_STD string node_name {};
		_STD string instance_id {};
		int			http_port { 0 };
	};
} // namespace plane::catalog
