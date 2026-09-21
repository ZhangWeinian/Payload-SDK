// cy_psdk/manager/psdk/PSDKAdapter.h

#pragma once

#include <dji_fc_subscription.h>
#include <dji_flight_controller.h>
#include <dji_hms_manager.h>
#include <dji_typedef.h>
#include <dji_version.h>
#include <dji_waypoint_v3.h>

#include "manager/event_manager/EventManager.h"
#include "protocol/DroneDataClass.h"
#include "protocol/HeartbeatDataClass.h"

#include <BS_thread_pool.hpp>
#include <eventpp/eventdispatcher.h>
#include <eventpp/utilities/scopedremover.h>

#include <source_location>
#include <string_view>
#include <unordered_map>
#include <atomic>
#include <chrono>
#include <future>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <variant>
#include <vector>

#include "define.h"

// V3.16.0 起 PSDK 新增 "返航电量/剩余飞行时间" 回调 API;
// 3.14/3.15 的库没有该接口, 编译期裁剪对应功能 (低电量返航评估数据为空)
#if DJI_VERSION_MAJOR > 3 || (DJI_VERSION_MAJOR == 3 && DJI_VERSION_MINOR >= 16)
    #define CY_PSDK_HAS_BATTERY_CAPACITY_GOHOME 1
#else
    #define CY_PSDK_HAS_BATTERY_CAPACITY_GOHOME 0
#endif

namespace plane::manager
{
    class PSDKAdapter
    {
    public:
        static PSDKAdapter& getInstance(void) noexcept;

        // 启动 PSDK 适配器服务，这是一个幂等的操作
        [[nodiscard]] bool start(void) noexcept;

        // 停止 PSDK 适配器服务，这是一个幂等的操作
        void stop(::std::chrono::milliseconds timeout = ::std::chrono::seconds(5)) noexcept;

        // 获取最新的状态数据载荷的一个副本
        [[nodiscard]] plane::protocol::StatusPayload getLatestStatusPayload(void) const noexcept;

    private:
        explicit PSDKAdapter(void) noexcept;
        ~PSDKAdapter(void) noexcept;
        PSDKAdapter(const PSDKAdapter&) noexcept            = delete;
        PSDKAdapter& operator=(const PSDKAdapter&) noexcept = delete;

        // PSDK 适配器状态
        enum class State
        {
            STOPPED,  // 已停止
            STARTING, // 启动中
            RUNNING,  // 运行中
            STOPPING  // 停止中
        };

        // 订阅 PSDK 数据状态、添加 PSDK 状态订阅
        [[nodiscard]] bool subscribeTelemetryData(void) noexcept;

        // 清理 PSDK 适配器状态、移除 PSDK 状态订阅
        void unsubscribeTelemetryData(void) noexcept;

        // 从 PSDK 四元数到欧拉角的转换，将 PSDK 飞控订阅的四元数数据转换为以度为单位的 roll（横滚）、pitch（俯仰）、yaw（偏航）三个角度
        void convertQuaternionToEulerAngle(const ::T_DjiFcSubscriptionQuaternion& q, double& roll, double& pitch, double& yaw) noexcept;
        // 读取固定设备信息 (飞控序列号等) 写入域模型; 适配器就绪后调用一次 (失败仅告警, 下次启动重试)
        // 读取飞控真实序列号并写入域模型 (返回是否成功取得; 失败由采集循环周期性重试)
        [[nodiscard]] bool refreshFixedAircraftInfo(void) noexcept;
        // 周期性地从 PSDK 订阅的飞控数据主题中拉取最新状态，转换为统一的 StatusPayload ，并通过事件总线发布
        void acquisitionLoop(void) noexcept;

        // 在 PSDK 航线任务状态发生变化时，将状态信息通过事件总线广播给系统其他模块
        void missionStateCallback(::T_DjiWaypointV3MissionState missionState);

        // 作为 PSDK 所要求的 C 兼容回调函数，将 C 接口调用桥接到 C++ 成员函数 missionStateCallback ，并返回 PSDK 期望的成功码
        static ::T_DjiReturnCode missionStateCallbackEntry(::T_DjiWaypointV3MissionState missionState);

        // 处理 PSDK 航线中某个具体动作执行状态变化，并通过事件总线广播给系统其他模块
        void actionStateCallback(::T_DjiWaypointV3ActionState actionState);

