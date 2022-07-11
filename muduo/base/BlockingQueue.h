// Use of this source code is governed by a BSD-style license
// that can be found in the License file.
//
// Author: Shuo Chen (chenshuo at chenshuo dot com)

#ifndef MUDUO_BASE_BLOCKINGQUEUE_H
#define MUDUO_BASE_BLOCKINGQUEUE_H

#include "muduo/base/Condition.h"
#include "muduo/base/Mutex.h"

#include <deque>
#include <assert.h>

namespace muduo
{

template<typename T>
class BlockingQueue : noncopyable
{
 public:
  using queue_type = std::deque<T>;

  BlockingQueue()
    : mutex_(),
      notEmpty_(mutex_),
      queue_()
  {
  }

  //生产者
  void put(const T& x)
  {
    //加锁
    //{
    MutexLockGuard lock(mutex_);
    queue_.push_back(x);
    //}
    //唤醒等待，这个地方可以写在解锁前，也可以写在解锁后
    notEmpty_.notify(); // wait morphing saves us
    // http://www.domaigne.com/blog/computing/condvars-signal-with-mutex-locked-or-not/
  }

  void put(T&& x)
  {
    MutexLockGuard lock(mutex_);
    queue_.push_back(std::move(x));
    notEmpty_.notify();
  }

  //消费者
  T take()
  {
    //加锁
    MutexLockGuard lock(mutex_);
    // always use a while-loop, due to spurious wakeup
    while (queue_.empty())
    {
      notEmpty_.wait();
    }
    assert(!queue_.empty());
    T front(std::move(queue_.front()));
    queue_.pop_front();
    return front;
  }

  queue_type drain()
  {
    std::deque<T> queue;
    {
      MutexLockGuard lock(mutex_);
      queue = std::move(queue_);
      assert(queue_.empty());
    }
    return queue;
  }

  //返回队列长度
  size_t size() const
  {
    MutexLockGuard lock(mutex_);
    return queue_.size();
  }

 private:
  mutable MutexLock mutex_;   //需要一个互斥量，有个函数是 mutable
  Condition         notEmpty_ GUARDED_BY(mutex_);   //一个条件变量
  queue_type        queue_ GUARDED_BY(mutex_);      //模板队列
};  // __attribute__ ((aligned (64)));

}  // namespace muduo

#endif  // MUDUO_BASE_BLOCKINGQUEUE_H
