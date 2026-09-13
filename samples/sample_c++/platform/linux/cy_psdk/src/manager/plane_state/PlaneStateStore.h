// cy_psdk/manager/plane_state/PlaneStateStore.h
//
// 内部"最新状态"运行时存储: 进程级唯一实例 (函数静态单例, 生命周期同进程), 全部 API 线程安全。
// 对应 msdk 的 core/state/PlaneState.kt (状态持有者, 不属于 domain 纯数据镜像层);
// 持有单一数据类 domain/PlaneStateDataClass.h (与 msdk 1:1)。
//
// 并发模型 (读写均可在任意线程/异步回调中高频调用):
//   - 一把 std::mutex 保护整份状态; 所有操作的临界区只做"单个/少数字段的拷贝赋值" (不整份复制),
//     即纳秒级; 读与写持锁成本对称, 高频下锁本身不构成瓶颈, 也不存在写者饥饿。
//   - 字段级 API 以成员指针寻址 (类型安全, 零维护: 模型增减字段自动可用, 无需手写访问器):
//       set   写单字段        modify  单字段就地读改写 (并发不丢更新)
//       get   读单字段        read    同一把锁内一次取齐多个字段 (组合读取不会半新半旧)
//   - update/snapshot 为整份批量操作, 用于低频路径 (启动装载 / 遥测整包组装)。
//
// 调用方约定:
//   - update(...)/modify(...) 的回调在锁内执行: 严禁阻塞; 严禁再次调用本类任何方法
//     (std::mutex 不可重入, 会自锁; 需要其它字段时先在回调外取值再进入);
//   - get/read/snapshot 返回副本, 修改副本不影响存储。

#pragma once

#include "domain/PlaneStateDataClass.h"

#include <type_traits>
#include <functional>
#include <mutex>
#include <tuple>
#include <utility>

namespace plane::domain
{
    class PlaneStateStore
    {
    public:
        // 进程级唯一实例: 首次调用时构造 (函数静态变量, C++ 保证构造线程安全), 生命周期同进程
        static PlaneStateStore& getInstance(void) noexcept;

        // ---- 字段级写入 ----

        // 单字段写入 (只复制该字段本身, 不触碰整份状态; 用法: set(&PlaneStateDataClass::字段, 值))
        template<typename Field, typename Value>
        void set(Field PlaneStateDataClass::* member, Value&& value) noexcept
        {
            static_assert(::std::is_assignable_v<Field&, Value&&>, "set(): 值类型必须可赋值给目标字段");
            const ::std::lock_guard<::std::mutex> lock { this->mutex_ };
            this->state_.*member = ::std::forward<Value>(value);
        }

        // 单字段就地读改写 (如计数器自增; 整体在锁内完成, 并发不丢失更新)
        template<typename Field, typename Fn>
        void modify(Field PlaneStateDataClass::* member, Fn&& fn) noexcept
        {
            static_assert(::std::is_invocable_v<Fn&, Field&>, "modify(): 回调需可接受 Field& (如 [](double& v) { v += 1.0; })");
            const ::std::lock_guard<::std::mutex> lock { this->mutex_ };
            ::std::forward<Fn>(fn)(this->state_.*member);
        }

        // ---- 字段级读取 ----

        // 单字段读取 (返回副本; 用法: get(&PlaneStateDataClass::字段))
        template<typename Field>
        [[nodiscard]] Field get(Field PlaneStateDataClass::* member) const noexcept
        {
            const ::std::lock_guard<::std::mutex> lock { this->mutex_ };
            return this->state_.*member;
        }

        // 多字段一致读取 (同一把锁内一次取齐; 返回 std::tuple, 建议结构化绑定接住)
        template<typename... Fields>
        [[nodiscard]] ::std::tuple<Fields...> read(Fields PlaneStateDataClass::*... members) const noexcept
        {
            const ::std::lock_guard<::std::mutex> lock { this->mutex_ };
            return ::std::tuple<Fields...> { (this->state_.*members)... };
        }

        // ---- 整份操作 (低频: 启动装载 / 遥测整包组装) ----

        // 整份替换最新状态
        void update(PlaneStateDataClass data) noexcept;

        // 就地更新最新状态 (回调在锁内执行, 不得阻塞/重入)
        void update(const ::std::function<void(PlaneStateDataClass&)>& mutator) noexcept;

        // 取快照 (线程安全, 返回拷贝)
        [[nodiscard]] PlaneStateDataClass snapshot(void) const noexcept;

    private:
        PlaneStateStore(void) noexcept                         = default;
        ~PlaneStateStore(void) noexcept                        = default;
        PlaneStateStore(const PlaneStateStore&)                = delete;
        PlaneStateStore&     operator=(const PlaneStateStore&) = delete;

        mutable ::std::mutex mutex_ {};
        PlaneStateDataClass  state_ {};
    };
} // namespace plane::domain
