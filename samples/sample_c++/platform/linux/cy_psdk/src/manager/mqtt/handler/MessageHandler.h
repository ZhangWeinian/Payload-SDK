// cy_psdk/manager/mqtt/handler/MessageHandler.h

#pragma once

#include "protocol/DroneDataClass.h"

#include <string_view>
#include <functional>
#include <map>
#include <mutex>
#include <string>

#include "define.h"

namespace plane::manager
{
    class MqttMessageHandler
    {
    public:
        using n_json       = ::nlohmann::json;
        using LogicHandler = ::std::function<void(const n_json&)>;

        static MqttMessageHandler& getInstance(void) noexcept;

        void                       registerHandler(::std::string_view topic, ::std::string_view messageType, LogicHandler handler) noexcept;
        void                       routeMessage(::std::string_view topic, ::std::string_view messageType, const n_json& payloadJson) noexcept;

    private:
        explicit MqttMessageHandler(void) noexcept = default;
        ~MqttMessageHandler(void) noexcept         = default;
        // key 存 ::std::string 避免 string_view 悬垂; 两层均用透明比较器(::std::less<>), 允许以 string_view 无分配查找
        ::std::map<::std::string, ::std::map<::std::string, LogicHandler, ::std::less<>>, ::std::less<>> handler_map_ {};
        ::std::mutex                                                                                     handler_mutex_ {};
    };
} // namespace plane::manager
