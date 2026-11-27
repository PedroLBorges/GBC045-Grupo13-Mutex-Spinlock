/*
 * bench.c - Benchmark do Tema 1: mutex x spinlock.
 *
 * N threads executam, juntas, um total fixo de aquisicoes do lock. Em cada
 * aquisicao a thread entra na secao critica, incrementa um contador
 * compartilhado, executa um trabalho (nenhum, ~1 us ou ~100 us) e sai.
 *
 * Matriz de configuracoes:
 *   primitiva : mutex, spinlock
 *   threads   : 1, 2, 4, 8, 16
 *   secao     : curta (so o incremento), media (~1 us), longa (~100 us)
 *
 * Cada configuracao e repetida --reps vezes (padrao 30). Cada repeticao
 * gera uma linha no CSV, gravada imediatamente (dados brutos).
 *
 * Uso:
 *   bench --out dados/linux.csv
 *   bench --out dados/teste.csv --quick      (teste rapido, NAO usar no relatorio)
 */
#include "platform_thread.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define MAX_THREADS 64

enum { PRIM_MUTEX = 0, PRIM_SPIN = 1, PRIM_COUNT = 2 };
enum { CS_SHORT = 0, CS_MEDIUM = 1, CS_LONG = 2, CS_COUNT = 3 };

static const char *PRIM_NAME[PRIM_COUNT] = { "mutex", "spinlock" };
static const char *CS_NAME[CS_COUNT]     = { "curta", "media", "longa" };
static const int   THREADS[]             = { 1, 2, 4, 8, 16 };
#define THREADS_COUNT ((int)(sizeof(THREADS) / sizeof(THREADS[0])))

/* ---------------- estado compartilhado de uma execucao ---------------- */

static pt_mutex_t    g_mutex;     /* lock sob teste (mutex)    */
static pt_spinlock_t g_spin;      /* lock sob teste (spinlock) */
static uint64_t      g_counter;   /* protegido pelo lock sob teste */

/* "Portao" de largada: todas as threads esperam aqui ate a main liberar,
 * para que o tempo de criacao das threads nao entre na medicao. */
static pt_mutex_t g_gate_m;
static pt_cond_t  g_gate_c;
static int        g_ready;
static int        g_go;

typedef struct {
    int      prim;        /* PRIM_MUTEX ou PRIM_SPIN                    */
    uint64_t quota;       /* aquisicoes que esta thread deve fazer      */
    uint64_t work_iters;  /* iteracoes de trabalho dentro da secao      */
} worker_arg_t;

#if defined(_MSC_VER)
#  define PT_NOINLINE __declspec(noinline)
#else
#  define PT_NOINLINE __attribute__((noinline))
#endif

/* Trabalho de CPU dentro da secao critica. O volatile impede o compilador
 * de eliminar o laco. A quantidade de iteracoes e calibrada em tempo de
 * execucao para durar ~1 us ou ~100 us na maquina em que roda.
 * NOINLINE: garante que a calibracao, o mutex e o spinlock executem
 * exatamente o mesmo codigo de maquina (sem isso o compilador gera uma
 * copia otimizada de forma diferente em cada ponto de chamada, e a secao
 * critica de uma primitiva fica mais longa que a da outra). */
static PT_NOINLINE void busy_work(uint64_t iters)
{
    volatile uint64_t x = 0;
    uint64_t i;
    for (i = 0; i < iters; i++)
        x += i;
}

static void *worker(void *p)
{
    worker_arg_t *a = (worker_arg_t *)p;
    uint64_t i;

    /* avisa que esta pronta e espera a largada */
    pt_mutex_lock(&g_gate_m);
    g_ready++;
    pt_cond_broadcast(&g_gate_c);
    while (!g_go)
        pt_cond_wait(&g_gate_c, &g_gate_m);
    pt_mutex_unlock(&g_gate_m);

    if (a->prim == PRIM_MUTEX) {
        for (i = 0; i < a->quota; i++) {
            pt_mutex_lock(&g_mutex);
            g_counter++;
            if (a->work_iters) busy_work(a->work_iters);
            pt_mutex_unlock(&g_mutex);
        }
    } else {
        for (i = 0; i < a->quota; i++) {
            pt_spin_lock(&g_spin);
            g_counter++;
            if (a->work_iters) busy_work(a->work_iters);
            pt_spin_unlock(&g_spin);
        }
    }
    return NULL;
}

/* Executa uma repeticao de uma configuracao.
 * Devolve 0 se o contador final bate com o total (exclusao mutua ok). */