        // 作为 PSDK 所要求的 C 兼容回调函数，将 C 接口调用桥接到 C++ 成员函数 actionStateCallback ，并返回 PSDK 期望的成功码
        static ::T_DjiReturnCode actionStateCallbackEntry(::T_DjiWaypointV3ActionState actionState);

        // 处理 PSDK HMS 信息更新，并通过事件总线广播给系统其他模块
        void hmsInfoCallback(::T_DjiHmsInfoTable hmsInfoTable);

        // 作为 PSDK 所要求的 C 兼容回调函数，将 C 接口调用桥接到 C++ 成员函数 hmsInfoCallback ，并返回 PSDK 期望的成功码
        static ::T_DjiReturnCode hmsInfoCallbackEntry(::T_DjiHmsInfoTable hmsInfoTable);

#if CY_PSDK_HAS_BATTERY_CAPACITY_GOHOME
        // 处理飞控"返航电量/剩余飞行时间"回调, 更新域模型低电量返航评估
        void batteryCapacityGohomeCallback(::T_DjiFlightControllerBatteryCapacityGohome info);

        // 作为 PSDK 所要求的 C 兼容回调函数, 将 C 接口调用桥接到 C++ 成员函数 batteryCapacityGohomeCallback
        static ::T_DjiReturnCode batteryCapacityGohomeCallbackEntry(::T_DjiFlightControllerBatteryCapacityGohome info);
#endif

        // 读取相机固定信息 (相机型号/固件版本) 写入域模型; 无相机/不支持时仅告警
        void refreshFixedCameraInfo(void) noexcept;

        // PSDK 命令执行器，可以安全地在线程池中异步执行一个 PSDK 命令，并返回一个 ::std::future 用于获取执行结果
        template<typename CommandLogic>
        ::std::future<::T_DjiReturnCode>
            executePsdkCommandAsync(CommandLogic&& logic, const ::std::source_location& location = ::std::source_location::current());

        // 异步执行 PSDK 航线动作命令，封装 DjiWaypointV3_Action 为一个异步任务
        ::std::future<::T_DjiReturnCode> executeWaypointActionAsync(
            ::E_DjiWaypointV3Action       action,
            const ::std::source_location& location = ::std::source_location::current()
        );

        // 异步执行起飞指令，使飞行器从地面垂直升空至安全高度
        [[nodiscard]] ::std::future<::T_DjiReturnCode> takeoffAsync(const plane::protocol::TakeoffPayload& takeoffParams);

        // 异步执行返航指令，使飞行器自动返回起飞点并降落
        [[nodiscard]] ::std::future<::T_DjiReturnCode> goHomeAsync(void);

        // 异步执行悬停指令，使飞行器在当前位置紧急制动并保持悬停
        [[nodiscard]] ::std::future<::T_DjiReturnCode> hoverAsync(void);

        // 异步执行降落指令，使飞行器从当前位置垂直下降并着陆
        [[nodiscard]] ::std::future<::T_DjiReturnCode> landAsync(void);

        // 异步上传并执行 KMZ 格式的航线任务
        [[nodiscard]] ::std::future<::T_DjiReturnCode> waypointAsync(const kmz_data_type& kmzData);

        // 异步设置飞行控制策略
        [[nodiscard]] ::std::future<::T_DjiReturnCode> setControlStrategyAsync(const ptz_control_strategy_type& strategyCode);

        // 异步执行环绕指定地理点飞行的任务
        [[nodiscard]] ::std::future<::T_DjiReturnCode> selfPOIAsync(const plane::protocol::CircleFlyPayload& circleParams);

        // 执行云台角度控制指令
        void rotateGimbal(const plane::protocol::GimbalControlPayload& payload);

        // 设置相机变焦倍数
        void setCameraZoomFactor(const plane::protocol::ZoomControlPayload& payload);

        // 切换相机视频流源
        void setCameraStreamSource(const video_source_type& source);

        // 发送原始虚拟摇杆数据
        void sendRawStickData(const plane::protocol::StickDataPayload& payload);

        // 启用虚拟摇杆控制模式
        void enableVirtualStick(const plane::protocol::StickModeSwitchPayload& payload);

        // 禁用虚拟摇杆控制模式
        void disableVirtualStick(const plane::protocol::StickModeSwitchPayload& payload);

