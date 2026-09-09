// cy_psdk/manager/catalog/client/Result.h
//
// 值或失败的结果封装 (对齐 java Result<T>)。用于预期的 SDK 失败路径。

#pragma once

#include <type_traits>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>

#include "define.h"
#include "manager/catalog/client/CatalogFailure.h"

namespace plane::catalog
{
	// 通用结果: 成功携带 T, 失败携带 CatalogFailure。
	// value() 仅在成功时可用; error() 仅在失败时可用; 误用抛 std::logic_error。
	template<typename T>
	class Result
	{
	public:
		using ValueType						   = T;

		Result(const Result&)				   = default;
		Result(Result&&)					   = default;
		Result&		  operator=(const Result&) = default;
		Result&		  operator=(Result&&)	   = default;

		static Result success(T value)
		{
			Result result {};
			result.ok_	  = true;
			result.value_ = _STD move(value);
			return result;
		}

		static Result failure(CatalogFailure error)
		{
			Result result {};
			result.ok_	  = false;
			result.error_ = _STD move(error);
			return result;
		}

		_NODISCARD bool isOk(void) const noexcept
		{
			return ok_;
		}

		_NODISCARD T& value()
		{
			if (!ok_)
			{
				throw _STD logic_error { "Cannot read a failed result: " + error_->message };
			}
			return *value_;
		}

		_NODISCARD const T& value() const
		{
			if (!ok_)
			{
				throw _STD logic_error { "Cannot read a failed result: " + error_->message };
			}
			return *value_;
		}

		_NODISCARD const CatalogFailure& error() const
		{
			if (ok_)
			{
				throw _STD logic_error { "Cannot read error of a successful result" };
			}
			return *error_;
		}

	private:
		Result(void) noexcept = default;

		bool ok_ { false };
		_STD optional<T> value_ {};
		_STD optional<CatalogFailure> error_ {};
	};

	// void 特化 (对齐 java Result<Void>)
	template<>
	class Result<void>
	{
	public:
		using ValueType						   = void;

		Result(const Result&)				   = default;
		Result(Result&&)					   = default;
		Result&		  operator=(const Result&) = default;
		Result&		  operator=(Result&&)	   = default;

		static Result success(void)
		{
			Result result {};
			result.ok_ = true;
			return result;
		}

		static Result failure(CatalogFailure error)
		{
			Result result {};
			result.ok_	  = false;
			result.error_ = _STD move(error);
			return result;
		}

		_NODISCARD bool isOk(void) const noexcept
		{
			return ok_;
		}

		_NODISCARD const CatalogFailure& error() const
		{
			if (ok_)
			{
				throw _STD logic_error { "Cannot read error of a successful result" };
			}
			return *error_;
		}

	private:
		Result(void) noexcept = default;

		bool ok_ { false };
		_STD optional<CatalogFailure> error_ {};
	};
} // namespace plane::catalog
