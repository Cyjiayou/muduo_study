// Use of this source code is governed by a BSD-style license
// that can be found in the License file.
//
// Author: Shuo Chen (chenshuo at chenshuo dot com)

#ifndef MUDUO_BASE_LOGFILE_H
#define MUDUO_BASE_LOGFILE_H

#include "muduo/base/Mutex.h"
#include "muduo/base/Types.h"

#include <memory>

namespace muduo
{

namespace FileUtil
{
class AppendFile;
}

class LogFile : noncopyable
{
 public:
  LogFile(const string& basename,               //文件基本名称
          off_t rollSize,                       //一次最大刷新字节数
          bool threadSafe = true,               //通过对写入操作加锁，来决定是否线程安全
          int flushInterval = 3,                //隔多少毫秒刷新一次
          int checkEveryN = 1024);              //文件最大行数
  ~LogFile();

  void append(const char* logline, int len);
  void flush();
  bool rollFile();

 private:
  void append_unlocked(const char* logline, int len);

  static string getLogFileName(const string& basename, time_t* now);

  const string basename_;               //基本名
  const off_t rollSize_;                //一个文件中允许的最大字节数
  const int flushInterval_;             //刷新频率
  const int checkEveryN_;               //允许停留在 buffer 的最大日志行数

  int count_;                           //目前写入的行数

  std::unique_ptr<MutexLock> mutex_;    //封装的互斥锁
  time_t startOfPeriod_;                //开始记录日志时间（调整至零点时间）单位是秒
  time_t lastRoll_;                     //上一次滚动日志文件的时间
  time_t lastFlush_;                    //上一次刷新的时间
  
  std::unique_ptr<FileUtil::AppendFile> file_;  //文件缓冲区

  const static int kRollPerSeconds_ = 60*60*24;
};

}  // namespace muduo
#endif  // MUDUO_BASE_LOGFILE_H
