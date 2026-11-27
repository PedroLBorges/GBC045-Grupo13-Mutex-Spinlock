/*
 * test_platform.c - Programa de usuario que testa cada operacao da camada
 * platform_thread: threads, mutex, spinlock, variavel de condicao e
 * utilitarios. Sai com codigo 0 se tudo passou.
 */
#include "platform_thread.h"

#include <stdio.h>
#include <stdlib.h>

#define N_THREADS 8
#define N_ITERS   200000

static int g_failed = 0;

#define CHECK(cond, msg)                                              \
    do {                                                              \
        if (cond) { printf("  [ok]    %s\n", msg); }                  \
        else      { printf("  [FALHA] %s\n", msg); g_failed++; }      \
    } while (0)

/* ---------- 1. criacao e join, com passagem de argumento e retorno ---------- */

static void *square(void *arg)
{
    intptr_t v = (intptr_t)arg;
    return (void *)(v * v);
}

static void test_threads(void)
{
    pt_thread_t th[N_THREADS];
    int i, ok = 1;

    printf("threads (create / join / yield)\n");
    for (i = 0; i < N_THREADS; i++)
        ok &= (pt_thread_create(&th[i], square, (void *)(intptr_t)(i + 1)) == 0);
    CHECK(ok, "pt_thread_create cria 8 threads");

    ok = 1;
    for (i = 0; i < N_THREADS; i++) {
        void *ret = NULL;
        ok &= (pt_thread_join(&th[i], &ret) == 0);
        ok &= ((intptr_t)ret == (intptr_t)(i + 1) * (i + 1));
    }
    CHECK(ok, "pt_thread_join devolve o valor retornado por cada thread");

    pt_thread_yield();
    CHECK(1, "pt_thread_yield retorna");
}

/* ---------- 2. mutex: exclusao mutua e trylock ---------- */

static pt_mutex_t g_mutex;
static long       g_count_mutex;

static void *inc_mutex(void *arg)
{
    int i;
    (void)arg;
    for (i = 0; i < N_ITERS; i++) {
        pt_mutex_lock(&g_mutex);
        g_count_mutex++;
        pt_mutex_unlock(&g_mutex);
    }
    return NULL;
}

static void *try_mutex(void *arg)
{
    (void)arg;
    /* devolve o resultado do trylock feito por OUTRA thread */
    if (pt_mutex_trylock(&g_mutex) == 0) {
        pt_mutex_unlock(&g_mutex);
        return (void *)(intptr_t)0;
    }
    return (void *)(intptr_t)1;
}

static void test_mutex(void)
{
    pt_thread_t th[N_THREADS], t;
    void *ret = NULL;
    int i;

    printf("mutex (init / lock / unlock / trylock / destroy)\n");
    CHECK(pt_mutex_init(&g_mutex) == 0, "pt_mutex_init");

    g_count_mutex = 0;
    for (i = 0; i < N_THREADS; i++) pt_thread_create(&th[i], inc_mutex, NULL);
    for (i = 0; i < N_THREADS; i++) pt_thread_join(&th[i], NULL);
    CHECK(g_count_mutex == (long)N_THREADS * N_ITERS,
          "8 threads x 200000 incrementos: contador correto");

    pt_mutex_lock(&g_mutex);
    pt_thread_create(&t, try_mutex, NULL);
    pt_thread_join(&t, &ret);
    CHECK((intptr_t)ret != 0, "pt_mutex_trylock falha com o lock ocupado por outra thread");
    pt_mutex_unlock(&g_mutex);

    pt_thread_create(&t, try_mutex, NULL);
    pt_thread_join(&t, &ret);
    CHECK((intptr_t)ret == 0, "pt_mutex_trylock adquire o lock quando esta livre");

    CHECK(pt_mutex_destroy(&g_mutex) == 0, "pt_mutex_destroy");
}

/* ---------- 3. spinlock: exclusao mutua e trylock ---------- */

static pt_spinlock_t g_spin;
static long          g_count_spin;

static void *inc_spin(void *arg)
{
    int i;
    (void)arg;
    for (i = 0; i < N_ITERS; i++) {
        pt_spin_lock(&g_spin);
        g_count_spin++;
        pt_spin_unlock(&g_spin);
    }
    return NULL;
}

