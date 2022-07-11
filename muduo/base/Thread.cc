// Use of this source code is governed by a BSD-style license
// that can be found in the License file.
//
// Author: Shuo Chen (chenshuo at chenshuo dot com)

#include "muduo/base/Thread.h"
#include "muduo/base/CurrentThread.h"
#include "muduo/base/Exception.h"
#include "muduo/base/Logging.h"

#include <type_traits>

#include <errno.h>
#include <stdio.h>
#include <unistd.h>
#include <sys/prctl.h>
#include <sys/syscall.h>
#include <sys/types.h>
#include <linux/unistd.h>

namespace muduo
{
namespace detail
{

//获取线程真正 id
pid_t gettid()
{
  return static_cast<pid_t>(::syscall(SYS_gettid));
}

void afterFork()
{
  //子进程的 tid 为 0，名称为 main
  muduo::CurrentThread::t_cachedTid = 0;
  muduo::CurrentThread::t_threadName = "main";
  //缓存 tid
  CurrentThread::tid();
  // no need to call pthread_atfork(NULL, NULL, &afterFork);
}

//先调用 fork 之前可能有多个线程，fork 可能在主线程中调用，也可能在子线程中调用
//如果在子线程中调用 fork 的话，新进程中只有执行序列【线程】，调用 fork 的线程被继承下来
//我们要将该线程设置为新进程的主线程
class ThreadNameInitializer
{
 public:
  ThreadNameInitializer()
  {
    //主线程的名称为 main
    muduo::CurrentThread::t_threadName = "main";
    //缓存当前线程的 tid
    CurrentThread::tid();
    //int pthread_atfork(void (*prepare)(void), void (*parent)(void), void (*child)(void));
    //调用 fork 时，内部创建子进程前在父进程中会调用 prepare，内部创建子进程成功后，父会调用 parent，子进程会调用 child
    //如果我们调用 fork 函数，子进程就会调用 afterFork 函数
    pthread_atfork(NULL, NULL, &afterFork);
  }
};

ThreadNameInitializer init;   //全局变量，自动调用 ThreadNameInitializer 的构造函数

struct ThreadData
{
  typedef muduo::Thread::ThreadFunc ThreadFunc;
  ThreadFunc func_;
  string name_;
  pid_t* tid_;
  CountDownLatch* latch_;

  ThreadData(ThreadFunc func,
             const string& name,
             pid_t* tid,
             CountDownLatch* latch)
    : func_(std::move(func)),
      name_(name),
      tid_(tid),
      latch_(latch)
  { }

  void runInThread()
  {
    //tid 指向的地址是 class thread 的 tid
    //每次运行 tid 的时候都会判断这个线程是不是第一次使用
    //如果不是第一次，则就说明他的 tid 已经存在
    //否则就要调用系统调用返回 tid
    *tid_ = muduo::CurrentThread::tid();
    //赋值后，tid 就没用了
    tid_ = NULL;
    latch_->countDown();
    latch_ = NULL;

    //更新线程名缓存
    muduo::CurrentThread::t_threadName = name_.empty() ? "muduoThread" : name_.c_str();
    ::prctl(PR_SET_NAME, muduo::CurrentThread::t_threadName);
    try
    {
      func_();    //调用 func_() 函数
      muduo::CurrentThread::t_threadName = "finished";
    }
    catch (const Exception& ex)
    {
      muduo::CurrentThread::t_threadName = "crashed";
      fprintf(stderr, "exception caught in Thread %s\n", name_.c_str());
      fprintf(stderr, "reason: %s\n", ex.what());
      fprintf(stderr, "stack trace: %s\n", ex.stackTrace());
      abort();
    }
    catch (const std::exception& ex)
    {
      muduo::CurrentThread::t_threadName = "crashed";
      fprintf(stderr, "exception caught in Thread %s\n", name_.c_str());
      fprintf(stderr, "reason: %s\n", ex.what());
      abort();
    }
    catch (...)
    {
      muduo::CurrentThread::t_threadName = "crashed";
      fprintf(stderr, "unknown exception caught in Thread %s\n", name_.c_str());
      throw; // rethrow
    }
  }
};

void* startThread(void* obj)
{
  ThreadData* data = static_cast<ThreadData*>(obj);
  data->runInThread();
  //ThreadData 是一个中介对象，执行完 runInThread 就没用了，释放
  delete data;
  return NULL;
}

}  // namespace detail


//获取线程的 tid，并且格式化
void CurrentThread::cacheTid()
{
  if (t_cachedTid == 0)
  {
    t_cachedTid = detail::gettid();
    t_tidStringLength = snprintf(t_tidString, sizeof t_tidString, "%5d ", t_cachedTid);
  }
}


bool CurrentThread::isMainThread()
{
  return tid() == ::getpid();
}

void CurrentThread::sleepUsec(int64_t usec)
{
  struct timespec ts = { 0, 0 };
  ts.tv_sec = static_cast<time_t>(usec / Timestamp::kMicroSecondsPerSecond);
  ts.tv_nsec = static_cast<long>(usec % Timestamp::kMicroSecondsPerSecond * 1000);
  ::nanosleep(&ts, NULL);
}

AtomicInt32 Thread::numCreated_;    //numCreated 的值默认是 0

Thread::Thread(ThreadFunc func, const string& n)
  : started_(false),
    joined_(false),
    pthreadId_(0),
    tid_(0),
    func_(std::move(func)),
    name_(n),
    latch_(1)
{
  //让线程个数原子性加 1
  //如果名字为空，则使用 Thread+numCreated_的值来确定
  setDefaultName();
}

Thread::~Thread()
{
  //如果线程在运行且没有等待，则等待线程结束
  if (started_ && !joined_)
  {
    pthread_detach(pthreadId_);
  }
}

void Thread::setDefaultName()
{
  int num = numCreated_.incrementAndGet();
  if (name_.empty())
  {
    char buf[32];
    snprintf(buf, sizeof buf, "Thread%d", num);
    name_ = buf;
  }
}

void Thread::start()
{
  assert(!started_);
  started_ = true;
  // FIXME: move(func_)
  detail::ThreadData* data = new detail::ThreadData(func_, name_, &tid_, &latch_);
  //线程运行失败
  if (pthread_create(&pthreadId_, NULL, &detail::startThread, data))
  {
    started_ = false;
    delete data; // or no delete?
    LOG_SYSFATAL << "Failed in pthread_create";
  }
  else
  {
    latch_.wait();
    assert(tid_ > 0);
  }
}

int Thread::join()
{
  assert(started_);
  assert(!joined_);
  joined_ = true;
  return pthread_join(pthreadId_, NULL);      //phread_join 等待线程结束，会阻塞线程
}

}  // namespace muduo
