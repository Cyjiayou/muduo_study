// Use of this source code is governed by a BSD-style license
// that can be found in the License file.
//
// Author: Shuo Chen (chenshuo at chenshuo dot com)

#ifndef MUDUO_BASE_ATOMIC_H
#define MUDUO_BASE_ATOMIC_H

#include "muduo/base/noncopyable.h"

#include <stdint.h>

namespace muduo
{

namespace detail
{
//模板类，封装原子操作
template<typename T>
class AtomicIntegerT : noncopyable    //不可拷贝【将 == 运算符设置为私有的】
{
 public:
  AtomicIntegerT()
    : value_(0)
  {
  }

  // uncomment if you need copying and assignment
  //
  // AtomicIntegerT(const AtomicIntegerT& that)
  //   : value_(that.get())
  // {}
  //
  // AtomicIntegerT& operator=(const AtomicIntegerT& that)
  // {
  //   getAndSet(that.get());
  //   return *this;
  // }

  T get()
  {
    // in gcc >= 4.7: __atomic_load_n(&value_, __ATOMIC_SEQ_CST)
    //如果 value_ 的值是 0，返回 0 并设置为 0
    //如果 value_ 的值不是 0，返回原来的值，且不设置
    return __sync_val_compare_and_swap(&value_, 0, 0);
  }

  T getAndAdd(T x)
  {
    // in gcc >= 4.7: __atomic_fetch_add(&value_, x, __ATOMIC_SEQ_CST)
    //先获取 value_ 的值，再加上 x
    //返回 value_ 原来的值
    return __sync_fetch_and_add(&value_, x);
  }

  T addAndGet(T x)
  {
    //返回加上 x 的值
    return getAndAdd(x) + x;
  }

  T incrementAndGet()
  {
    //自增
    return addAndGet(1);
  }

  T decrementAndGet()
  {
    //自减
    return addAndGet(-1);
  }

  void add(T x)
  {
    //先获取后加
    getAndAdd(x);
  }

  void increment()
  {
    incrementAndGet();
  }

  void decrement()
  {
    decrementAndGet();
  }

  T getAndSet(T newValue)
  {
    // in gcc >= 4.7: __atomic_exchange_n(&value_, newValue, __ATOMIC_SEQ_CST)
    //返回原来的值并设置为新值
    return __sync_lock_test_and_set(&value_, newValue);
  }

 private:
  //value_ 被设置为 volatile，避免计算机优化，取值的时候从寄存器中获得【多线程中使用】
  volatile T value_;
};
}  // namespace detail

typedef detail::AtomicIntegerT<int32_t> AtomicInt32;
typedef detail::AtomicIntegerT<int64_t> AtomicInt64;

}  // namespace muduo

#endif  // MUDUO_BASE_ATOMIC_H