static int run_once(int prim, int nthreads, uint64_t total, uint64_t work_iters,
                    uint64_t *elapsed_ns, uint64_t *cpu_ns)
{
    pt_thread_t  th[MAX_THREADS];
    worker_arg_t args[MAX_THREADS];
    uint64_t t0, t1, c0, c1;
    int i;

    pt_mutex_init(&g_mutex);
    pt_spin_init(&g_spin);
    pt_mutex_init(&g_gate_m);
    pt_cond_init(&g_gate_c);
    g_counter = 0;
    g_ready   = 0;
    g_go      = 0;

    for (i = 0; i < nthreads; i++) {
        args[i].prim       = prim;
        args[i].work_iters = work_iters;
        /* divide o total; o resto vai para as primeiras threads */
        args[i].quota = total / (uint64_t)nthreads
                      + ((uint64_t)i < total % (uint64_t)nthreads ? 1 : 0);
        if (pt_thread_create(&th[i], worker, &args[i]) != 0) {
            fprintf(stderr, "erro: pt_thread_create falhou\n");
            exit(1);
        }
    }

    /* espera todas ficarem prontas e da a largada */
    pt_mutex_lock(&g_gate_m);
    while (g_ready < nthreads)
        pt_cond_wait(&g_gate_c, &g_gate_m);
    c0 = pt_process_cpu_ns();
    t0 = pt_now_ns();
    g_go = 1;
    pt_cond_broadcast(&g_gate_c);
    pt_mutex_unlock(&g_gate_m);

    for (i = 0; i < nthreads; i++)
        pt_thread_join(&th[i], NULL);
    t1 = pt_now_ns();
    c1 = pt_process_cpu_ns();

    *elapsed_ns = t1 - t0;
    *cpu_ns     = c1 - c0;

    pt_cond_destroy(&g_gate_c);
    pt_mutex_destroy(&g_gate_m);
    pt_spin_destroy(&g_spin);
    pt_mutex_destroy(&g_mutex);

    return g_counter == total ? 0 : 1;
}

/* Mede quantos ns custa uma iteracao de busy_work (melhor de 7 tentativas,
 * para descartar tentativas interrompidas pelo SO). */
static double calibrate_ns_per_iter(void)
{
    const uint64_t iters = 5000000ULL;
    double best = 1e18;
    int k;
    for (k = 0; k < 7; k++) {
        uint64_t t0 = pt_now_ns();
        busy_work(iters);
        {
            double per = (double)(pt_now_ns() - t0) / (double)iters;
            if (per < best) best = per;
        }
    }
    return best;
}

static int file_exists(const char *path)
{
    FILE *f = fopen(path, "r");
    if (f) { fclose(f); return 1; }
    return 0;
}

static void usage(const char *prog)
{
    fprintf(stderr,
        "uso: %s --out arquivo.csv [opcoes]\n"
        "  --reps N           repeticoes por configuracao (padrao 30)\n"
        "  --warmup N         passadas de aquecimento descartadas (padrao 1)\n"
        "  --total-curta N    aquisicoes na secao curta  (padrao 2000000)\n"
        "  --total-media N    aquisicoes na secao media  (padrao 500000)\n"
        "  --total-longa N    aquisicoes na secao longa  (padrao 5000)\n"
        "  --quick            teste rapido: 3 repeticoes, totais / 10\n"
        "  --force            sobrescreve o CSV se ja existir\n", prog);
}

