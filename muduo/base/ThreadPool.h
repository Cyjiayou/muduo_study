// Use of this source code is governed by a BSD-style license
// that can be found in the License file.
//
// Author: Shuo Chen (chenshuo at chenshuo dot com)

#ifndef MUDUO_BASE_THREADPOOL_H
#define MUDUO_BASE_THREADPOOL_H

#include "muduo/base/Condition.h"
#include "muduo/base/Mutex.h"
#include "muduo/base/Thread.h"
#include "muduo/base/Types.h"

#include <deque>
#include <vector>

namespace muduo
{

class ThreadPool : noncopyable
{
 public:
  typedef std::function<void ()> Task;

  explicit ThreadPool(const string& nameArg = string("ThreadPool"));
  ~ThreadPool();

  // Must be called before start().
  void setMaxQueueSize(int maxSize) { maxQueueSize_ = maxSize; }
  void setThreadInitCallback(const Task& cb)
  { threadInitCallback_ = cb; }

  void start(int numThreads);     //启动线程池，线程的个数是固定的
  void stop();    

  const string& name() const
  { return name_; }

  size_t queueSize() const;

  // Could block if maxQueueSize > 0
  // Call after stop() will return immediately.
  // There is no move-only version of std::function in C++ as of C++14.
  // So we don't need to overload a const& and an && versions
  // as we do in (Bounded)BlockingQueue.
  // https://stackoverflow.com/a/25408989
  void run(Task f);   //向任务队列添加任务

 private:
  bool isFull() const REQUIRES(mutex_);
  void runInThread();     //线程池中的线程执行函数
  Task take();            //获取任务

  mutable MutexLock mutex_;       //互斥锁
  Condition notEmpty_ GUARDED_BY(mutex_);   //有界队列，判断是否为满
  Condition notFull_ GUARDED_BY(mutex_);    //判断是否为空
  string name_;         //线程池名称
  Task threadInitCallback_;
  std::vector<std::unique_ptr<muduo::Thread>> threads_;   //线程队列
  std::deque<Task> queue_ GUARDED_BY(mutex_);     //任务队列
  size_t maxQueueSize_;
  bool running_;        //判断线程池是否处于运行状态
};

}  // namespace muduo

#endif  // MUDUO_BASE_THREADPOOL_H
