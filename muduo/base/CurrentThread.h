// Use of this source code is governed by a BSD-style license
// that can be found in the License file.
//
// Author: Shuo Chen (chenshuo at chenshuo dot com)

#ifndef MUDUO_BASE_CURRENTTHREAD_H
#define MUDUO_BASE_CURRENTTHREAD_H

#include "muduo/base/Types.h"

namespace muduo
{
namespace CurrentThread
{
  // internal
  //__thread 修饰的变量是线程局部存储的
  //说明这些变量是每个线程特有的，而不是全局变量被每个线程共享
  //声明变量
  extern __thread int t_cachedTid;        //线程真实 pid（tid） 的缓存，为了提高线程获取 tid 的效率，减少 Linux 系统调用次数
  extern __thread char t_tidString[32];   //线程 tid 的字符串表示
  extern __thread int t_tidStringLength;  //线程 tid 的长度
  extern __thread const char* t_threadName;   //线程名称
  void cacheTid();

  //全局函数，可以直接返回当前线程的 tid
  //tid() 第一次会确定线程真实 tid
  //以后的 tid() 都是用来返回 tid 的
  inline int tid() 
  {
    //判断 t_cachedTid 有没有值
    if (__builtin_expect(t_cachedTid == 0, 0))
    {
      cacheTid();
    }
    return t_cachedTid;
  }

  inline const char* tidString() // for logging
  {
    return t_tidString;
  }

  inline int tidStringLength() // for logging
  {
    return t_tidStringLength;
  }

  inline const char* name()
  {
    return t_threadName;
  }

  //判断该线程是不是主线程
  //如果线程 tid 等于当前进程的 pid，则说明该进程就是主线程
  bool isMainThread();

  void sleepUsec(int64_t usec);  // for testing

  string stackTrace(bool demangle);
}  // namespace CurrentThread
}  // namespace muduo

#endif  // MUDUO_BASE_CURRENTTHREAD_H
