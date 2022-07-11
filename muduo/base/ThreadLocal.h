// Use of this source code is governed by a BSD-style license
// that can be found in the License file.
//
// Author: Shuo Chen (chenshuo at chenshuo dot com)

#ifndef MUDUO_BASE_THREADLOCAL_H
#define MUDUO_BASE_THREADLOCAL_H

#include "muduo/base/Mutex.h"
#include "muduo/base/noncopyable.h"

#include <pthread.h>

namespace muduo
{

template<typename T>
class ThreadLocal : noncopyable
{
 public:
  ThreadLocal()
  {
    //创建 pkey_，实际对象由 destructor 销毁
    MCHECK(pthread_key_create(&pkey_, &ThreadLocal::destructor));
  }

  ~ThreadLocal()
  {
    //销毁 pkey_，但不销毁 pkey_ 指向的对象
    MCHECK(pthread_key_delete(pkey_));
  }

  T& value()
  {
    //获取线程特定数据，转化为特定类型
    T* perThreadValue = static_cast<T*>(pthread_getspecific(pkey_));
    //如果特定数据为空，则创建数据，放入 pkey_ 中
    if (!perThreadValue)
    {
      //实际数据是通过 new 创建的，堆上数据，需要调用 delete 释放
      T* newObj = new T();
      //设置 pkey_ 指向
      MCHECK(pthread_setspecific(pkey_, newObj));
      perThreadValue = newObj;
    }
    return *perThreadValue;
  }

 private:

  //调用 phread_key_delete 传入的回调函数，用于销毁 pkey 指向的实际数据
  static void destructor(void *x)
  {
    T* obj = static_cast<T*>(x);
    //判断完全数据类型
    typedef char T_must_be_complete_type[sizeof(T) == 0 ? -1 : 1];
    T_must_be_complete_type dummy; (void) dummy;
    //销毁数据
    delete obj;
  }

 private:
  pthread_key_t pkey_;
};

}  // namespace muduo

#endif  // MUDUO_BASE_THREADLOCAL_H
