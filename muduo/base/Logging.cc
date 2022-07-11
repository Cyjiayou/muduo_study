// Use of this source code is governed by a BSD-style license
// that can be found in the License file.
//
// Author: Shuo Chen (chenshuo at chenshuo dot com)

#include "muduo/base/Logging.h"

#include "muduo/base/CurrentThread.h"
#include "muduo/base/Timestamp.h"
#include "muduo/base/TimeZone.h"

#include <errno.h>
#include <stdio.h>
#include <string.h>

#include <sstream>

namespace muduo
{

/*
class LoggerImpl
{
 public:
  typedef Logger::LogLevel LogLevel;
  LoggerImpl(LogLevel level, int old_errno, const char* file, int line);
  void finish();

  Timestamp time_;
  LogStream stream_;
  LogLevel level_;
  int line_;
  const char* fullname_;
  const char* basename_;
};
*/

__thread char t_errnobuf[512];
__thread char t_time[64];       //格式化后的时间
__thread time_t t_lastSecond;   //每个线程所拥有的时间

const char* strerror_tl(int savedErrno)
{
  return strerror_r(savedErrno, t_errnobuf, sizeof t_errnobuf);
}

Logger::LogLevel initLogLevel()
{
  //获取环境变量，如果都没有则默认返回 INFO
  if (::getenv("MUDUO_LOG_TRACE"))
    return Logger::TRACE;
  else if (::getenv("MUDUO_LOG_DEBUG"))
    return Logger::DEBUG;
  else
    return Logger::INFO;
}

//定义日志级别
Logger::LogLevel g_logLevel = initLogLevel();

const char* LogLevelName[Logger::NUM_LOG_LEVELS] =
{
  "TRACE ",
  "DEBUG ",
  "INFO  ",
  "WARN  ",
  "ERROR ",
  "FATAL ",
};

// helper class for known string length at compile time
class T
{
 public:
  T(const char* str, unsigned len)
    :str_(str),
     len_(len)
  {
    assert(strlen(str) == len_);
  }

  const char* str_;
  const unsigned len_;
};

inline LogStream& operator<<(LogStream& s, T v)
{
  s.append(v.str_, v.len_);
  return s;
}

inline LogStream& operator<<(LogStream& s, const Logger::SourceFile& v)
{
  s.append(v.data_, v.size_);
  return s;
}

void defaultOutput(const char* msg, int len)
{
  size_t n = fwrite(msg, 1, len, stdout);
  //FIXME check n
  (void)n;
}

void defaultFlush()
{
  fflush(stdout);
}

//g_output 是函数指针
//默认是 defaultOutput 函数，输出到 stdout
//g_flush 是函数指针
//默认是 defaultFlush 函数，刷新标准输出缓冲区
Logger::OutputFunc g_output = defaultOutput;
Logger::FlushFunc g_flush = defaultFlush;
TimeZone g_logTimeZone;

}  // namespace muduo

using namespace muduo;

Logger::Impl::Impl(LogLevel level, int savedErrno, const SourceFile& file, int line)
  : time_(Timestamp::now()),
    stream_(),
    level_(level),
    line_(line),
    basename_(file)
{
  //输出时间到缓冲区
  formatTime();
  //缓存线程 tid【防止该线程没有获取过】
  CurrentThread::tid();
  //输出线程 tid
  stream_ << T(CurrentThread::tidString(), CurrentThread::tidStringLength());
  //输出日志级别
  stream_ << T(LogLevelName[level], 6);
  //如果 errno 不为 0，需要输出相应的信息
  if (savedErrno != 0)
  {
    stream_ << strerror_tl(savedErrno) << " (errno=" << savedErrno << ") ";
  }
}

void Logger::Impl::formatTime()
{
  //可以得到微秒数
  int64_t microSecondsSinceEpoch = time_.microSecondsSinceEpoch();
  //得到秒
  time_t seconds = static_cast<time_t>(microSecondsSinceEpoch / Timestamp::kMicroSecondsPerSecond);
  //得到微妙
  int microseconds = static_cast<int>(microSecondsSinceEpoch % Timestamp::kMicroSecondsPerSecond);
  //每个线程都拥有自己的 t_lastSecond，一开始为 NULL
  if (seconds != t_lastSecond)
  {
    t_lastSecond = seconds;
    struct tm tm_time;
    if (g_logTimeZone.valid())
    {
      tm_time = g_logTimeZone.toLocalTime(seconds);
    }
    else
    {
      ::gmtime_r(&seconds, &tm_time); // FIXME TimeZone::fromUtcTime
    }

    //格式化
    int len = snprintf(t_time, sizeof(t_time), "%4d%02d%02d %02d:%02d:%02d",
        tm_time.tm_year + 1900, tm_time.tm_mon + 1, tm_time.tm_mday,
        tm_time.tm_hour, tm_time.tm_min, tm_time.tm_sec);
    assert(len == 17); (void)len;
  }

  if (g_logTimeZone.valid())
  {
    Fmt us(".%06d ", microseconds);
    assert(us.length() == 8);
    stream_ << T(t_time, 17) << T(us.data(), 8);
  }
  else
  {
    //调用 Fmt 将毫秒格式化
    Fmt us(".%06dZ ", microseconds);
    assert(us.length() == 9);
    //输出，重载了插入运算符
    stream_ << T(t_time, 17) << T(us.data(), 9);
  }
}

void Logger::Impl::finish()
{
  stream_ << " - " << basename_ << ':' << line_ << '\n';
}

Logger::Logger(SourceFile file, int line)
  : impl_(INFO, 0, file, line)
{
}

Logger::Logger(SourceFile file, int line, LogLevel level, const char* func)
  : impl_(level, 0, file, line)
{
  //输出函数名称
  impl_.stream_ << func << ' ';
}

Logger::Logger(SourceFile file, int line, LogLevel level)
  : impl_(level, 0, file, line)
{
}

Logger::Logger(SourceFile file, int line, bool toAbort)
  : impl_(toAbort?FATAL:ERROR, errno, file, line)
{
}

Logger::~Logger()
{
  //这里有换行符，添加文件名和行号
  impl_.finish();
  //引用缓冲区的内容
  const LogStream::Buffer& buf(stream().buffer());
  //输出缓冲区的内容，默认输出到标准输出缓冲区
  g_output(buf.data(), buf.length());
  if (impl_.level_ == FATAL)
  {
    //如果是 FATAL，我们先要刷新一下缓冲区
    g_flush();
    abort();
  }
}

void Logger::setLogLevel(Logger::LogLevel level)
{
  g_logLevel = level;
}

//设置输出函数
void Logger::setOutput(OutputFunc out)
{
  g_output = out;
}


//设置刷新还书
void Logger::setFlush(FlushFunc flush)
{
  g_flush = flush;
}

void Logger::setTimeZone(const TimeZone& tz)
{
  g_logTimeZone = tz;
}
