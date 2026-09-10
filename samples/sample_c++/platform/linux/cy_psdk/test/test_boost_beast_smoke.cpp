// cy_psdk/tests/test_boost_beast_smoke.cpp
//
// Boost.Beast WebSocket 集成冒烟测试:
//   验证 vcpkg 依赖 boost-beast (1.92.0) 在 C++23 下可正常编译/链接。
//   仅做类型构造与生命周期检查, 不进行任何网络操作。

#include <gtest/gtest.h>

#include <boost/asio/io_context.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/beast/core.hpp>
#include <boost/beast/websocket.hpp>

namespace
{
	using WsStream = boost::beast::websocket::stream<boost::asio::ip::tcp::socket>;
} // namespace

TEST(BoostBeastSmoke, WebsocketStreamIsUsable)
{
	boost::asio::io_context io {};
	WsStream				stream { boost::asio::ip::tcp::socket { io } };

	EXPECT_FALSE(stream.is_open());
	stream.binary(true);
}
