// Use of this source code is governed by a BSD-style license
// that can be found in the License file.
//
// Author: Shuo Chen (chenshuo at chenshuo dot com)

#include "muduo/base/LogFile.h"

#include "muduo/base/FileUtil.h"
#include "muduo/base/ProcessInfo.h"

#include <assert.h>
#include <stdio.h>
#include <time.h>

using namespace muduo;

LogFile::LogFile(const string& basename,
                 off_t rollSize,
                 bool threadSafe,
                 int flushInterval,
                 int checkEveryN)
  : basename_(basename),
    rollSize_(rollSize),
    flushInterval_(flushInterval),
    checkEveryN_(checkEveryN),
    count_(0),
    mutex_(threadSafe ? new MutexLock : NULL),
    startOfPeriod_(0),
    lastRoll_(0),
    lastFlush_(0)
{
  assert(basename.find('/') == string::npos);   //如果不是一个目录，报错
  rollFile();
}

LogFile::~LogFile() = default;

void LogFile::append(const char* logline, int len)
{
  if (mutex_)
  {
    MutexLockGuard lock(*mutex_);
    append_unlocked(logline, len);
  }
  else
  {
    append_unlocked(logline, len);
  }
}

void LogFile::flush()
{
  if (mutex_)
  {
    MutexLockGuard lock(*mutex_);
    file_->flush();
  }
  else
  {
    file_->flush();
  }
}

void LogFile::append_unlocked(const char* logline, int len)
{
  //将字符串加入文件缓冲区中
  file_->append(logline, len);

  //如果文件写满了，就滚动日志
  //缓冲区写满了会自动 flush
  if (file_->writtenBytes() > rollSize_)
  {
    rollFile();
  }
  else
  {
    //增加行数
    ++count_;
    if (count_ >= checkEveryN_)
    {
      count_ = 0;
      time_t now = ::time(NULL);
      time_t thisPeriod_ = now / kRollPerSeconds_ * kRollPerSeconds_;
      //判断日期是不是同一天
      //如果不是同一天则创建新文件
      if (thisPeriod_ != startOfPeriod_)
      {
        rollFile();
      }
      //如果距离上次刷新时间超过 3 秒则刷盘
      else if (now - lastFlush_ > flushInterval_)
      {
        lastFlush_ = now;
        file_->flush();
      }
    }
  }
}

bool LogFile::rollFile()
{
  time_t now = 0;

  //filename 是目录名+日期+主机名+pid+.log
  string filename = getLogFileName(basename_, &now);
  
  //start 等价于 now - (now % kRollPerSeconds_)
  //将时间调整为当前零点
  time_t start = now / kRollPerSeconds_ * kRollPerSeconds_;

  if (now > lastRoll_)
  {
    lastRoll_ = now;
    lastFlush_ = now;
    startOfPeriod_ = start;   
    //改变 file_ 的内容
    file_.reset(new FileUtil::AppendFile(filename));    
    return true;
  }
  return false;
}

//文件名格式化
string LogFile::getLogFileName(const string& basename, time_t* now)
{
  string filename;
  //日期在格式化为字符串后，大小为 64
  filename.reserve(basename.size() + 64);
  filename = basename;

  char timebuf[32];
  struct tm tm;
  *now = time(NULL);
  //线程安全
  gmtime_r(now, &tm); // FIXME: localtime_r ?
  strftime(timebuf, sizeof timebuf, ".%Y%m%d-%H%M%S.", &tm);
  filename += timebuf;

  filename += ProcessInfo::hostname();

  char pidbuf[32];
  snprintf(pidbuf, sizeof pidbuf, ".%d", ProcessInfo::pid());
  filename += pidbuf;

  filename += ".log";

  return filename;
}

