# Grupo 13 — GBC045 Sistemas Operacionais — Tema 1: Mutex e Spinlock

Camada de abstração de threads multiplataforma (`platform_thread`) e avaliação
experimental de mutex × spinlock em Linux e Windows. O objetivo é comparar
tempo de execução, vazão e consumo de CPU sob diferentes níveis de contenção.

## Integrantes

- Gabriela Camilo Rodrigues - 12411BSI356
- Guilherme Willians Morais Silva - 12411BSI373
- Pedro Lucas Oliveira Borges - 12311BSI261

## Estrutura

```
grupo13/
├── README.md
├── .gitignore
├── video/          vídeo da apresentação (ou link)
├── code/           platform_thread.h/.c, bench.c, Makefile, build.bat
├── testes/         test_platform.c (testa cada operação da camada)
├── dados/          dados brutos (CSV) e metadados de cada execução
└── experimentos/   scripts de execução, análise, gráficos e resumo
```

## A camada `platform_thread`

Uma única API (`pt_*`) traduzida em tempo de compilação para a API nativa:

| Operação | Linux / FreeBSD | Windows |
| --- | --- | --- |
| `pt_thread_create/join` | `pthread_create/join` | `CreateThread` + `WaitForSingleObject` |
| `pt_mutex_*` | `pthread_mutex_t` | `CRITICAL_SECTION` |
| `pt_cond_*` | `pthread_cond_t` | `CONDITION_VARIABLE` |
| `pt_spin_*` | `__atomic_exchange_n` | `InterlockedExchange` |
| `pt_now_ns` | `clock_gettime(CLOCK_MONOTONIC)` | `QueryPerformanceCounter` |
| `pt_process_cpu_ns` | `getrusage` | `GetProcessTimes` |

O spinlock é uma implementação própria baseada em test-and-test-and-set (TTAS),
com instrução `PAUSE` durante a espera. A estratégia é equivalente nas duas
plataformas, mas as diferenças de desempenho podem decorrer das operações
atômicas, das implementações de mutex, dos compiladores, do escalonamento e
das características do ambiente de execução.

## Como reproduzir

Execute os comandos a partir da raiz do repositório. Para a comparação final,
utilize Linux e Windows nativos na mesma máquina, conectada à tomada e com
outros programas fechados. O tempo total depende do hardware e da contenção.
Os arquivos de ambiente e metadados devem corresponder à execução dos CSVs.

### Linux nativo

```sh
sh experimentos/run_linux.sh
```

Requer GCC (ou compilador C compatível), Make e pthreads. O script compila
(`make -C code`), executa os testes funcionais e grava `dados/linux.csv`,
`dados/linux.csv.meta.txt` e `dados/linux.ambiente.txt`.

### Windows (nativo)

Com MSVC, no "x64 Native Tools Command Prompt for VS":

```bat
experimentos\run_windows.bat
```

Com MinGW-w64 (o código só inclui `<windows.h>`, sem `-pthread`):

```bat
experimentos\run_windows.bat gcc
```

O script compila, executa os testes funcionais e grava `dados/windows.csv`,
`dados/windows.csv.meta.txt` e `dados/windows.ambiente.txt`.

### Análise e gráficos

```sh
python3 -m pip install pandas matplotlib
python3 experimentos/analyze.py
```

No Windows, pode-se usar `python` em vez de `python3`.

Gera em `experimentos/`: `resumo.csv` (média e desvio padrão),
`vazao_vs_threads.png`, `cruzamento.png` e `cruzamentos.txt`.

## Experimento

- Threads: 1, 2, 4, 8, 16. Primitiva: mutex e spinlock.
- Seção crítica: curta (só o incremento do contador), média (~1 µs) e longa (~100 µs).
- Total fixo de aquisições por duração (2.000.000 / 500.000 / 5.000), dividido entre as N threads.
- 30 repetições por configuração, mais 1 passada de aquecimento descartada.
- A repetição é o laço externo: cada passada percorre todas as configurações, o que espalha o ruído que varia no tempo.
- O trabalho da seção crítica é um laço calibrado em tempo de execução em cada sistema.
- As threads esperam em um portão (mutex + variável de condição) e largam juntas; a criação das threads fica fora do tempo medido.
- Cada execução confere se o contador final é igual ao total (coluna `contador_ok`).

Cada plataforma possui 30 configurações (5 quantidades de threads × 2
primitivas × 3 durações), totalizando 900 medições válidas esperadas por
plataforma e 1.800 no conjunto final, sem contar o aquecimento.

## Formato dos dados brutos

`dados/<so>.csv`, uma linha por repetição, nunca editado à mão:

```
os,ncpu,primitiva,secao,threads,rep,total_aquisicoes,tempo_ns,vazao_ops_s,cpu_ns,contador_ok
```

`dados/<so>.csv.meta.txt` guarda data, calibração e totais; `dados/<so>.ambiente.txt` guarda kernel e CPU.

## Análise dos resultados

A análise compara a média e o desvio padrão das medições, a vazão em operações
por segundo e os pontos de cruzamento entre as primitivas. Os cruzamentos
estimados por interpolação não representam novas configurações medidas.

O spinlock utiliza espera ocupada e pode favorecer seções críticas curtas,
enquanto o mutex pode reduzir o desperdício de CPU em esperas prolongadas.
A seção crítica permanece serializada, de modo que aumentar o número de threads
não garante ganho de desempenho. A interpretação quantitativa deve utilizar
os CSVs finais e os gráficos regenerados a partir deles.

Entre as limitações estão os compiladores distintos, a calibração com uma
thread, possíveis variações de frequência, ausência de afinidade de CPU,
serviços em segundo plano, uma única configuração de hardware e o spinlock
sem recuo exponencial. Não foram avaliadas variantes adaptativas de mutex.

## Vídeo

Ver [`video/`](video/) para o arquivo ou link da apresentação do grupo.
