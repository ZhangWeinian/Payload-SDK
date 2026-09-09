// cy_psdk/manager/catalog/client/internal/TargetExpander.cpp

#include "manager/catalog/client/internal/TargetExpander.h"

#include <fmt/format.h>
#include <cstdint>

#include "manager/catalog/client/CatalogError.h"
#include "manager/catalog/client/CatalogFailure.h"

#include "define.h"

namespace plane::catalog::internal
{
	namespace
	{
		_NODISCARD _STD string ipv4ToString(_STD uint64_t ip)
		{
			return _FMT format("{}.{}.{}.{}", (ip >> 24) & 0Xff, (ip >> 16) & 0Xff, (ip >> 8) & 0Xff, ip & 0Xff);
		}

		_NODISCARD int parseOctet(_STD string_view value)
		{
			if (value.empty() || (value.size() > 1 && value.front() == '+'))
			{
				throw _STD invalid_argument { "非法 IP" };
			}
			int octet { 0 };
			try
			{
				const _STD string text { value };
				octet = _STD	  stoi(text);
			}
			catch (const _STD exception&)
			{
				throw _STD invalid_argument { "非法 IP" };
			}
			if (octet < 0 || octet > 255)
			{
				throw _STD invalid_argument { "非法 IP" };
			}
			return octet;
		}

		_NODISCARD _STD uint64_t parseIpv4(_STD string_view value)
		{
			_STD uint64_t result { 0 };
			_STD size_t	  start { 0 };
			int			  part { 0 };
			for (_STD size_t index { 0 }; index <= value.size(); ++index)
			{
				if (index == value.size() || value[index] == '.')
				{
					if (part >= 4)
					{
						throw _STD invalid_argument { "非法 IP" };
					}
					result = (result << 8) | static_cast<_STD uint64_t>(parseOctet(value.substr(start, index - start)));
					start  = index + 1;
					++part;
				}
			}
			if (part != 4)
			{
				throw _STD invalid_argument { "非法 IP" };
			}
			return result;
		}

		// 展开超限异常 (内部标记)
		struct TooManyTargets
		{};

		// 展开一个规格到 result; 返回 false 表示超出上限
		_NODISCARD bool expandSpec(_STD string_view spec, int max_count, _STD vector<_STD string>& result)
		{
			auto add = [&result, max_count](_STD uint64_t ip) -> bool
			{
				if (static_cast<int>(result.size()) >= max_count)
				{
					return false;
				}
				result.push_back(ipv4ToString(ip));
				return true;
			};

			if (spec.find('*') != _STD string_view::npos)
			{
				const _STD size_t wildcard { spec.find('*') };
				if (wildcard != spec.size() - 1 || wildcard == 0 || spec[wildcard - 1] != '.' ||
					spec.find('*', wildcard + 1) != _STD string_view::npos)
				{
					throw _STD invalid_argument { "非法通配目标" };
				}
				const _STD uint64_t network { parseIpv4(spec.substr(0, wildcard)) & 0Xff'ff'ff'00ull };
				for (_STD uint64_t ip { network + 1 }; ip <= network + 254; ++ip)
				{
					if (!add(ip))
					{
						throw TooManyTargets {};
					}
				}
				return true;
			}

			if (spec.find('/') != _STD string_view::npos)
			{
				const _STD size_t	slash { spec.find('/') };
				const _STD uint64_t base { parseIpv4(spec.substr(0, slash)) };
				int					prefix { 0 };
				try
				{
					prefix = _STD stoi(_STD string { spec.substr(slash + 1) });
				}
				catch (const _STD exception&)
				{
					throw _STD invalid_argument { "非法 CIDR 前缀" };
				}
				if (prefix < 0 || prefix > 32)
				{
					throw _STD invalid_argument { "非法 CIDR 前缀" };
				}
				const _STD uint64_t mask { prefix == 0 ? 0ull : (0Xff'ff'ff'ffull << (32 - prefix)) & 0Xff'ff'ff'ffull };
				const _STD uint64_t network { base & mask };
				const _STD uint64_t broadcast { network | (~mask & 0Xff'ff'ff'ffull) };
				const _STD uint64_t first { prefix >= 31 ? network : network + 1 };
				const _STD uint64_t last { prefix >= 31 ? broadcast : broadcast - 1 };
				for (_STD uint64_t ip { first }; ip <= last && ip >= first; ++ip)
				{
					if (!add(ip))
					{
						throw TooManyTargets {};
					}
				}
				return true;
			}

			if (spec.find('-') != _STD string_view::npos)
			{
				const _STD size_t	dash { spec.find('-') };
				const _STD uint64_t start { parseIpv4(spec.substr(0, dash)) };
				const _STD string	right { spec.substr(dash + 1) };
				const _STD uint64_t end { right.find('.') != _STD string::npos
											  ? parseIpv4(right)
											  : (start & 0Xff'ff'ff'00ull) | static_cast<_STD uint64_t>(parseOctet(right)) };
				if (end < start)
				{
					throw _STD invalid_argument { "范围终点小于起点" };
				}
				for (_STD uint64_t ip { start }; ip <= end; ++ip)
				{
					if (!add(ip))
					{
						throw TooManyTargets {};
					}
				}
				return true;
			}

			if (!add(parseIpv4(spec)))
			{
				throw TooManyTargets {};
			}
			return true;
		}
	} // namespace

	Result<_STD vector<_STD string>> expandTargets(const _STD vector<_STD string>& specs, int max_count)
	{
		_STD vector<_STD string> result {};
		try
		{
			for (const auto& raw : specs)
			{
				const _STD string spec { raw.empty() ? "" : raw }; // 保留原样 trim? java trim
				// java 先 trim
				const auto trimmed { _STD string { raw } };
				// 模拟 java: spec = raw.trim()
				// 用自定义 trim
				_STD size_t		  begin { trimmed.find_first_not_of(" \t\r\n") };
				_STD size_t		  end { trimmed.find_last_not_of(" \t\r\n") };
				const _STD string clean { begin == _STD string::npos ? "" : trimmed.substr(begin, end - begin + 1) };
				if (clean.empty())
				{
					continue;
				}
				(void)expandSpec(clean, max_count, result);
			}
		}
		catch (const TooManyTargets&)
		{
			return Result<_STD vector<_STD string>>::
				failure(makeFailure(CatalogError::INVALID_ARGUMENT, _FMT format("展开后的目标数量超过上限 {}", max_count)));
		}
		catch (const _STD exception& ex)
		{
			return Result<_STD vector<_STD string>>::failure(makeFailure(CatalogError::INVALID_ARGUMENT, ex.what()));
		}

		if (result.empty())
		{
			return Result<_STD vector<_STD string>>::failure(makeFailure(CatalogError::INVALID_ARGUMENT, "目标列表为空"));
		}
		return Result<_STD vector<_STD string>>::success(_STD move(result));
	}
} // namespace plane::catalog::internal
