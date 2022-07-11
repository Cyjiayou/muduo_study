// Use of this source code is governed by a BSD-style license
// that can be found in the License file.
//
// Author: Shuo Chen (chenshuo at chenshuo dot com)

#ifndef MUDUO_BASE_THREAD_H
#define MUDUO_BASE_THREAD_H

#include "muduo/base/Atomic.h"
#include "muduo/base/CountDownLatch.h"
#include "muduo/base/Types.h"

#include <functional>
#include <memory>
#include <pthread.h>

namespace muduo
{

class Thread : noncopyable
{
 public:
  //使用了 boost::function<>    void 是返回值类型，() 表示没有参数
  typedef std::function<void ()> ThreadFunc;

  explicit Thread(ThreadFunc, const string& name = string());
  // FIXME: make it movable in C++11
  ~Thread();

  //正常来说这里应该有 static 函数作为线程的启动函数（回调函数）
  //在 static 函数中调用 run() 函数
  //这些操作用放到了 detail::ThreadData 中 runInThread
  //startThread 是一个静态函数
  //runInThread 先要为线程提供一个 tid 号 tid() 函数
  void start();   //启动线程
  int join(); // return pthread_join()

  bool started() const { return started_; }
  // pthread_t pthreadId() const { return pthreadId_; }
  pid_t tid() const { return tid_; }
  const string& name() const { return name_; }

  static int numCreated() { return numCreated_.get(); }

 private:
  void setDefaultName();    

  bool       started_;      //线程是否已经启动了
  bool       joined_;       //是否停止
  pthread_t  pthreadId_;    
  pid_t      tid_;          //线程的真实 id
  ThreadFunc func_;         //该线程要回调的函数
  string     name_;         //线程的名称
  CountDownLatch latch_;    //?

  static AtomicInt32 numCreated_;     //已经创建的线程个数，原子整数类，见 atomic 文件
};

}  // namespace muduo
#endif  // MUDUO_BASE_THREAD_H
