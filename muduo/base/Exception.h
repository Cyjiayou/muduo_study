// Use of this source code is governed by a BSD-style license
// that can be found in the License file.
//
// Author: Shuo Chen (chenshuo at chenshuo dot com)

#ifndef MUDUO_BASE_EXCEPTION_H
#define MUDUO_BASE_EXCEPTION_H

#include "muduo/base/Types.h"
#include <exception>

namespace muduo
{

class Exception : public std::exception
{
 public:
  Exception(string what);
  ~Exception() noexcept override = default;

  // default copy-ctor and operator= are okay.

  //返回 message
  const char* what() const noexcept override
  {
    return message_.c_str();
  }

  //返回 stack
  const char* stackTrace() const noexcept
  {
    return stack_.c_str();
  }

 private:
  string message_;      //异常信息字符串
  string stack_;        //异常发生时候，调用函数的栈信息
};

}  // namespace muduo

#endif  // MUDUO_BASE_EXCEPTION_H
