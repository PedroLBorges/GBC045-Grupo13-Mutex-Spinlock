/*
 * platform_thread.h - Camada de abstracao de threads e sincronizacao.
 *
 * Uma unica API (prefixo pt_) traduzida para a API nativa de cada SO:
 *   - Linux / FreeBSD : POSIX Threads (pthread_create, pthread_mutex_t, pthread_cond_t)
 *   - Windows         : Win32 nativa (CreateThread, CRITICAL_SECTION, CONDITION_VARIABLE)
 *
 * Nao usa winpthreads. No Windows compila com MSVC (cl) ou MinGW-w64,
 * chamando apenas funcoes de <windows.h>.
 *
 * Convencao de retorno: 0 = sucesso, != 0 = erro (exceto onde indicado).
 */
#ifndef PLATFORM_THREAD_H
#define PLATFORM_THREAD_H

#include <stdint.h>

#if defined(_WIN32)
#  define PT_WINDOWS 1
#  ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#  endif
#  include <windows.h>
#else
#  define PT_POSIX 1
#  include <pthread.h>
#endif

#ifdef __cplusplus
extern "C" {
#endif

/* ------------------------------------------------------------------ */
/* Threads                                                             */
/* ------------------------------------------------------------------ */

typedef void *(*pt_thread_fn)(void *arg);

typedef struct pt_thread {
#if defined(PT_WINDOWS)
    HANDLE       handle;
    pt_thread_fn fn;    /* usados pelo trampolim: a assinatura do Win32 */
    void        *arg;   /* (DWORD WINAPI f(LPVOID)) difere da POSIX      */
    void        *ret;
#else
    pthread_t    handle;
#endif
} pt_thread_t;

/* Cria uma thread executando fn(arg).
 * ATENCAO: *t precisa continuar no mesmo endereco ate o pt_thread_join
 * (no Windows o trampolim guarda o ponteiro para t). */
int  pt_thread_create(pt_thread_t *t, pt_thread_fn fn, void *arg);

/* Espera a thread terminar. Se ret != NULL, recebe o valor retornado por fn. */
int  pt_thread_join(pt_thread_t *t, void **ret);

/* Cede o processador voluntariamente (sched_yield / SwitchToThread). */
void pt_thread_yield(void);

/* ------------------------------------------------------------------ */
/* Mutex: pthread_mutex_t  |  CRITICAL_SECTION                         */
/* ------------------------------------------------------------------ */

typedef struct pt_mutex {
#if defined(PT_WINDOWS)
    CRITICAL_SECTION cs;
#else
    pthread_mutex_t  m;
#endif
} pt_mutex_t;

int  pt_mutex_init(pt_mutex_t *m);
int  pt_mutex_destroy(pt_mutex_t *m);
int  pt_mutex_lock(pt_mutex_t *m);
int  pt_mutex_unlock(pt_mutex_t *m);
/* Retorna 0 se adquiriu o lock, != 0 se ja estava ocupado. */
int  pt_mutex_trylock(pt_mutex_t *m);

/* ------------------------------------------------------------------ */
/* Variavel de condicao: pthread_cond_t  |  CONDITION_VARIABLE         */
/* ------------------------------------------------------------------ */

typedef struct pt_cond {
#if defined(PT_WINDOWS)
    CONDITION_VARIABLE cv;
#else
    pthread_cond_t     c;
#endif
} pt_cond_t;

int  pt_cond_init(pt_cond_t *c);
int  pt_cond_destroy(pt_cond_t *c);
/* Sempre chamar dentro de um while que testa a condicao (despertar espurio). */
int  pt_cond_wait(pt_cond_t *c, pt_mutex_t *m);
int  pt_cond_signal(pt_cond_t *c);
int  pt_cond_broadcast(pt_cond_t *c);

/* ------------------------------------------------------------------ */
/* Spinlock (implementacao propria, test-and-test-and-set)             */
/*                                                                     */
/* O Win32 nao oferece spinlock em modo usuario, entao o mesmo         */
/* algoritmo e implementado nas duas plataformas, mudando apenas a     */
/* primitiva atomica nativa: InterlockedExchange no Windows e          */
/* __atomic_exchange_n (GCC/Clang) no Linux/FreeBSD. Assim a diferenca */
/* medida entre os SOs vem do escalonador, e nao do algoritmo.         */
/* A espera e ocupada pura: nunca cede a CPU ao SO.                    */
/* ------------------------------------------------------------------ */

typedef struct pt_spinlock {
#if defined(PT_WINDOWS)
    volatile LONG state;   /* 0 = livre, 1 = ocupado */
#else
    volatile int  state;
#endif
} pt_spinlock_t;

int  pt_spin_init(pt_spinlock_t *s);
int  pt_spin_destroy(pt_spinlock_t *s);
int  pt_spin_lock(pt_spinlock_t *s);
int  pt_spin_unlock(pt_spinlock_t *s);
/* Retorna 0 se adquiriu o lock, != 0 se ja estava ocupado. */
int  pt_spin_trylock(pt_spinlock_t *s);

/* ------------------------------------------------------------------ */
/* Utilitarios para os experimentos                                    */
/* ------------------------------------------------------------------ */

/* Relogio monotonico em nanossegundos
 * (clock_gettime(CLOCK_MONOTONIC) | QueryPerformanceCounter). */
uint64_t    pt_now_ns(void);

/* Tempo de CPU (usuario + kernel) consumido pelo processo, em ns
 * (getrusage | GetProcessTimes). */
uint64_t    pt_process_cpu_ns(void);

/* Numero de processadores logicos disponiveis. */
int         pt_cpu_count(void);

/* "linux", "freebsd", "windows" ou "outro". */
const char *pt_os_name(void);

#ifdef __cplusplus
}
#endif

#endif /* PLATFORM_THREAD_H */
