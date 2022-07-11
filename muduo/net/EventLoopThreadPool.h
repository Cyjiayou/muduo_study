// Copyright 2010, Shuo Chen.  All rights reserved.
// http://code.google.com/p/muduo/
//
// Use of this source code is governed by a BSD-style license
// that can be found in the License file.

// Author: Shuo Chen (chenshuo at chenshuo dot com)
//
// This is an internal header file, you should not include this.

#ifndef MUDUO_NET_EVENTLOOPTHREADPOOL_H
#define MUDUO_NET_EVENTLOOPTHREADPOOL_H

#include "muduo/base/noncopyable.h"
#include "muduo/base/Types.h"

#include <functional>
#include <memory>
#include <vector>

namespace muduo
{

namespace net
{

class EventLoop;
class EventLoopThread;

class EventLoopThreadPool : noncopyable
{
 public:
  typedef std::function<void(EventLoop*)> ThreadInitCallback;

  EventLoopThreadPool(EventLoop* baseLoop, const string& nameArg);
  ~EventLoopThreadPool();
  void setThreadNum(int numThreads) { numThreads_ = numThreads; }
  void start(const ThreadInitCallback& cb = ThreadInitCallback());

  // valid after calling start()
  /// round-robin
  EventLoop* getNextLoop();

  /// with the same hash code, it will always return the same EventLoop
  EventLoop* getLoopForHash(size_t hashCode);

  std::vector<EventLoop*> getAllLoops();

  bool started() const
  { return started_; }

  const string& name() const
  { return name_; }

 private:

  //main Reactor 的 EvenLoop
  EventLoop* baseLoop_;   //与 Acceptor 所属的 EvenLoop 对象相同
  string name_;
  bool started_;
  int numThreads_;    //线程数
  int next_;          //新连接到来，所选择的 EvenLoop 的对象下标
  //IO 线程列表，由于使用 unique_ptr 进行管理，当这个对象销毁时，EvenLoopThread 对象也跟着销毁
  std::vector<std::unique_ptr<EventLoopThread>> threads_;   
  //EvenLoop 列表，一个 IO 线程对应一个 EvenLoop 对象，这些对象都是栈上对象
  std::vector<EventLoop*> loops_;
};

}  // namespace net
}  // namespace muduo

#endif  // MUDUO_NET_EVENTLOOPTHREADPOOL_H