        // 同步发送 NED 坐标系下的速度指令（北-东-地）
        void sendNedVelocityCommand(const plane::protocol::NedVelocityPayload& payload);

        // 异步停止当前正在执行的航线任务
        [[nodiscard]] ::std::future<::T_DjiReturnCode> stopWaypointMissionAsync(void);

        // 异步暂停当前正在执行的航线任务
        [[nodiscard]] ::std::future<::T_DjiReturnCode> pauseWaypointMissionAsync(void);

        // 异步恢复已暂停的航线任务
        [[nodiscard]] ::std::future<::T_DjiReturnCode> resumeWaypointMissionAsync(void);

        // 持续监听并处理命令事件的主循环
        void commandProcessingLoop(void);

        // 注册一个回调函数，用于响应指定类型的命令事件
        template<typename PayloadType, typename Func>
        void registerCommandListener(plane::manager::EventManager::CommandEvent event, Func func);

        friend class PSDKManager;

        // 当前适配器状态
        struct SubscriptionStatus
        {
            /*!
             * 飞行器融合位置主题名称。请参考 ::T_DjiFcSubscriptionPositionFused 了解数据结构信息。
             *
             * @warning 请注意，如果 GPS 信号较弱（参见下方的 visibleSatelliteNumber），则纬度/经度值将不会更新，但高度仍可能更新。
             *          目前无法判断纬度/经度的更新是否可靠。
             *
             * @details 此主题最重要的组成部分是 T_DjiFcSubscriptionPositionFused::visibleSatelliteNumber。
             *          请使用该值来跟踪您的 GPS 卫星覆盖情况，并建立一些启发式方法，以便在可能失去 GPS 更新时提前做出预判。
             */
            bool positionFused { false };

            /*!
             * @brief 飞行器融合高度主题名称。融合高度主题提供飞行器相对于海平面的融合高度。
             *        请参考 ::T_DjiFcSubscriptionAltitudeFused 了解数据结构信息。
             *
             * 单位 m
             * 数据结构 \ref T_DjiFcSubscriptionAltitudeFused
             */
            bool altitudeFused { false };

            /*!
             * @brief 提供飞行器上次起飞时相对于海平面的高度。
             *
             * @details 这是飞控系统融合输出的结果，同时也使用了国际标准大气（ICAO）模型。
             *          ICAO 模型定义在 15°C 时海平面标准气压为 1013.25 mBar，温度递减率为每 1000 米下降 6.5°C。
             *          在您的实际场景中，起飞点的气压可能高于 1013.25 mBar。例如，气象站显示旧金山国际机场（SFO）近期记录的气压为 1027.1 mBar。
             *          SFO 实际海拔约为 4 米，但若使用 ICAO 模型计算气压高度，则对应约为 -114 米。您可以使用在线计算器来估算您所在区域的气压高度。
             *
             *          影响高度读数的另一个因素是气压计的制造差异——在同一物理位置，两架不同的飞行器之间出现 ±30 米的高度偏差并不罕见。
             *          对于同一架飞行器，这些读数通常是稳定的，因此如果您的代码依赖于绝对高度值的准确性，您需要对系统进行偏移校准。
             *
             * @note 该值在每次无人机起飞时更新。
             *
             * 单位 m
             * 数据结构 \ref T_DjiFcSubscriptionAltitudeOfHomePoint
             */
            bool altitudeOfHomepoint { false };

            /*!
             * @brief 飞行器四元数主题名称。四元数主题提供从飞行器机体坐标系（FRD）到地面坐标系（NED）的旋转关系。
             *        请参考 ::T_DjiFcSubscriptionQuaternion 了解数据结构信息。
             *
             * @details DJI 的四元数采用 Hamilton 约定（q0 = w, q1 = x, q2 = y, q3 = z）。
             *
             * 数据结构 \ref T_DjiFcSubscriptionQuaternion
             */
            bool quaternion { false };

            /*!
             * @brief 飞行器速度主题名称。速度主题提供飞行器在固定于地面的 NEU 坐标系中的速度。
             *        请参考 ::T_DjiFcSubscriptionVelocity 了解数据结构信息。
             *
             * @warning 请注意，此数据并非采用常规的右手坐标系。
             *
             * @details 该速度数据是飞行器融合输出的结果。原始输出是在右手 NED 坐标系中，但在发布到此主题前，Z 轴速度的符号已被翻转。
             *          因此，如果您希望获得 NED 坐标系下的速度，只需将 Z 轴速度值再次取反即可。
             *          在此基础上，您可以通过旋转将其转换为任意右手坐标系。
             *
             * 数据结构 \ref T_DjiFcSubscriptionVelocity
             */
            bool velocity { false };

