// cy_psdk/manager/binding/DeviceBinder.cpp
//
// 设备绑定实现 (对齐 msdk NicknameSync / SwarmServiceUseCase.bindDeviceBySerialNumber)。
// 传输层复用自研 catalog 的 CppHttpTransport (cpp-httplib, 已在 vcpkg)。

#include "manager/binding/DeviceBinder.h"

#include <fmt/format.h>
#include <nlohmann/json.hpp>
#include <chrono>
#include <string>
#include <thread>

#include "manager/catalog/CatalogManager.h"
#include "manager/catalog/client/internal/CppHttpTransport.h"
#include "manager/plane_state/PlaneStateStore.h"
#include "utils/log_util/Logger.h"
#include "utils/network_util/GetLocalIPV4.h"

#include "define.h"

namespace plane::manager
{
	namespace
	{
		using Ms = _STD_CHRONO milliseconds;

		// 绑定重试/轮询周期
		constexpr Ms kRetryInterval { 3000 };

		// 绑定请求超时: 后台绑定可能触发自动建机, 2~3s, 给足 5s
		constexpr Ms kBindTimeout { 5000 };

		// planeTypeCode = "DJI" + 相机类型名 (临时兼容: M4E 一律按 M4T 上报)。
		// 当前 PSDK 相机枚举尚未扩展 (camera_type 仅占位), 先固定 DJIM4T;
		// PSDK 相机类型就绪后由 camera_type 映射。
		_NODISCARD _STD string resolvePlaneTypeCode(const plane::domain::PlaneStateDataClass& snapshot) noexcept
		{
			(void)snapshot;
			return "DJIM4T";
		}

		// imageUrl: 完整 RTSP 推流地址 (rtsp://user:pass@ip:port/base)。
		// 本机 IP 不可得时不伪造 (返回空)。
		_NODISCARD _STD string buildRtspImageUrl(const plane::domain::PlaneStateDataClass& snapshot) noexcept
		{
			const auto local_ip { plane::utils::getLocalIPV4() };
			if (!local_ip.has_value() || local_ip->empty())
			{
				LOG_DEBUG("本机 IP 未就绪, imageUrl 留空");
				return {};
			}

			const _STD string user { snapshot.rtsp_push_video_user_name.empty() ? "admin" : snapshot.rtsp_push_video_user_name };
			const _STD string pass { snapshot.rtsp_push_video_password.empty() ? "1" : snapshot.rtsp_push_video_password };
			const int		  port { snapshot.rtsp_push_video_server_port > 0 ? snapshot.rtsp_push_video_server_port : 8554 };
			const _STD string base { snapshot.rtsp_push_video_base_url.empty() ? "streaming/live/1" : snapshot.rtsp_push_video_base_url };
			return _FMT		  format("rtsp://{}:{}@{}:{}/{}", user, pass, *local_ip, port, base);
		}

		// 后台明确否定当前绑定 (业务拒绝 / 响应缺 planeId) 时回退本机绑定态:
		// 仅当本机曾处于绑定态才清理域状态并恢复目录默认服务名;
		// 传输层失败 (后台不可达) 不应走此路径, 避免已绑定状态闪断
		void applyUnboundIfNeeded(const plane::domain::PlaneStateDataClass& snapshot, _STD string& bound_sn) noexcept
		{
			const bool ever_bound { !bound_sn.empty() || snapshot.device_binding || !snapshot.internal_plane_id.empty() ||
									snapshot.device_nickname != "未绑定" };
			if (!ever_bound)
			{
				return;
			}
			bound_sn.clear();
			plane::domain::PlaneStateStore::getInstance().update(
				[](plane::domain::PlaneStateDataClass& state)
				{
					state.device_nickname	= "未绑定";
					state.internal_plane_id = "";
					state.device_binding	= false;
				}
			);
			// 恢复目录注册默认名 (DJI-PSDK-<SN>)
			plane::manager::CatalogManager::getInstance().updateServiceName("");
			LOG_WARN("当前绑定已被后台否定, 已回退为未绑定状态");
		}
	} // namespace

	DeviceBinder& DeviceBinder::getInstance(void) noexcept
	{
		static DeviceBinder instance {};
		return instance;
	}

	DeviceBinder::~DeviceBinder(void) noexcept
	{
		this->stop();
	}

	void DeviceBinder::start(void) noexcept
	{
		if (this->started_.exchange(true, _STD memory_order_acq_rel))
		{
			return;
		}
		this->running_.store(true, _STD memory_order_release);
		this->thread_ = _STD thread(&DeviceBinder::runLoop, this);
		LOG_INFO("设备绑定流程已启动 (等待目录就绪与序列号)");
	}

	void DeviceBinder::stop(void) noexcept
	{
		if (!this->started_.exchange(false, _STD memory_order_acq_rel))
		{
			this->running_.store(false, _STD memory_order_release);
			return;
		}
		this->running_.store(false, _STD memory_order_release);
		if (this->thread_.joinable())
		{
			this->thread_.join();
		}
		LOG_INFO("设备绑定流程已停止");
	}

