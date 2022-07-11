// Use of this source code is governed by a BSD-style license
// that can be found in the License file.
//
// Author: Shuo Chen (chenshuo at chenshuo dot com)

#ifndef MUDUO_BASE_CONDITION_H
#define MUDUO_BASE_CONDITION_H

#include "muduo/base/Mutex.h"

#include <pthread.h>

namespace muduo
{

class Condition : noncopyable
{
 public:
  explicit Condition(MutexLock& mutex)
    : mutex_(mutex)
  {
    MCHECK(pthread_cond_init(&pcond_, NULL));   //创建条件变量
  }

  ~Condition()
  {
    MCHECK(pthread_cond_destroy(&pcond_));      //销毁条件变量
  }

  void wait()
  {
    //可省略
    MutexLock::UnassignGuard ug(mutex_);      
    MCHECK(pthread_cond_wait(&pcond_, mutex_.getPthreadMutex()));
  }

  // returns true if time out, false otherwise.
  bool waitForSeconds(double seconds);

  void notify()
  {
    //调用 signal
    MCHECK(pthread_cond_signal(&pcond_));
  }

  void notifyAll()
  {
    //调用 broadcast
    MCHECK(pthread_cond_broadcast(&pcond_));
  }

 private:
  MutexLock& mutex_;        //这里也是引用
  pthread_cond_t pcond_;
};

}  // namespace muduo

#endif  // MUDUO_BASE_CONDITION_H
