// Use of this source code is governed by a BSD-style license
// that can be found in the License file.
//
// Author: Shuo Chen (chenshuo at chenshuo dot com)

#ifndef MUDUO_BASE_SINGLETON_H
#define MUDUO_BASE_SINGLETON_H

#include "muduo/base/noncopyable.h"

#include <assert.h>
#include <pthread.h>
#include <stdlib.h> // atexit

namespace muduo
{

namespace detail
{
// This doesn't detect inherited member functions!
// http://stackoverflow.com/questions/1966362/sfinae-to-check-for-inherited-member-functions
template<typename T>
struct has_no_destroy
{
  template <typename C> static char test(decltype(&C::no_destroy));
  template <typename C> static int32_t test(...);
  const static bool value = sizeof(test<T>(0)) == 1;
};
}  // namespace detail

template<typename T>
class Singleton : noncopyable
{
 public:
  Singleton() = delete;
  ~Singleton() = delete;

  //返回一个对象，唯一公有成员
  static T& instance()
  {
    //保证该函数只被调用一次
    pthread_once(&ponce_, &Singleton::init);
    assert(value_ != NULL);
    return *value_;
  }

 private:
  //内部创建对象
  static void init()
  {
    value_ = new T();
    if (!detail::has_no_destroy<T>::value)
    {
      //程序结束后会销毁对象
      ::atexit(destroy);
    }
  }

  //内部销毁对象
  static void destroy()
  {
    //判断 T 是不是完整类型
    //如果 T 不是完整类型，数组大小是 -1，发生错误
    typedef char T_must_be_complete_type[sizeof(T) == 0 ? -1 : 1];
    T_must_be_complete_type dummy; (void) dummy;

    delete value_;
    value_ = NULL;
  }

 private:
  static pthread_once_t ponce_;     //可以保证函数只被执行一次
  static T*             value_;     //返回对象
};

template<typename T>
pthread_once_t Singleton<T>::ponce_ = PTHREAD_ONCE_INIT;

template<typename T>
T* Singleton<T>::value_ = NULL;

}  // namespace muduo

#endif  // MUDUO_BASE_SINGLETON_H
