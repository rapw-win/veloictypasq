// lust web build: wasm32-wasi has no threads, and its libc++ leaves out <mutex> / <thread> / <condition_variable>.
// These single-threaded stand-ins keep the shared sources compiling; the web build never starts a worker thread
// (the 3D preview renders synchronously) and model3d's ParFor sees one hardware thread.
#pragma once
#if defined(__wasi__) && !defined(LUST_COMPAT_THREADS)
#define LUST_COMPAT_THREADS
#include <__config>
#include <chrono>
#include <utility>
#include <mutex>
#include <thread>
#include <condition_variable>
namespace std {
struct mutex { void lock() {} void unlock() {} bool try_lock() { return true; } };
struct recursive_mutex { void lock() {} void unlock() {} bool try_lock() { return true; } };
struct condition_variable {
    template <class L, class P> void wait(L&, P pred) { (void)pred; }
    void notify_one() {}
    void notify_all() {}
};
class thread {
public:
    thread() = default;
    template <class F, class... A> explicit thread(F&& f, A&&... a) { std::forward<F>(f)(std::forward<A>(a)...); }
    thread(thread&&) = default;
    thread& operator=(thread&&) = default;
    bool joinable() const { return false; }
    void join() {}
    void detach() {}
    static unsigned hardware_concurrency() { return 1; }
};
} // namespace std
#endif
