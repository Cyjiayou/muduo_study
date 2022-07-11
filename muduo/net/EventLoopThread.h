// Copyright 2010, Shuo Chen.  All rights reserved.
// http://code.google.com/p/muduo/
//
// Use of this source code is governed by a BSD-style license
// that can be found in the License file.

// Author: Shuo Chen (chenshuo at chenshuo dot com)
//
// This is a public header file, it must only include public header files.

#ifndef MUDUO_NET_EVENTLOOPTHREAD_H
#define MUDUO_NET_EVENTLOOPTHREAD_H

#include "muduo/base/Condition.h"
#include "muduo/base/Mutex.h"
#include "muduo/base/Thread.h"

namespace muduo
{
namespace net
{

class EventLoop;

class EventLoopThread : noncopyable
{
 public:
  typedef std::function<void(EventLoop*)> ThreadInitCallback;

  //回调函数默认为空
  EventLoopThread(const ThreadInitCallback& cb = ThreadInitCallback(),
                  const string& name = string());
  ~EventLoopThread();
  EventLoop* startLoop();     //启动线程，该线程称为 IO 线程

 private:
  void threadFunc();          //线程函数

  EventLoop* loop_ GUARDED_BY(mutex_);    //指向一个 EvenLoop 对象，一个线程只有一个 EvenLoop 对象
  bool exiting_;
  Thread thread_;     //包含了一个 thread_ 类对象，基于编程的思想
  //使用锁和条件变量的原因
  //主线程和子线程共用一个 loop 对象，主线程相等于消费者
  //子线程相当于生产者，主线程需要等待子线程产生 loop 对象
  MutexLock mutex_;   
  Condition cond_ GUARDED_BY(mutex_);
  ThreadInitCallback callback_;   //回调函数在 EvenLoop::loop 事件循环之前调用
};

}  // namespace net
}  // namespace muduo

#endif  // MUDUO_NET_EVENTLOOPTHREAD_H