static void test_spinlock(void)
{
    pt_thread_t th[N_THREADS];
    int i;

    printf("spinlock (init / lock / unlock / trylock / destroy)\n");
    CHECK(pt_spin_init(&g_spin) == 0, "pt_spin_init");

    g_count_spin = 0;
    for (i = 0; i < N_THREADS; i++) pt_thread_create(&th[i], inc_spin, NULL);
    for (i = 0; i < N_THREADS; i++) pt_thread_join(&th[i], NULL);
    CHECK(g_count_spin == (long)N_THREADS * N_ITERS,
          "8 threads x 200000 incrementos: contador correto");

    CHECK(pt_spin_trylock(&g_spin) == 0, "pt_spin_trylock adquire o lock quando esta livre");
    CHECK(pt_spin_trylock(&g_spin) != 0, "pt_spin_trylock falha com o lock ocupado");
    CHECK(pt_spin_unlock(&g_spin) == 0,  "pt_spin_unlock");
    CHECK(pt_spin_trylock(&g_spin) == 0, "lock volta a ficar disponivel apos o unlock");
    pt_spin_unlock(&g_spin);

    CHECK(pt_spin_destroy(&g_spin) == 0, "pt_spin_destroy");
}

/* ---------- 4. variavel de condicao: signal (pingue-pongue) e broadcast ---------- */

#define PING_ROUNDS 10000

static pt_mutex_t g_cm;
static pt_cond_t  g_cc;
static int        g_turn;      /* 0 = vez da main, 1 = vez da outra thread */
static int        g_released;  /* para o teste de broadcast */
static int        g_woke;

static void *ponger(void *arg)
{
    int i;
    (void)arg;
    for (i = 0; i < PING_ROUNDS; i++) {
        pt_mutex_lock(&g_cm);
        while (g_turn != 1)              /* sempre while, nunca if */
            pt_cond_wait(&g_cc, &g_cm);
        g_turn = 0;
        pt_cond_signal(&g_cc);
        pt_mutex_unlock(&g_cm);
    }
    return NULL;
}

static void *waiter(void *arg)
{
    (void)arg;
    pt_mutex_lock(&g_cm);
    while (!g_released)
        pt_cond_wait(&g_cc, &g_cm);
    g_woke++;
    pt_mutex_unlock(&g_cm);
    return NULL;
}

static void test_cond(void)
{
    pt_thread_t t, th[N_THREADS];
    int i;

    printf("variavel de condicao (init / wait / signal / broadcast / destroy)\n");
    pt_mutex_init(&g_cm);
    CHECK(pt_cond_init(&g_cc) == 0, "pt_cond_init");

    g_turn = 0;
    pt_thread_create(&t, ponger, NULL);
    for (i = 0; i < PING_ROUNDS; i++) {
        pt_mutex_lock(&g_cm);
        g_turn = 1;
        pt_cond_signal(&g_cc);
        while (g_turn != 0)
            pt_cond_wait(&g_cc, &g_cm);
        pt_mutex_unlock(&g_cm);
    }
    pt_thread_join(&t, NULL);
    CHECK(g_turn == 0, "pt_cond_wait / pt_cond_signal: 10000 trocas de turno sem travar");

    g_released = 0;
    g_woke = 0;
    for (i = 0; i < N_THREADS; i++) pt_thread_create(&th[i], waiter, NULL);
    pt_mutex_lock(&g_cm);
    g_released = 1;
    pt_cond_broadcast(&g_cc);
    pt_mutex_unlock(&g_cm);
    for (i = 0; i < N_THREADS; i++) pt_thread_join(&th[i], NULL);
    CHECK(g_woke == N_THREADS, "pt_cond_broadcast acorda as 8 threads em espera");

    CHECK(pt_cond_destroy(&g_cc) == 0, "pt_cond_destroy");
    pt_mutex_destroy(&g_cm);
}

/* ---------- 5. utilitarios ---------- */

static void test_utils(void)
{
    uint64_t t0, t1, c0, c1;
    volatile uint64_t x = 0;
    uint64_t i;

    printf("utilitarios (relogio / tempo de CPU / numero de CPUs / nome do SO)\n");
    c0 = pt_process_cpu_ns();
    t0 = pt_now_ns();
    for (i = 0; i < 50000000ULL; i++) x += i;
    t1 = pt_now_ns();
    c1 = pt_process_cpu_ns();

    CHECK(t1 > t0, "pt_now_ns e monotonico e avanca");
    CHECK(c1 >= c0, "pt_process_cpu_ns nao retrocede");
    CHECK(pt_cpu_count() >= 1, "pt_cpu_count >= 1");
    printf("          so=%s  cpus=%d  laco=%.2f ms  cpu=%.2f ms\n",
           pt_os_name(), pt_cpu_count(),
           (double)(t1 - t0) / 1e6, (double)(c1 - c0) / 1e6);
}

int main(void)
{
    test_threads();
    test_mutex();
    test_spinlock();
    test_cond();
    test_utils();

    if (g_failed) {
        printf("\n%d teste(s) FALHARAM\n", g_failed);
        return 1;
    }
    printf("\ntodos os testes passaram\n");
    return 0;
}