int main(int argc, char **argv)
{
    const char *out = NULL;
    int reps = 30, warmup = 1, quick = 0, force = 0;
    uint64_t total[CS_COUNT] = { 2000000ULL, 500000ULL, 5000ULL };
    uint64_t work[CS_COUNT];
    double target_ns[CS_COUNT] = { 0.0, 1000.0, 100000.0 };
    double ns_per_iter;
    char meta_path[1024];
    FILE *csv, *meta;
    int ncpu, rep, cs, prim, ti, i, failures = 0;
    time_t now;

    for (i = 1; i < argc; i++) {
        if      (!strcmp(argv[i], "--out")         && i + 1 < argc) out = argv[++i];
        else if (!strcmp(argv[i], "--reps")        && i + 1 < argc) reps = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--warmup")      && i + 1 < argc) warmup = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--total-curta") && i + 1 < argc) total[CS_SHORT]  = strtoull(argv[++i], NULL, 10);
        else if (!strcmp(argv[i], "--total-media") && i + 1 < argc) total[CS_MEDIUM] = strtoull(argv[++i], NULL, 10);
        else if (!strcmp(argv[i], "--total-longa") && i + 1 < argc) total[CS_LONG]   = strtoull(argv[++i], NULL, 10);
        else if (!strcmp(argv[i], "--quick")) quick = 1;
        else if (!strcmp(argv[i], "--force")) force = 1;
        else { usage(argv[0]); return 2; }
    }
    if (!out || reps < 1 || warmup < 0) { usage(argv[0]); return 2; }
    if (quick) {
        reps = 3;
        for (cs = 0; cs < CS_COUNT; cs++) total[cs] /= 10;
    }
    if (file_exists(out) && !force) {
        fprintf(stderr, "erro: %s ja existe. Use outro nome ou --force "
                        "(dados brutos nao devem ser sobrescritos por engano).\n", out);
        return 2;
    }

    ncpu = pt_cpu_count();
    (void)pt_now_ns(); /* inicializa o relogio antes de criar threads */

    /* calibra o trabalho da secao critica para esta maquina */
    ns_per_iter = calibrate_ns_per_iter();
    for (cs = 0; cs < CS_COUNT; cs++) {
        work[cs] = (uint64_t)(target_ns[cs] / ns_per_iter + 0.5);
        if (target_ns[cs] > 0.0 && work[cs] == 0) work[cs] = 1;
    }

    csv = fopen(out, "w");
    if (!csv) { fprintf(stderr, "erro: nao consegui criar %s\n", out); return 1; }
    fprintf(csv, "os,ncpu,primitiva,secao,threads,rep,total_aquisicoes,"
                 "tempo_ns,vazao_ops_s,cpu_ns,contador_ok\n");
    fflush(csv);

    /* arquivo de metadados ao lado do CSV: descreve como os dados foram gerados */
    snprintf(meta_path, sizeof(meta_path), "%s.meta.txt", out);
    meta = fopen(meta_path, "w");
    if (meta) {
        now = time(NULL);
        fprintf(meta, "data: %s", ctime(&now));
        fprintf(meta, "os: %s\nncpu: %d\nreps: %d\nwarmup: %d\nquick: %d\n",
                pt_os_name(), ncpu, reps, warmup, quick);
        fprintf(meta, "calibracao_ns_por_iteracao: %.4f\n", ns_per_iter);
        for (cs = 0; cs < CS_COUNT; cs++)
            fprintf(meta, "secao_%s: alvo_ns=%.0f iteracoes=%llu total_aquisicoes=%llu\n",
                    CS_NAME[cs], target_ns[cs],
                    (unsigned long long)work[cs], (unsigned long long)total[cs]);
        fclose(meta);
    }

    fprintf(stderr, "os=%s ncpu=%d reps=%d warmup=%d  calibracao=%.3f ns/iter\n",
            pt_os_name(), ncpu, reps, warmup, ns_per_iter);

    /* A repeticao e o laco externo: cada passada percorre todas as
     * configuracoes. Assim, ruido que varia ao longo do tempo (temperatura,
     * processos de fundo) se espalha por todas elas em vez de cair em uma so.
     * Repeticoes negativas sao aquecimento e nao vao para o CSV. */
    for (rep = -warmup; rep < reps; rep++) {
        for (cs = 0; cs < CS_COUNT; cs++) {
            for (prim = 0; prim < PRIM_COUNT; prim++) {
                for (ti = 0; ti < THREADS_COUNT; ti++) {
                    uint64_t elapsed = 0, cpu = 0;
                    int n   = THREADS[ti];
                    int bad = run_once(prim, n, total[cs], work[cs], &elapsed, &cpu);
                    double thr = elapsed ? (double)total[cs] * 1e9 / (double)elapsed : 0.0;

                    if (bad) failures++;
                    if (rep < 0) continue;

                    fprintf(csv, "%s,%d,%s,%s,%d,%d,%llu,%llu,%.3f,%llu,%d\n",
                            pt_os_name(), ncpu, PRIM_NAME[prim], CS_NAME[cs], n, rep + 1,
                            (unsigned long long)total[cs], (unsigned long long)elapsed,
                            thr, (unsigned long long)cpu, bad ? 0 : 1);
                    fflush(csv);
                }
            }
        }
        if (rep < 0) fprintf(stderr, "aquecimento concluido\n");
        else         fprintf(stderr, "repeticao %d/%d concluida\n", rep + 1, reps);
    }

    fclose(csv);
    if (failures) {
        fprintf(stderr, "FALHA: %d execucoes com contador incorreto (exclusao mutua violada)\n", failures);
        return 1;
    }
    fprintf(stderr, "ok: dados em %s (metadados em %s)\n", out, meta_path);
    return 0;
}