            /*!
             * @brief 电池信息主题名称。请参考 ::T_DjiFcSubscriptionWholeBatteryInfo 了解数据结构信息。
             *
             * 数据结构 \ref T_DjiFcSubscriptionWholeBatteryInfo
             */
            bool batteryInfo { false };

            /*!
             * @brief 提供 1 号云台的俯仰（pitch）、横滚（roll）、偏航（yaw）角度，最高更新频率达 50Hz 。
             *
             * @details 云台角度的参考坐标系是附着于云台的 NED 坐标系。
             *          该主题使用了一个过于通用的数据结构 Vector3f 。各分量含义如下:
             *          |  数据结构元素  |      含义      |
             *          |  Vector3f.x  |  俯仰角（pitch）|
             *          |  Vector3f.y  |  横滚角（roll） |
             *          |  Vector3f.z  |  偏航角（yaw）  |
             *
             * 单位 deg（度）
             * 数据结构 \ref T_DjiFcSubscriptionGimbalAngles
             * 参见 \ref TOPIC_GIMBAL_STATUS, \ref TOPIC_GIMBAL_CONTROL_MODE
             */
            bool gimbalAngles { false };

            /*! @brief 主电池单电池详情 (INDEX1): 温度/电流/电芯数 */
            bool batterySingleInfo { false };

            /*! @brief 飞行器飞行状态 (0 停桨 / 1 地面转 / 2 空中) */
            bool statusFlight { false };

            /*! @brief 飞行器显示模式 (DJI Go 状态机) */
            bool statusDisplayMode { false };

            /*! @brief 返航点经纬度 */
            bool homePointInfo { false };

            /*! @brief 返航点是否已设置 */
            bool homePointSetStatus { false };

            /*! @brief GPS 信号等级 (0-5, 越大越好) */
            bool gpsSignalLevel { false };

            /*! @brief GPS 控制等级 */
            bool gpsControlLevel { false };

            /*! @brief 控制设备/控制权归属 */
            bool controlDevice { false };
        } sub_status_;

        mutable ::std::mutex                                                                    payload_mutex_ {};
        ::std::mutex                                                                            psdk_command_mutex_ {};
        ::std::mutex                                                                            mission_state_mutex_ {};
        ::std::mutex                                                                            hms_mutex_ {};
        ::std::thread                                                                           acquisition_thread_ {};
        ::std::thread                                                                           command_processing_thread_ {};
        ::std::vector<::std::uint32_t>                                                          last_hms_error_codes_ {};
        ::std::atomic<State>                                                                    state_ { State::STOPPED };
        ::std::atomic<bool>                                                                     run_acquisition_ { false };
        ::std::atomic<bool>                                                                     run_command_processing_ { false };
        ::std::unique_ptr<::BS::thread_pool<>>                                                  command_pool_ {};
        ::std::unique_ptr<::std::promise<::T_DjiReturnCode>>                                    mission_completion_promise_ {};
        ::std::unique_ptr<::eventpp::ScopedRemover<plane::manager::EventManager::CommandQueue>> command_queue_remover_ {};

        ::T_DjiWaypointV3MissionState                                                           last_mission_state_ {};

        plane::protocol::StatusPayload                                                          latest_payload_ {};
        constexpr static auto ACQUISITION_INTERVAL { ::std::chrono::milliseconds(20) };

        // 真数据采集运行态 (跳线程: 回调/命令线程写入, 采集线程读取)
        ::std::atomic<int>                      mission_current_waypoint_ { 0 }; // 当前航点 (航线任务回调写入)
        ::std::atomic<int>                      virtual_stick_mode_ { 0 };       // 虚拟摇杆模式: 0 关 / 1 启用 / 2 高级 (命令写入)
        ::std::atomic<int>                      laser_distance_01m_ { -1 };      // 激光测距 (0.1m 单位; -1 = 未测到)
        ::std::chrono::steady_clock::time_point last_laser_poll_ {};             // 激光轮询节流 (仅采集线程使用)
    };
} // namespace plane::manager
