// cy_psdk/manager/catalog/client/CatalogError.h
//
// 自研 SwarmCatalog 客户端 (由 swarm-catalog-client-java 转译, 全量对齐)。
// 稳定的客户端错误分类 (与 java/C++ SDK 语义一致)。

#pragma once

#include <string_view>

#include "define.h"

namespace plane::catalog
{
	// 客户端侧错误分类 (与 java CatalogError 一一对应; retryable 标记一致)
	enum class CatalogError
	{
		NONE = 0,
		INVALID_ARGUMENT,	   // 参数非法
		ALREADY_STARTED,	   // 运行时已启动
		NOT_STARTED,		   // 运行时未启动
		CATALOG_UNAVAILABLE,   // 服务目录不可用
		CATALOG_CONFLICT,	   // 发现多个服务目录实例
		DISCOVERY_TIMEOUT,	   // 目录探测超时
		REGISTRATION_REJECTED, // 注册被拒绝
		INSTANCE_NOT_FOUND,	   // 实例不存在
		SERVICE_NOT_FOUND,	   // 服务不存在
		CONFIG_NOT_FOUND,	   // 配置不存在
		HTTP_ERROR,			   // HTTP 请求失败
		PROTOCOL_ERROR,		   // 协议或 JSON 非法
		TIMEOUT,			   // 请求超时
		STOPPED				   // 运行时已停止
	};

	// 该错误类别是否可重试 (对齐 java CatalogError.retryable)
	_NODISCARD inline bool isRetryableError(CatalogError code) noexcept
	{
		switch (code)
		{
			case CatalogError::CATALOG_UNAVAILABLE:
			case CatalogError::DISCOVERY_TIMEOUT:
			case CatalogError::HTTP_ERROR:
			case CatalogError::TIMEOUT:
				return true;
			default:
				return false;
		}
	}

	// 默认错误消息 (对齐 java CatalogError.defaultMessage)
	_NODISCARD inline _STD string_view defaultErrorMessage(CatalogError code) noexcept
	{
		switch (code)
		{
			case CatalogError::NONE:
				return "成功";
			case CatalogError::INVALID_ARGUMENT:
				return "参数非法";
			case CatalogError::ALREADY_STARTED:
				return "运行时已启动";
			case CatalogError::NOT_STARTED:
				return "运行时未启动";
			case CatalogError::CATALOG_UNAVAILABLE:
				return "服务目录不可用";
			case CatalogError::CATALOG_CONFLICT:
				return "发现多个服务目录实例";
			case CatalogError::DISCOVERY_TIMEOUT:
				return "目录探测超时";
			case CatalogError::REGISTRATION_REJECTED:
				return "注册被拒绝";
			case CatalogError::INSTANCE_NOT_FOUND:
				return "实例不存在";
			case CatalogError::SERVICE_NOT_FOUND:
				return "服务不存在";
			case CatalogError::CONFIG_NOT_FOUND:
				return "配置不存在";
			case CatalogError::HTTP_ERROR:
				return "HTTP 请求失败";
			case CatalogError::PROTOCOL_ERROR:
				return "协议或 JSON 非法";
			case CatalogError::TIMEOUT:
				return "请求超时";
			case CatalogError::STOPPED:
				return "运行时已停止";
		}
		return "";
	}
} // namespace plane::catalog
