// Copyright 2010, Shuo Chen.  All rights reserved.
// http://code.google.com/p/muduo/
//
// Use of this source code is governed by a BSD-style license
// that can be found in the License file.

// Author: Shuo Chen (chenshuo at chenshuo dot com)

#include "muduo/net/EventLoopThreadPool.h"

#include "muduo/net/EventLoop.h"
#include "muduo/net/EventLoopThread.h"

#include <stdio.h>

using namespace muduo;
using namespace muduo::net;

EventLoopThreadPool::EventLoopThreadPool(EventLoop* baseLoop, const string& nameArg)
  : baseLoop_(baseLoop),
    name_(nameArg),
    started_(false),
    numThreads_(0),
    next_(0)
{
}

//EvenLoop 对象都是栈上对象，不需要我们释放
//EvenLoopThread 由 unique_ptr 管理，也不需要我们释放
EventLoopThreadPool::~EventLoopThreadPool()
{
  // Don't delete loop, it's stack variable
}


//启动线程池
void EventLoopThreadPool::start(const ThreadInitCallback& cb)
{
  assert(!started_);
  baseLoop_->assertInLoopThread();

  started_ = true;

  //创建若干个线程
  for (int i = 0; i < numThreads_; ++i)
  {
    char buf[name_.size() + 32];
    snprintf(buf, sizeof buf, "%s%d", name_.c_str(), i);
    EventLoopThread* t = new EventLoopThread(cb, buf);
    threads_.push_back(std::unique_ptr<EventLoopThread>(t));
    //启动 EvenLoopThread 线程，在进入事件循环之前，会调用 cb
    //函数返回一个 EvenLoop 对象
    loops_.push_back(t->startLoop());
  }
  //如果只有一个 EvenLoop 对象，那么在使用这个 EvenLoop 对象之前先执行 cb
  if (numThreads_ == 0 && cb)
  {
    cb(baseLoop_);
  }
}

//当一个新连接到来时，我们要选择一个 EvenLoop 对象进行处理
EventLoop* EventLoopThreadPool::getNextLoop()
{
  //新连接需要在主IO线程中调用
  baseLoop_->assertInLoopThread();
  assert(started_);
  EventLoop* loop = baseLoop_;

  //如果 loops_ 为空，则 loop 指向 baseLoop，即只有一个 EvenLoop
  //如果不为空，则按照轮叫的调度方式选择一个 EvenLoop
  if (!loops_.empty())
  {
    // round-robin
    loop = loops_[next_];
    ++next_;
    if (implicit_cast<size_t>(next_) >= loops_.size())
    {
      next_ = 0;
    }
  }
  return loop;
}

EventLoop* EventLoopThreadPool::getLoopForHash(size_t hashCode)
{
  baseLoop_->assertInLoopThread();
  EventLoop* loop = baseLoop_;

  if (!loops_.empty())
  {
    loop = loops_[hashCode % loops_.size()];
  }
  return loop;
}

std::vector<EventLoop*> EventLoopThreadPool::getAllLoops()
{
  baseLoop_->assertInLoopThread();
  assert(started_);
  if (loops_.empty())
  {
    return std::vector<EventLoop*>(1, baseLoop_);
  }
  else
  {
    return loops_;
  }
}
