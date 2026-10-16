#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <pthread.h>
#include <string>
#include <thread>

namespace {
struct TaskExit {};

struct TaskInfo {
  std::string name;
};

thread_local TaskInfo* t_self = nullptr;
TaskInfo s_main_task{"loopTask"};

// A counting semaphore with an owner-agnostic give, which is how the firmware
// uses its mutexes (taken in one function, given in a destructor).
struct Sem {
  std::mutex m;
  std::condition_variable cv;
  int count;
  explicit Sem(int c) : count(c) {}
};

std::mutex& critMutex(portMUX_TYPE* p) {
  static std::recursive_mutex dummy;
  (void)dummy;
  static std::mutex global_mutexes_guard;
  std::lock_guard<std::mutex> g(global_mutexes_guard);
  if (!p->m) p->m = new std::mutex();
  return *static_cast<std::mutex*>(p->m);
}
}  // namespace

void portENTER_CRITICAL(portMUX_TYPE* m) { critMutex(m).lock(); }
void portEXIT_CRITICAL(portMUX_TYPE* m) { critMutex(m).unlock(); }

BaseType_t xTaskCreatePinnedToCore(TaskFunction_t fn, const char* name, uint32_t stack_bytes,
                                   void* arg, UBaseType_t prio, TaskHandle_t* out, BaseType_t core) {
  (void)prio; (void)core;
  TaskInfo* info = new TaskInfo{name ? name : "task"};
  // A real thread with a stack at least as large as the ESP task asked for.
  pthread_attr_t attr;
  pthread_attr_init(&attr);
  size_t st = stack_bytes < 256 * 1024 ? 256 * 1024 : stack_bytes * 4;
  pthread_attr_setstacksize(&attr, st);
  pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
  struct Start { TaskFunction_t fn; void* arg; TaskInfo* info; };
  Start* s = new Start{fn, arg, info};
  pthread_t th;
  int r = pthread_create(&th, &attr, [](void* p) -> void* {
    Start* st = static_cast<Start*>(p);
    t_self = st->info;
    pthread_setname_np(pthread_self(), st->info->name.substr(0, 15).c_str());
    try {
      st->fn(st->arg);
    } catch (const TaskExit&) {
    }
    delete st->info;
    delete st;
    return nullptr;
  }, s);
  pthread_attr_destroy(&attr);
  if (r != 0) {
    delete info;
    delete s;
    return pdFAIL;
  }
  if (out) *out = info;
  return pdPASS;
}

BaseType_t xTaskCreate(TaskFunction_t fn, const char* name, uint32_t stack_bytes, void* arg,
                       UBaseType_t prio, TaskHandle_t* out) {
  return xTaskCreatePinnedToCore(fn, name, stack_bytes, arg, prio, out, tskNO_AFFINITY);
}

void vTaskDeleteSelf() {
  if (!t_self || t_self == &s_main_task) {
    fprintf(stderr, "vTaskDelete(NULL) on the main loop - ignored\n");
    for (;;) std::this_thread::sleep_for(std::chrono::hours(1));
  }
  throw TaskExit{};
}

void vTaskDeleteOther(TaskHandle_t) {
  fprintf(stderr, "vTaskDelete(other) is not supported on the Pi build\n");
}

void vTaskDelay(TickType_t ticks) { std::this_thread::sleep_for(std::chrono::milliseconds(ticks)); }

TickType_t xTaskGetTickCount() {
  static const auto t0 = std::chrono::steady_clock::now();
  return (TickType_t)std::chrono::duration_cast<std::chrono::milliseconds>(
      std::chrono::steady_clock::now() - t0).count();
}

TaskHandle_t xTaskGetCurrentTaskHandle() { return t_self ? t_self : &s_main_task; }
TaskHandle_t xTaskGetCurrentTaskHandleForCore(BaseType_t) { return xTaskGetCurrentTaskHandle(); }
UBaseType_t uxTaskGetStackHighWaterMark(TaskHandle_t) { return 64 * 1024; }
const char* pcTaskGetName(TaskHandle_t t) {
  TaskInfo* i = static_cast<TaskInfo*>(t ? t : xTaskGetCurrentTaskHandle());
  return i->name.c_str();
}
BaseType_t xPortGetCoreID() { return t_self ? 0 : 1; }

SemaphoreHandle_t xSemaphoreCreateMutex() { return new Sem(1); }
SemaphoreHandle_t xSemaphoreCreateRecursiveMutex() { return new Sem(1); }
SemaphoreHandle_t xSemaphoreCreateBinary() { return new Sem(0); }

BaseType_t xSemaphoreTake(SemaphoreHandle_t h, TickType_t ticks) {
  Sem* s = static_cast<Sem*>(h);
  if (!s) return pdFALSE;
  std::unique_lock<std::mutex> lk(s->m);
  if (ticks == portMAX_DELAY) {
    s->cv.wait(lk, [s] { return s->count > 0; });
  } else if (!s->cv.wait_for(lk, std::chrono::milliseconds(ticks), [s] { return s->count > 0; })) {
    return pdFALSE;
  }
  s->count--;
  return pdTRUE;
}

BaseType_t xSemaphoreGive(SemaphoreHandle_t h) {
  Sem* s = static_cast<Sem*>(h);
  if (!s) return pdFALSE;
  {
    std::lock_guard<std::mutex> lk(s->m);
    if (s->count >= 1) return pdFALSE;
    s->count++;
  }
  s->cv.notify_one();
  return pdTRUE;
}

void vSemaphoreDelete(SemaphoreHandle_t h) { delete static_cast<Sem*>(h); }
