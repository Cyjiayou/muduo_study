// Use of this source code is governed by a BSD-style license
// that can be found in the License file.
//
// Author: Shuo Chen (chenshuo at chenshuo dot com)

#ifndef MUDUO_BASE_LOGGING_H
#define MUDUO_BASE_LOGGING_H

#include "muduo/base/LogStream.h"
#include "muduo/base/Timestamp.h"

namespace muduo
{

class TimeZone;

//日志等级
class Logger
{
 public:
  enum LogLevel
  {
    TRACE,
    DEBUG,
    INFO,
    WARN,
    ERROR,
    FATAL,
    NUM_LOG_LEVELS,
  };

  // compile time calculation of basename of source file
  //获取文件名，没看
  class SourceFile
  {
   public:
    template<int N>
    SourceFile(const char (&arr)[N])    //？
      : data_(arr),
        size_(N-1)
    {
      //找到路径中的文件名 
      //返回第二个参数最后一次出现的位置的指针
      const char* slash = strrchr(data_, '/'); // builtin function
      if (slash)
      {
        data_ = slash + 1;
        size_ -= static_cast<int>(data_ - arr);
      }
    }

    explicit SourceFile(const char* filename)
      : data_(filename)
    {
      const char* slash = strrchr(filename, '/');
      if (slash)
      {
        data_ = slash + 1;
      }
      size_ = static_cast<int>(strlen(data_));
    }

    const char* data_;
    int size_;
  };

  Logger(SourceFile file, int line);
  Logger(SourceFile file, int line, LogLevel level);
  Logger(SourceFile file, int line, LogLevel level, const char* func);
  Logger(SourceFile file, int line, bool toAbort);
  ~Logger();

  //返回 LogStream 对象的引用
  LogStream& stream() { return impl_.stream_; }

  static LogLevel logLevel();
  static void setLogLevel(LogLevel level);

  typedef void (*OutputFunc)(const char* msg, int len);
  typedef void (*FlushFunc)();
  static void setOutput(OutputFunc);
  static void setFlush(FlushFunc);
  static void setTimeZone(const TimeZone& tz);

 private:

class Impl
{
 public:
  typedef Logger::LogLevel LogLevel;
  //old_errno 是错误级别
  Impl(LogLevel level, int old_errno, const SourceFile& file, int line);
  void formatTime();
  void finish();

  Timestamp time_;      //提供当前时间，单位是微秒
  LogStream stream_;    
  LogLevel level_;      //日志级别
  int line_;            //日志行数
  SourceFile basename_; //日志文件名
};

  Impl impl_;     //Logger 类中只有这一个成员变量

};

extern Logger::LogLevel g_logLevel;   

inline Logger::LogLevel Logger::logLevel()
{
  return g_logLevel;
}

//
// CAUTION: do not write:
//
// if (good)
//   LOG_INFO << "Good news";
// else
//   LOG_WARN << "Bad news";
//
// this expends to
//
// if (good)
//   if (logging_INFO)
//     logInfoStream << "Good news";
//   else
//     logWarnStream << "Bad news";
//
/*
  日志输出，可以输出到标准输出，也可以输出到文件中
  默认级别是 INFO，TRACE 和 DEBUG 默认不输出
  if 条件根据当前级别的大小判断是否输出其他级别的日志
  __FILE__ 是文件名，__LINE__ 是行号， muduo::Logger::TRACE 是日志级别，__func__ 是函数名
  LOG_SYSERR 日志的级别实际上还是 ERROR
  LOG_SYSFATAL 日志的级别实际上是 FATAL
  stream() 说明重载了输出运算符，按照流的方式操作日志
  FATAL 级别的日志会强制程序退出

  过程：
    1.先调用 Logger 对应的构造函数，构造函数实际上是将参数传入给 Impl 对象
    2.Impl 对象会输出时间，线程名，日志级别，【错误信息】，【函数名】，
      我们在 << 添加的内容 到缓冲区中
    3.调用 Logger 的析构函数，析构函数会将缓冲区中的内容输出到文件缓冲区或者标准输出缓冲区
*/

#define LOG_TRACE if (muduo::Logger::logLevel() <= muduo::Logger::TRACE) \
  muduo::Logger(__FILE__, __LINE__, muduo::Logger::TRACE, __func__).stream()
#define LOG_DEBUG if (muduo::Logger::logLevel() <= muduo::Logger::DEBUG) \
  muduo::Logger(__FILE__, __LINE__, muduo::Logger::DEBUG, __func__).stream()
#define LOG_INFO if (muduo::Logger::logLevel() <= muduo::Logger::INFO) \
  muduo::Logger(__FILE__, __LINE__).stream()
#define LOG_WARN muduo::Logger(__FILE__, __LINE__, muduo::Logger::WARN).stream()
#define LOG_ERROR muduo::Logger(__FILE__, __LINE__, muduo::Logger::ERROR).stream()
#define LOG_FATAL muduo::Logger(__FILE__, __LINE__, muduo::Logger::FATAL).stream()
#define LOG_SYSERR muduo::Logger(__FILE__, __LINE__, false).stream()
#define LOG_SYSFATAL muduo::Logger(__FILE__, __LINE__, true).stream()

const char* strerror_tl(int savedErrno);

// Taken from glog/logging.h
//
// Check that the input is non NULL.  This very useful in constructor
// initializer lists.

#define CHECK_NOTNULL(val) \
  ::muduo::CheckNotNull(__FILE__, __LINE__, "'" #val "' Must be non NULL", (val))

// A small helper for CHECK_NOTNULL().
template <typename T>
T* CheckNotNull(Logger::SourceFile file, int line, const char *names, T* ptr)
{
  if (ptr == NULL)
  {
   Logger(file, line, Logger::FATAL).stream() << names;
  }
  return ptr;
}

}  // namespace muduo

#endif  // MUDUO_BASE_LOGGING_H
