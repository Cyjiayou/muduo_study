// Use of this source code is governed by a BSD-style license
// that can be found in the License file.
//
// Author: Shuo Chen (chenshuo at chenshuo dot com)

#include "muduo/base/CurrentThread.h"

#include <cxxabi.h>
#include <execinfo.h>
#include <stdlib.h>

namespace muduo
{
namespace CurrentThread
{
__thread int t_cachedTid = 0;
__thread char t_tidString[32];
__thread int t_tidStringLength = 6;
__thread const char* t_threadName = "unknown";
//std::is_same<T, C>::value 用来判断 T 和 C 是不是同一类型，返回值为 value
static_assert(std::is_same<int, pid_t>::value, "pid_t should be int");

string stackTrace(bool demangle)
{
  string stack;
  //地址最大数量
  const int max_frames = 200;
  //指针数组，用来保存地址
  void* frame[max_frames];
  //nptrs 就是实际的保存个数
  int nptrs = ::backtrace(frame, max_frames);
  //将这些地址转化为函数 string 也是一个指针数组，每个指针都指向一个字符串
  //表示函数的信息，注意每个指针都是 malloc 出来的，需要手动释放
  char** strings = ::backtrace_symbols(frame, nptrs);
  if (strings)
  {
    size_t len = 256;
    char* demangled = demangle ? static_cast<char*>(::malloc(len)) : nullptr;
    for (int i = 1; i < nptrs; ++i)  // skipping the 0-th, which is this function
    {
      if (demangle)
      {
        // https://panthema.net/2008/0901-stacktrace-demangled/
        // bin/exception_test(_ZN3Bar4testEv+0x79) [0x401909]
        char* left_par = nullptr;
        char* plus = nullptr;
        for (char* p = strings[i]; *p; ++p)
        {
          if (*p == '(')
            left_par = p;
          else if (*p == '+')
            plus = p;
        }

        if (left_par && plus)
        {
          *plus = '\0';
          int status = 0;
          char* ret = abi::__cxa_demangle(left_par+1, demangled, &len, &status);
          *plus = '+';
          if (status == 0)
          {
            demangled = ret;  // ret could be realloc()
            stack.append(strings[i], left_par+1);
            stack.append(demangled);
            stack.append(plus);
            stack.push_back('\n');
            continue;
          }
        }
      }
      // Fallback to mangled names
      //将函数信息放入到 stack 中
      //可以通过 demangled 函数将函数信息进行改写
      stack.append(strings[i]);
      stack.push_back('\n');
    }
    free(demangled);
    free(strings);    //手动释放每个 strings 指针
  }
  return stack;
}

}  // namespace CurrentThread
}  // namespace muduo
