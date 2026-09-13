// cy_psdk/utils/json_converter/BuildAndParse.cpp

#include "utils/json_converter/BuildAndParse.h"

#include "manager/mqtt/handler/MessageHandler.h"
#include "utils/device_identity/DeviceIdentity.h"
#include "utils/log_util/Logger.h"

#include <fmt/chrono.h>
#include <fmt/format.h>

#include <chrono>

namespace plane::utils
{
    using n_json = ::nlohmann::json;

    namespace
    {
        static int64_t getCurrentTimestampMs(void) noexcept
        {
            return ::std::chrono::duration_cast<::std::chrono::milliseconds>(::std::chrono::system_clock::now().time_since_epoch()).count();
        }

        // 时间戳 -> 本地时间字符串 (与 Logger 一致, 统一走 fmt chrono; 截断到秒与旧 put_time 行为一致)
        static ::std::string formatTimestamp(int64_t ms) noexcept
        {
            const auto tp { ::std::chrono::system_clock::time_point { ::std::chrono::milliseconds { ms } } };
            return ::fmt::format("{:%Y-%m-%d %H:%M:%S}", ::std::chrono::time_point_cast<::std::chrono::seconds>(tp));
        }
    } // namespace

    ::std::string JsonConverter::buildStatusReportJson(const plane::protocol::StatusPayload& payload) noexcept
    {
        const ::std::string                                             plane_code { plane::utils::DeviceIdentity::resolveDeviceCode() };
        auto                                                            now { getCurrentTimestampMs() };
        plane::protocol::NetworkMessage<plane::protocol::StatusPayload> msg { .ZBID = plane_code,
                                                                              .XXID = ::fmt::format("SBZT-{}-{}", plane_code, now),
                                                                              .XXLX = "SBZT",
                                                                              .SJC  = now,
                                                                              .SBSJ = formatTimestamp(now),
                                                                              .XXXX = payload };
        n_json                                                          j = msg;
        return j.dump();
    }

    ::std::string JsonConverter::buildMissionInfoJson(const plane::protocol::MissionInfoPayload& payload) noexcept
    {
        const ::std::string                                                  plane_code { plane::utils::DeviceIdentity::resolveDeviceCode() };
        auto                                                                 now { getCurrentTimestampMs() };
        plane::protocol::NetworkMessage<plane::protocol::MissionInfoPayload> msg { .ZBID = plane_code,
                                                                                   .XXID = ::fmt::format("GDXX-{}-{}", plane_code, now),
                                                                                   .XXLX = "GDXX",
                                                                                   .SJC  = now,
                                                                                   .SBSJ = formatTimestamp(now),
                                                                                   .XXXX = payload };
        n_json                                                               j = msg;
        return j.dump();
    }

    ::std::string JsonConverter::buildHealthStatusJson(const plane::protocol::HealthStatusPayload& payload) noexcept
    {
        const ::std::string                                                   plane_code { plane::utils::DeviceIdentity::resolveDeviceCode() };
        auto                                                                  now { getCurrentTimestampMs() };
        plane::protocol::NetworkMessage<plane::protocol::HealthStatusPayload> msg { .ZBID = plane_code,
                                                                                    .XXID = ::fmt::format("JKGL-{}-{}", plane_code, now),
                                                                                    .XXLX = "JKGL",
                                                                                    .SJC  = now,
                                                                                    .SBSJ = formatTimestamp(now),
                                                                                    .XXXX = payload };
        n_json                                                                j = msg;
        return j.dump();
    }

    ::std::string JsonConverter::buildMissionProgressJson(const plane::protocol::MissionProgressPayload& payload) noexcept
    {
        const ::std::string plane_code { plane::utils::DeviceIdentity::resolveDeviceCode() };
        auto                now { getCurrentTimestampMs() };
        plane::protocol::NetworkMessage<plane::protocol::MissionProgressPayload> msg { .ZBID = plane_code,
                                                                                       .XXID = ::fmt::format("RWJD-{}-{}", plane_code, now),
                                                                                       .XXLX = "RWJD",
                                                                                       .SJC  = now,
                                                                                       .SBSJ = formatTimestamp(now),
                                                                                       .XXXX = payload };
        n_json                                                                   j = msg;
        return j.dump();
    }

    void JsonConverter::parseAndRouteMessage(::std::string_view topic, ::std::string_view jsonString) noexcept
    {
        try
        {
            n_json j = n_json::parse(jsonString);
            if (j.contains("ZBID"))
            {
                ::std::string target_plane_code { j.at("ZBID").get<::std::string>() };
                ::std::string local_plane_code { plane::utils::DeviceIdentity::resolveDeviceCode() };
                if (target_plane_code != local_plane_code)
                {
                    LOG_DEBUG("收到发往其他设备 ({}) 的消息, 本机 ({}) 已忽略", target_plane_code, local_plane_code);
                    return;
                }
            }
            else
            {
                LOG_WARN("收到的消息缺少 ZBID 字段, 无法验证目标设备");
                return;
            }

            ::std::string message_type { j.at("XXLX").get<::std::string>() };
            n_json        payload_json = j.value("XXXX", n_json {});
            plane::manager::MqttMessageHandler::getInstance().routeMessage(topic, message_type, payload_json);
        }
        catch (const n_json::exception& e)
        {
            LOG_ERROR("处理 MQTT 消息时发生 JSON 错误 (主题: '{}'): {}. Raw JSON string:\n{}", topic, e.what(), jsonString);
        }
        catch (const ::std::exception& e)
        {
            LOG_ERROR("处理 MQTT 消息时发生未知异常 (主题: '{}'): {}. Raw JSON string:\n{}", topic, e.what(), jsonString);
        }
    }
} // namespace plane::utils
