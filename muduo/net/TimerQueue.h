// Copyright 2010, Shuo Chen.  All rights reserved.
// http://code.google.com/p/muduo/
//
// Use of this source code is governed by a BSD-style license
// that can be found in the License file.

// Author: Shuo Chen (chenshuo at chenshuo dot com)
//
// This is an internal header file, you should not include this.

#ifndef MUDUO_NET_TIMERQUEUE_H
#define MUDUO_NET_TIMERQUEUE_H

#include <set>
#include <vector>

#include "muduo/base/Mutex.h"
#include "muduo/base/Timestamp.h"
#include "muduo/net/Callbacks.h"
#include "muduo/net/Channel.h"

namespace muduo
{
namespace net
{

class EventLoop;
class Timer;
class TimerId;

///
/// A best efforts timer queue.
/// No guarantee that the callback will be on time.
///
class TimerQueue : noncopyable
{
 public:
  explicit TimerQueue(EventLoop* loop);
  ~TimerQueue();

  ///
  /// Schedules the callback to be run at given time,
  /// repeats if @c interval > 0.0.
  ///
  /// Must be thread safe. Usually be called from other threads.
  // 一定是线程安全的，可以跨线程调用，通常情况下被其他先线程调用
  // 添加函数返回 TimerID 用来被 cancel 函数调用
  TimerId addTimer(TimerCallback cb,
                   Timestamp when,
                   double interval);

  void cancel(TimerId timerId);

 private:

  // FIXME: use unique_ptr<Timer> instead of raw pointers.
  // This requires heterogeneous comparison lookup (N3465) from C++14
  // so that we can find an T* in a set<unique_ptr<T>>.
  typedef std::pair<Timestamp, Timer*> Entry;
  typedef std::set<Entry> TimerList;
  typedef std::pair<Timer*, int64_t> ActiveTimer;
  typedef std::set<ActiveTimer> ActiveTimerSet;


  //一下成员函数只可能在其所属的 IO 线程中调用，因而不必加锁
  void addTimerInLoop(Timer* timer);
  void cancelInLoop(TimerId timerId);
  // called when timerfd alarms
  void handleRead();
  // move out all expired timers
  // 返回超时的定时器列表
  std::vector<Entry> getExpired(Timestamp now);
  // 对超时定时器进行重置
  void reset(const std::vector<Entry>& expired, Timestamp now);

  bool insert(Timer* timer);

  EventLoop* loop_;           //所属的 EvenLoop
  const int timerfd_;         //定时器描述符
  Channel timerfdChannel_;    //定时器通道
  // Timer list sorted by expiration
  TimerList timers_;         //timers_ 是按到期时间排序

  // for cancel()
  // timers_ 和 activeTimers_ 存放的是同一个东西，只是顺序不同
  ActiveTimerSet activeTimers_;       //activeTimers_ 是按照对象地址排序
  bool callingExpiredTimers_; /* atomic */    //处理超时定时器
  ActiveTimerSet cancelingTimers_;    //保存被取消的定时器
};

}  // namespace net
}  // namespace muduo
#endif  // MUDUO_NET_TIMERQUEUE_H