	void DeviceBinder::runLoop(void) noexcept
	{
		bool		need_bind { true }; // 首次必绑
		bool		last_ready { false };
		_STD string bound_sn {};

		while (this->running_.load(_STD memory_order_acquire))
		{
			const auto		  snapshot { plane::domain::PlaneStateStore::getInstance().snapshot() };
			const _STD string sn { snapshot.serial_number };
			const bool		  ready { plane::manager::CatalogManager::getInstance().isCatalogReady() };

			// 目录未就绪: 等待 (失联后恢复视为新的绑定机会)
			if (!ready)
			{
				last_ready = false;
				_STD this_thread::sleep_for(kRetryInterval);
				continue;
			}
			if (!last_ready)
			{
				last_ready = true;
				need_bind  = true;
				LOG_INFO("目录已就绪, 触发设备绑定评估");
			}
			// 序列号变化 (如占位 SN -> 真机 SN) 视为新的绑定机会
			if (sn != bound_sn)
			{
				need_bind = true;
			}
			if (!need_bind)
			{
				_STD this_thread::sleep_for(kRetryInterval);
				continue;
			}

			// 序列号未就绪: 不发起 (不兜底)
			if (sn.empty())
			{
				LOG_DEBUG("设备序列号未就绪, 等待后再试");
				_STD this_thread::sleep_for(kRetryInterval);
				continue;
			}

			// 经 Catalog 定位后台"业务"服务
			const _STD string base { plane::manager::CatalogManager::getInstance().resolveServiceBaseUrl("swarm.service.base", "http") };
			if (base.empty())
			{
				LOG_WARN("未定位到后台业务服务 (swarm.service.base), 稍后重试绑定");
				_STD this_thread::sleep_for(kRetryInterval);
				continue;
			}

			// 构造绑定请求 (对齐 msdk: planeTypeCode/serialNumber/imageUrl)
			const _STD string type_code { resolvePlaneTypeCode(snapshot) };
			const _STD string image_url { buildRtspImageUrl(snapshot) };
			LOG_INFO("开始设备绑定: SN={}, planeTypeCode={}, imageUrl={}", sn, type_code, image_url.empty() ? "(空)" : image_url);

			_NLOHMANN_JSON json payload;
			payload["planeTypeCode"] = type_code;
			payload["serialNumber"]	 = sn;
			payload["imageUrl"]		 = image_url;

			plane::catalog::internal::CppHttpTransport http {};
			http.setTimeout(kBindTimeout);
			const auto response { http.post(base + "/control/bindDeviceBySerialNumber", payload.dump()) };

			if (!response.transport_ok || response.status / 100 != 2)
			{
				// 后台不可达/服务异常 ≠ 绑定被否定: 保留已生效绑定, 仅保持重试, 避免昵称/目录名闪断
				LOG_WARN(
					"设备绑定请求失败 (后台不可达?), 保留当前绑定并重试: transport_ok={}, status={}, error={}",
					response.transport_ok,
					response.status,
					response.transport_error
				);
				need_bind = true;
				_STD this_thread::sleep_for(kRetryInterval);
				continue;
			}

			// 解析响应。后台契约: {code,message,data{planeId,planeName,planeCode,typeId}}。
			// 结构不符 (含顶层数组等) 时记录原始响应以便诊断。
			int			code { -1 };
			_STD string message {};
			_STD string plane_id {};
			_STD string plane_name {};
			_STD string raw_body { response.body };
			if (raw_body.size() > 1024)
			{
				raw_body.resize(1024);
				raw_body += "...(截断)";
			}
			try
			{
				const _NLOHMANN_JSON json parsed = _NLOHMANN_JSON json::parse(response.body);
				// 容错: 正常契约是对象; 若被网关包装成单元素对象数组则取其首元素
				_NLOHMANN_JSON json root = parsed;
				if (root.is_array() && !root.empty() && root[0].is_object())
				{
					root = root[0];
				}
				if (!root.is_object())
				{
					LOG_ERROR(
						"绑定响应结构异常 (顶层类型={}, 元素数={}), 原始响应: {}",
						parsed.type_name(),
						parsed.is_array() ? parsed.size() : 0,
						raw_body
					);
				}
				else
				{
					code						   = root.value("code", -1);
					message						   = root.value("message", _STD string {});
					const _NLOHMANN_JSON json data = root.contains("data") ? root["data"] : _NLOHMANN_JSON json::object();
					plane_id					   = data.is_object() ? data.value("planeId", _STD string {}) : _STD string {};
					plane_name					   = data.is_object() ? data.value("planeName", _STD string {}) : _STD string {};
				}
			}
			catch (const _STD exception& ex)
			{
				LOG_ERROR("绑定响应 JSON 解析失败: {}; 原始响应: {}", ex.what(), raw_body);
			}

			// 绑定成立判据: 业务码 0 且后台分配了 planeId
			if (code == 0 && !plane_id.empty())
			{
				plane::domain::PlaneStateStore::getInstance().update(
					[&plane_id, &plane_name](plane::domain::PlaneStateDataClass& state)
					{
						state.device_nickname	= plane_name.empty() ? _STD string { "未绑定" } : plane_name;
						state.internal_plane_id = plane_id;
						state.device_binding	= true;
					}
				);
				if (!plane_name.empty())
				{
					// 昵称联动目录注册名: DJI-PSDK-<昵称>
					plane::manager::CatalogManager::getInstance().updateServiceName("DJI-PSDK-" + plane_name);
				}
				bound_sn  = sn;
				need_bind = false;
				this->bound_.store(true, _STD memory_order_release);
				LOG_INFO("设备绑定成功: planeId='{}', planeName='{}'", plane_id, plane_name);
			}
			else
			{
				if (code == 0)
				{
					LOG_WARN("绑定响应缺少有效 planeId (planeName='{}'), 按失败处理并重试", plane_name);
				}
				else
				{
					LOG_WARN("设备绑定被后台拒绝: code={}, message={}", code, message);
				}
				// 仅业务性否定 (后台明确未接受本 SN) 才回退已生效绑定并恢复目录默认名
				applyUnboundIfNeeded(snapshot, bound_sn);
				this->bound_.store(false, _STD memory_order_release);
			}
			_STD this_thread::sleep_for(kRetryInterval);
		}
	}
} // namespace plane::manager
