// Copyright 2010, Shuo Chen.  All rights reserved.
// http://code.google.com/p/muduo/
//
// Use of this source code is governed by a BSD-style license
// that can be found in the License file.

// Author: Shuo Chen (chenshuo at chenshuo dot com)
//
// This is a public header file, it must only include public header files.

#ifndef MUDUO_NET_EVENTLOOP_H
#define MUDUO_NET_EVENTLOOP_H

#include <atomic>
#include <functional>
#include <vector>

#include <boost/any.hpp>

#include "muduo/base/Mutex.h"
#include "muduo/base/CurrentThread.h"
#include "muduo/base/Timestamp.h"
#include "muduo/net/Callbacks.h"
#include "muduo/net/TimerId.h"

namespace muduo
{
namespace net
{

//前置类，为了创建该对象，调用对象函数
class Channel;
class Poller;
class TimerQueue;

///
/// Reactor, at most one per thread.
///
/// This is an interface class, so don't expose too much details.
class EventLoop : noncopyable
{
 public:
  typedef std::function<void()> Functor;

  EventLoop();
  ~EventLoop();  // force out-line dtor, for std::unique_ptr members.

  ///
  /// Loops forever.
  ///
  /// Must be called in the same thread as creation of the object.
  /// 只能在创建该对象的线程中调用
  //事件循环，该函数不能跨线程调用
  void loop();

  /// Quits loop.
  ///
  /// This is not 100% thread safe, if you call through a raw pointer,
  /// better to call through shared_ptr<EventLoop> for 100% safety.
  void quit();

  ///
  /// Time when poll returns, usually means data arrival.
  ///
  Timestamp pollReturnTime() const { return pollReturnTime_; }

  int64_t iteration() const { return iteration_; }

  /// Runs callback immediately in the loop thread.
  /// It wakes up the loop, and run the cb.
  /// If in the same loop thread, cb is run within the function.
  /// Safe to call from other threads.
  void runInLoop(Functor cb);
  /// Queues callback in the loop thread.
  /// Runs after finish pooling.
  /// Safe to call from other threads.
  void queueInLoop(Functor cb);

  size_t queueSize() const;

  // timers

  ///
  /// Runs callback at 'time'.
  /// Safe to call from other threads.
  ///
  TimerId runAt(Timestamp time, TimerCallback cb);
  ///
  /// Runs callback after @c delay seconds.
  /// Safe to call from other threads.
  ///
  TimerId runAfter(double delay, TimerCallback cb);
  ///
  /// Runs callback every @c interval seconds.
  /// Safe to call from other threads.
  ///
  TimerId runEvery(double interval, TimerCallback cb);
  ///
  /// Cancels the timer.
  /// Safe to call from other threads.
  ///
  void cancel(TimerId timerId);

  // internal usage
  void wakeup();
  void updateChannel(Channel* channel);       //在 Poller 中添加或者更新通道
  void removeChannel(Channel* channel);       //从 Poller 中删除通道
  bool hasChannel(Channel* channel);

  // pid_t threadId() const { return threadId_; }
  // 断言当前处于创建该对象的线程中
  void assertInLoopThread()
  {
    if (!isInLoopThread())
    {
      //发生错误并退出
      abortNotInLoopThread();
    }
  }

  //判断当前对象是否已经处于创建该对象的线程中
  bool isInLoopThread() const { return threadId_ == CurrentThread::tid(); }
  // bool callingPendingFunctors() const { return callingPendingFunctors_; }
  bool eventHandling() const { return eventHandling_; }

  void setContext(const boost::any& context)
  { context_ = context; }

  const boost::any& getContext() const
  { return context_; }

  boost::any* getMutableContext()
  { return &context_; }

  static EventLoop* getEventLoopOfCurrentThread();

 private:
  void abortNotInLoopThread();
  void handleRead();  // waked up
  void doPendingFunctors();

  void printActiveChannels() const; // DEBUG

  typedef std::vector<Channel*> ChannelList;

  bool looping_; /* atomic */   //是否处于循环状态，bool 类型变量默认是原子操作
  std::atomic<bool> quit_;      //是否退出
  bool eventHandling_; /* atomic */     //当前是否处于事件处理状态
  bool callingPendingFunctors_; /* atomic */  //正在调用 pendingFunc
  int64_t iteration_;
  const pid_t threadId_;            //线程 ID，记录当前对象属于哪个线程
  Timestamp pollReturnTime_;        //调用 poll 函数返回的时间
  std::unique_ptr<Poller> poller_;  //虚基类指针，指向派生类对象
  std::unique_ptr<TimerQueue> timerQueue_;

  //在其他线程中调用 quit 时，会唤醒 wakeupfd，原来的 IO 就会被唤醒
  //在其他线程中调用 queueInLoop，也会调用 wakeup 唤醒
  int wakeupFd_;                    //用于 eventfd
  // unlike in TimerQueue, which is an internal class,
  // we don't expose Channel to client.
  // EvenLoop 只负责 wakeChannel_ 的生存期
  std::unique_ptr<Channel> wakeupChannel_;    //该通道将会纳入 poller_ 来管理
  boost::any context_;

  // scratch variables
  ChannelList activeChannels_;      //Poller 返回的活动通道
  Channel* currentActiveChannel_;   //当前正在处理的活动通道

  mutable MutexLock mutex_;
  std::vector<Functor> pendingFunctors_ GUARDED_BY(mutex_);
};

}  // namespace net
}  // namespace muduo

#endif  // MUDUO_NET_EVENTLOOP_H
