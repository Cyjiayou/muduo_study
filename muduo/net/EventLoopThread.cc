// Copyright 2010, Shuo Chen.  All rights reserved.
// http://code.google.com/p/muduo/
//
// Use of this source code is governed by a BSD-style license
// that can be found in the License file.

// Author: Shuo Chen (chenshuo at chenshuo dot com)

#include "muduo/net/EventLoopThread.h"

#include "muduo/net/EventLoop.h"

using namespace muduo;
using namespace muduo::net;

EventLoopThread::EventLoopThread(const ThreadInitCallback& cb,
                                 const string& name)
  : loop_(NULL),
    exiting_(false),
    //线程实际的调用函数是 threadFunc
    thread_(std::bind(&EventLoopThread::threadFunc, this), name),
    mutex_(),
    cond_(mutex_),
    callback_(cb)
{
}

EventLoopThread::~EventLoopThread()
{
  exiting_ = true;
  if (loop_ != NULL) // not 100% race-free, eg. threadFunc could be running callback_.
  {
    // still a tiny chance to call destructed object, if threadFunc exits just now.
    // but when EventLoopThread destructs, usually programming is exiting anyway.
    loop_->quit();    //退出 IO 线程
    thread_.join();   //等待线程结束
  }
}

EventLoop* EventLoopThread::startLoop()
{
  assert(!thread_.started());
  //启动 IO 线程，此时就有两个线程，一个是当前线程，一个是 IO 线程
  //start 函数主要就是调用 threadFunc()
  thread_.start();

  //等待 IO 线程创建 EvenLoop 并返回
  EventLoop* loop = NULL;
  {
    MutexLockGuard lock(mutex_);
    while (loop_ == NULL)
    {
      cond_.wait();
    }
    loop = loop_;
  }

  return loop;
}

void EventLoopThread::threadFunc()
{
  EventLoop loop;

  //如果 cb 不为空，先调用回调函数
  if (callback_)
  {
    callback_(&loop);
  }

  {
    MutexLockGuard lock(mutex_);
    // loop_ 指针指向一个栈上的对象，threadFunc 退出后就失效了
    // threadFunc 函数退出，就意味着线程退出了，EvenLoopThread 对象就没有存在的价值了
    loop_ = &loop;
    cond_.notify();
  }

  loop.loop();
  //assert(exiting_);
  MutexLockGuard lock(mutex_);
  loop_ = NULL;
}

