/*
 * platform_thread.c - Implementacao da camada de abstracao.
 *
 * Cada funcao pt_* tem dois corpos, escolhidos em tempo de compilacao:
 * um com a API Win32 nativa e outro com POSIX Threads.
 */
#include "platform_thread.h"

#if defined(PT_POSIX)
#  include <sched.h>
#  include <time.h>
#  include <unistd.h>
#  include <sys/time.h>
#  include <sys/resource.h>
#endif

/* Dica para o processador de que estamos em espera ocupada (instrucao
 * PAUSE no x86, YIELD no ARM). Reduz consumo e libera o hyperthread irmao.
 * Nao e uma chamada de sistema: a thread continua na CPU. */
static void pt_cpu_relax(void)
{
#if defined(PT_WINDOWS)
    YieldProcessor();
#elif defined(__x86_64__) || defined(__i386__)
    __builtin_ia32_pause();
#elif defined(__aarch64__) || defined(__arm__)
    __asm__ __volatile__("yield");
#else
    /* sem dica disponivel nesta arquitetura */
#endif
}

/* ================================================================== */
/* Threads                                                             */
/* ================================================================== */

#if defined(PT_WINDOWS)

/* CreateThread exige DWORD WINAPI f(LPVOID); a API pt_ usa void *f(void *).
 * O trampolim adapta uma assinatura para a outra e guarda o retorno. */
static DWORD WINAPI pt_trampoline(LPVOID param)
{
    pt_thread_t *t = (pt_thread_t *)param;
    t->ret = t->fn(t->arg);
    return 0;
}

int pt_thread_create(pt_thread_t *t, pt_thread_fn fn, void *arg)
{
    t->fn  = fn;
    t->arg = arg;
    t->ret = NULL;
    t->handle = CreateThread(NULL, 0, pt_trampoline, t, 0, NULL);
    return t->handle == NULL ? (int)GetLastError() : 0;
}

int pt_thread_join(pt_thread_t *t, void **ret)
{
    if (WaitForSingleObject(t->handle, INFINITE) != WAIT_OBJECT_0)
        return (int)GetLastError();
    CloseHandle(t->handle);
    t->handle = NULL;
    if (ret) *ret = t->ret;
    return 0;
}

void pt_thread_yield(void)
{
    SwitchToThread();
}

#else /* POSIX */

int pt_thread_create(pt_thread_t *t, pt_thread_fn fn, void *arg)
{
    return pthread_create(&t->handle, NULL, fn, arg);
}

int pt_thread_join(pt_thread_t *t, void **ret)
{
    return pthread_join(t->handle, ret);
}

void pt_thread_yield(void)
{
    sched_yield();
}

#endif

/* ================================================================== */
/* Mutex                                                               */
/* ================================================================== */

#if defined(PT_WINDOWS)

int pt_mutex_init(pt_mutex_t *m)
{
    /* Spin count padrao (0): a CRITICAL_SECTION bloqueia direto no kernel
     * quando ha contencao, comportamento analogo ao pthread_mutex padrao. */
    InitializeCriticalSection(&m->cs);
    return 0;
}

int pt_mutex_destroy(pt_mutex_t *m) { DeleteCriticalSection(&m->cs); return 0; }
int pt_mutex_lock(pt_mutex_t *m)    { EnterCriticalSection(&m->cs);  return 0; }
int pt_mutex_unlock(pt_mutex_t *m)  { LeaveCriticalSection(&m->cs);  return 0; }

int pt_mutex_trylock(pt_mutex_t *m)
{
    return TryEnterCriticalSection(&m->cs) ? 0 : 1;
}

#else /* POSIX */

int pt_mutex_init(pt_mutex_t *m)    { return pthread_mutex_init(&m->m, NULL); }
int pt_mutex_destroy(pt_mutex_t *m) { return pthread_mutex_destroy(&m->m); }
int pt_mutex_lock(pt_mutex_t *m)    { return pthread_mutex_lock(&m->m); }
int pt_mutex_unlock(pt_mutex_t *m)  { return pthread_mutex_unlock(&m->m); }
int pt_mutex_trylock(pt_mutex_t *m) { return pthread_mutex_trylock(&m->m); }

#endif

/* ================================================================== */
/* Variavel de condicao                                                */
/* ================================================================== */

#if defined(PT_WINDOWS)

int pt_cond_init(pt_cond_t *c)    { InitializeConditionVariable(&c->cv); return 0; }
int pt_cond_destroy(pt_cond_t *c) { (void)c; return 0; } /* Win32 nao tem destroy */

int pt_cond_wait(pt_cond_t *c, pt_mutex_t *m)
{
    return SleepConditionVariableCS(&c->cv, &m->cs, INFINITE) ? 0 : (int)GetLastError();
}

