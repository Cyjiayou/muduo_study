// Use of this source code is governed by a BSD-style license
// that can be found in the License file.
//
// Author: Shuo Chen (chenshuo at chenshuo dot com)

#ifndef MUDUO_BASE_COUNTDOWNLATCH_H
#define MUDUO_BASE_COUNTDOWNLATCH_H

#include "muduo/base/Condition.h"
#include "muduo/base/Mutex.h"

namespace muduo
{

class CountDownLatch : noncopyable
{
 public:

  explicit CountDownLatch(int count);

  void wait();

  void countDown();

  int getCount() const;

 private:
  mutable MutexLock mutex_;     //为什么这里使用 mutable，因为 getCount 函数使用 const 修饰，而且我们需要修改 mutex_
  Condition condition_ GUARDED_BY(mutex_);
  int count_ GUARDED_BY(mutex_);      //计数器
};

}  // namespace muduo
#endif  // MUDUO_BASE_COUNTDOWNLATCH_H
