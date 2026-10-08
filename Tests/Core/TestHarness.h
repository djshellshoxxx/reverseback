// Minimal dependency-free test harness shared by the core and integration tests.
#pragma once

// The allocation counter replaces global operator new/delete, which fights with the sanitizers' own allocator
// (and their checks make it redundant), so sanitizer builds run without it.
#if (defined(__SANITIZE_ADDRESS__) || defined(__SANITIZE_THREAD__)) && ! defined(RB_NO_ALLOC_HOOK)
 #define RB_NO_ALLOC_HOOK 1
#endif

#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <type_traits>
#include <vector>

namespace rbt
{
struct Case
{
    const char* name;
    void (*fn)();
};

inline std::vector<Case>& registry()
{
    static std::vector<Case> r;
    return r;
}

struct Registrar
{
    Registrar (const char* n, void (*f)()) { registry().push_back ({ n, f }); }
};

struct TestAbort {};   // thrown to stop the current test after a fatal failure

inline int& failureCount()
{
    static int f = 0;
    return f;
}

inline int& checkCount()
{
    static int c = 0;
    return c;
}

template <class T>
std::string toStr (const T& v)
{
    if constexpr (std::is_arithmetic_v<T>)
        return std::to_string (v);
    else if constexpr (std::is_enum_v<T>)
        return std::to_string (static_cast<long long> (v));
    else if constexpr (std::is_pointer_v<T>)
    {
        char buf[32];
        std::snprintf (buf, sizeof (buf), "%p", static_cast<const void*> (v));
        return buf;
    }
    else
        return "<value>";
}

inline void fail (const char* file, int line, const std::string& what)
{
    ++failureCount();
    std::printf ("    FAIL %s:%d  %s\n", file, line, what.c_str());
}

// Allocation instrumentation (implemented in AllocHook.cpp). Counts operator new calls made on a
// thread while a NoAlloc guard is alive on that thread.
extern std::atomic<long> allocViolations;
extern thread_local int noAllocDepth;

struct NoAlloc
{
    NoAlloc() { ++noAllocDepth; }
    ~NoAlloc() { --noAllocDepth; }
};

int runAll (int argc, char** argv);
}  // namespace rbt

#define RB_TEST(name)                                          \
    static void name();                                        \
    static ::rbt::Registrar rb_registrar_##name (#name, name); \
    static void name()

#define CHECK(cond)                                                      \
    do                                                                   \
    {                                                                    \
        ++::rbt::checkCount();                                           \
        if (! (cond))                                                    \
            ::rbt::fail (__FILE__, __LINE__, std::string ("CHECK ") + #cond); \
    } while (0)

#define CHECK_EQ(a, b)                                                                                     \
    do                                                                                                     \
    {                                                                                                      \
        ++::rbt::checkCount();                                                                             \
        const auto va_ = (a);                                                                              \
        const auto vb_ = (b);                                                                              \
        if (! (va_ == vb_))                                                                                \
            ::rbt::fail (__FILE__, __LINE__,                                                               \
                         std::string (#a " == " #b "  (") + ::rbt::toStr (va_) + " vs " + ::rbt::toStr (vb_) + ")"); \
    } while (0)

#define CHECK_NEAR(a, b, tol)                                                                              \
    do                                                                                                     \
    {                                                                                                      \
        ++::rbt::checkCount();                                                                             \
        const double va_ = static_cast<double> (a);                                                        \
        const double vb_ = static_cast<double> (b);                                                        \
        if (! (std::abs (va_ - vb_) <= static_cast<double> (tol)))                                         \
            ::rbt::fail (__FILE__, __LINE__,                                                               \
                         std::string (#a " ~= " #b "  (") + std::to_string (va_) + " vs " + std::to_string (vb_) + ")"); \
    } while (0)

#ifdef RB_NO_ALLOC_HOOK
#define CHECK_NO_ALLOC(stmt) do { stmt; } while (0)
#else
#define CHECK_NO_ALLOC(stmt)                                          \
    do                                                                \
    {                                                                 \
        const long before_ = ::rbt::allocViolations.load();           \
        {                                                             \
            ::rbt::NoAlloc guard_;                                    \
            stmt;                                                     \
        }                                                             \
        CHECK_EQ (::rbt::allocViolations.load(), before_);            \
    } while (0)
#endif