int pt_cond_signal(pt_cond_t *c)    { WakeConditionVariable(&c->cv);    return 0; }
int pt_cond_broadcast(pt_cond_t *c) { WakeAllConditionVariable(&c->cv); return 0; }

#else /* POSIX */

int pt_cond_init(pt_cond_t *c)      { return pthread_cond_init(&c->c, NULL); }
int pt_cond_destroy(pt_cond_t *c)   { return pthread_cond_destroy(&c->c); }
int pt_cond_wait(pt_cond_t *c, pt_mutex_t *m) { return pthread_cond_wait(&c->c, &m->m); }
int pt_cond_signal(pt_cond_t *c)    { return pthread_cond_signal(&c->c); }
int pt_cond_broadcast(pt_cond_t *c) { return pthread_cond_broadcast(&c->c); }

#endif

/* ================================================================== */
/* Spinlock (test-and-test-and-set)                                    */
/* ================================================================== */

/* Troca atomica: grava 1 e devolve o valor anterior.
 * Devolver 0 significa que o lock estava livre e agora e nosso. */
static int pt_spin_exchange(pt_spinlock_t *s)
{
#if defined(PT_WINDOWS)
    return (int)InterlockedExchange(&s->state, 1);
#else
    return __atomic_exchange_n(&s->state, 1, __ATOMIC_ACQUIRE);
#endif
}

int pt_spin_init(pt_spinlock_t *s)    { s->state = 0; return 0; }
int pt_spin_destroy(pt_spinlock_t *s) { (void)s; return 0; }

int pt_spin_lock(pt_spinlock_t *s)
{
    for (;;) {
        /* "test-and-set": tenta pegar o lock com uma troca atomica. */
        if (pt_spin_exchange(s) == 0)
            return 0;
        /* "test": enquanto estiver ocupado, apenas le (sem escrever),
         * para nao ficar invalidando a linha de cache dos outros nucleos. */
        while (s->state != 0)
            pt_cpu_relax();
    }
}

int pt_spin_trylock(pt_spinlock_t *s)
{
    return pt_spin_exchange(s) == 0 ? 0 : 1;
}

int pt_spin_unlock(pt_spinlock_t *s)
{
#if defined(PT_WINDOWS)
    InterlockedExchange(&s->state, 0);
#else
    __atomic_store_n(&s->state, 0, __ATOMIC_RELEASE);
#endif
    return 0;
}

/* ================================================================== */
/* Utilitarios                                                         */
/* ================================================================== */

#if defined(PT_WINDOWS)

uint64_t pt_now_ns(void)
{
    static LARGE_INTEGER freq; /* zero na primeira chamada */
    LARGE_INTEGER now;
    uint64_t f, c;
    if (freq.QuadPart == 0)
        QueryPerformanceFrequency(&freq);
    QueryPerformanceCounter(&now);
    f = (uint64_t)freq.QuadPart;
    c = (uint64_t)now.QuadPart;
    /* separa parte inteira e resto para nao estourar 64 bits */
    return (c / f) * 1000000000ULL + (c % f) * 1000000000ULL / f;
}

uint64_t pt_process_cpu_ns(void)
{
    FILETIME creation, exit_time, kernel, user;
    ULARGE_INTEGER k, u;
    if (!GetProcessTimes(GetCurrentProcess(), &creation, &exit_time, &kernel, &user))
        return 0;
    k.LowPart = kernel.dwLowDateTime; k.HighPart = kernel.dwHighDateTime;
    u.LowPart = user.dwLowDateTime;   u.HighPart = user.dwHighDateTime;
    return (uint64_t)(k.QuadPart + u.QuadPart) * 100ULL; /* FILETIME = 100 ns */
}

int pt_cpu_count(void)
{
    SYSTEM_INFO si;
    GetSystemInfo(&si);
    return (int)si.dwNumberOfProcessors;
}

const char *pt_os_name(void) { return "windows"; }

#else /* POSIX */

uint64_t pt_now_ns(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ULL + (uint64_t)ts.tv_nsec;
}

uint64_t pt_process_cpu_ns(void)
{
    struct rusage ru;
    if (getrusage(RUSAGE_SELF, &ru) != 0)
        return 0;
    return ((uint64_t)ru.ru_utime.tv_sec + (uint64_t)ru.ru_stime.tv_sec) * 1000000000ULL
         + ((uint64_t)ru.ru_utime.tv_usec + (uint64_t)ru.ru_stime.tv_usec) * 1000ULL;
}

int pt_cpu_count(void)
{
    long n = sysconf(_SC_NPROCESSORS_ONLN);
    return n > 0 ? (int)n : 1;
}

const char *pt_os_name(void)
{
#if defined(__linux__)
    return "linux";
#elif defined(__FreeBSD__)
    return "freebsd";
#else
    return "outro";
#endif
}

#endif
