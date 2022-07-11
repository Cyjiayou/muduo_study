// Use of this source code is governed by a BSD-style license
// that can be found in the License file.
//
// Author: Shuo Chen (chenshuo at chenshuo dot com)

#ifndef MUDUO_BASE_LOGSTREAM_H
#define MUDUO_BASE_LOGSTREAM_H

#include "muduo/base/noncopyable.h"
#include "muduo/base/StringPiece.h"
#include "muduo/base/Types.h"
#include <assert.h>
#include <string.h> // memcpy

namespace muduo
{

namespace detail
{

const int kSmallBuffer = 4000;
const int kLargeBuffer = 4000*1000;

//缓冲区类
//非类型参数
template<int SIZE>
class FixedBuffer : noncopyable
{
 public:
  FixedBuffer()
    : cur_(data_)
  {
    setCookie(cookieStart);
  }

  ~FixedBuffer()
  {
    setCookie(cookieEnd);
  }

  //添加数据
  void append(const char* /*restrict*/ buf, size_t len)
  {
    // FIXME: append partially
    if (implicit_cast<size_t>(avail()) > len)
    {
      memcpy(cur_, buf, len);
      cur_ += len;
    }
  }

  const char* data() const { return data_; }
  //已经使用的空间大小
  int length() const { return static_cast<int>(cur_ - data_); }

  // write to data_ directly
  char* current() { return cur_; }
  //返回剩余空间
  int avail() const { return static_cast<int>(end() - cur_); }
  //调整 cur 指针的位置
  void add(size_t len) { cur_ += len; }

  //重置缓冲区
  void reset() { cur_ = data_; }
  //将数据清零。相当于 memset(data_, 0, sizeof(date_));
  void bzero() { memZero(data_, sizeof data_); }

  // for used by GDB
  //在 data_ 末尾添加 '\0'，变成字符串格式
  const char* debugString();
  void setCookie(void (*cookie)()) { cookie_ = cookie; }
  // for used by unit test
  //构造一个 string 对象
  string toString() const { return string(data_, length()); }
  StringPiece toStringPiece() const { return StringPiece(data_, length()); }

 private:
  //返回缓冲区的尾地址
  const char* end() const { return data_ + sizeof data_; }
  // Must be outline function for cookies.
  static void cookieStart();    //未定义函数体，没用
  static void cookieEnd();      //

  //函数指针
  void (*cookie_)();
  //缓冲区，通过模板传递大小
  char data_[SIZE];
  //当前指针，指向添加数据的下一个缓冲区地址 
  char* cur_;
};

}  // namespace detail


//重载 << 运算符
class LogStream : noncopyable
{
  //
  typedef LogStream self;
 public:
  //小缓冲区
  typedef detail::FixedBuffer<detail::kSmallBuffer> Buffer;

  self& operator<<(bool v)
  {
    buffer_.append(v ? "1" : "0", 1);
    return *this;
  }

  //当做 int/unsigned int 进行处理
  self& operator<<(short);
  self& operator<<(unsigned short);
  self& operator<<(int);
  self& operator<<(unsigned int);
  self& operator<<(long);
  self& operator<<(unsigned long);
  self& operator<<(long long);
  self& operator<<(unsigned long long);

  self& operator<<(const void*);

  self& operator<<(float v)
  {
    *this << static_cast<double>(v);
    return *this;
  }
  self& operator<<(double);
  // self& operator<<(long double);

  self& operator<<(char v)
  {
    buffer_.append(&v, 1);
    return *this;
  }

  // self& operator<<(signed char);
  // self& operator<<(unsigned char);

  self& operator<<(const char* str)
  {
    if (str)
    {
      buffer_.append(str, strlen(str));
    }
    else
    {
      buffer_.append("(null)", 6);
    }
    return *this;
  }

  self& operator<<(const unsigned char* str)
  {
    return operator<<(reinterpret_cast<const char*>(str));
  }

  self& operator<<(const string& v)
  {
    buffer_.append(v.c_str(), v.size());
    return *this;
  }

  self& operator<<(const StringPiece& v)
  {
    buffer_.append(v.data(), v.size());
    return *this;
  }

  self& operator<<(const Buffer& v)
  {
    *this << v.toStringPiece();
    return *this;
  }

  void append(const char* data, int len) { buffer_.append(data, len); }
  //返回缓存
  const Buffer& buffer() const { return buffer_; }
  void resetBuffer() { buffer_.reset(); }

 private:
  void staticCheck();

  template<typename T>
  void formatInteger(T);

  //缓冲区
  Buffer buffer_;

  static const int kMaxNumericSize = 48;
};

//将 val 按照 fmt 格式格式化到 buf 中
class Fmt // : noncopyable
{
 public:
  template<typename T>
  Fmt(const char* fmt, T val);

  const char* data() const { return buf_; }
  int length() const { return length_; }

 private:
  char buf_[32];
  int length_;
};

inline LogStream& operator<<(LogStream& s, const Fmt& fmt)
{
  s.append(fmt.data(), fmt.length());
  return s;
}

// Format quantity n in SI units (k, M, G, T, P, E).
// The returned string is atmost 5 characters long.
// Requires n >= 0
string formatSI(int64_t n);

// Format quantity n in IEC (binary) units (Ki, Mi, Gi, Ti, Pi, Ei).
// The returned string is atmost 6 characters long.
// Requires n >= 0
string formatIEC(int64_t n);

}  // namespace muduo

#endif  // MUDUO_BASE_LOGSTREAM_H
