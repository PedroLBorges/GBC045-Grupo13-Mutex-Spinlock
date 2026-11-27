#!/usr/bin/env python3
"""
analyze.py - Le os dados brutos (dados/*.csv) e gera, em experimentos/:

  resumo.csv             media e desvio padrao por configuracao
  vazao_vs_threads.png   grafico obrigatorio 1: vazao x numero de threads
  cruzamento.png         grafico obrigatorio 2: razao spinlock/mutex por plataforma
                         (o cruzamento e onde a razao passa por 1)
  cruzamentos.txt        pontos de cruzamento em texto

Os CSVs brutos nunca sao alterados. Uso (na raiz do repositorio):

    python3 experimentos/analyze.py
    python3 experimentos/analyze.py <pasta_dos_csv> <pasta_de_saida>

Requer: pip install pandas matplotlib
"""
import glob
import math
import os
import sys

import matplotlib

matplotlib.use("Agg")
import matplotlib.pyplot as plt
import pandas as pd

RAIZ = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
DADOS = sys.argv[1] if len(sys.argv) > 1 else os.path.join(RAIZ, "dados")
SAIDA = sys.argv[2] if len(sys.argv) > 2 else os.path.join(RAIZ, "experimentos")

SECOES = ["curta", "media", "longa"]
ROTULO_SECAO = {
    "curta": "Seção curta (só incremento)",
    "media": "Seção média (~1 µs)",
    "longa": "Seção longa (~100 µs)",
}
COR = {"mutex": "#1f77b4", "spinlock": "#d62728"}
COR_SECAO = {"curta": "#2ca02c", "media": "#ff7f0e", "longa": "#9467bd"}
MIN_REPS = 30


def carregar():
    arquivos = sorted(glob.glob(os.path.join(DADOS, "*.csv")))
    if not arquivos:
        sys.exit(f"nenhum CSV encontrado em {DADOS}")
    df = pd.concat([pd.read_csv(a) for a in arquivos], ignore_index=True)
    print("arquivos lidos:", ", ".join(os.path.basename(a) for a in arquivos))

    ruins = df[df["contador_ok"] != 1]
    if len(ruins):
        print(f"ATENCAO: {len(ruins)} execucoes com contador incorreto (exclusao mutua violada)")
    return df


def resumir(df):
    df = df.copy()
    df["tempo_s"] = df["tempo_ns"] / 1e9
    # nucleos ocupados em media durante a execucao (tempo de CPU / tempo de relogio)
    df["cpus_em_uso"] = df["cpu_ns"] / df["tempo_ns"]
    g = df.groupby(["os", "ncpu", "primitiva", "secao", "threads"])
    res = g.agg(
        n=("rep", "count"),
        tempo_medio_s=("tempo_s", "mean"),
        tempo_dp_s=("tempo_s", "std"),
        vazao_media=("vazao_ops_s", "mean"),
        vazao_dp=("vazao_ops_s", "std"),
        cpus_em_uso=("cpus_em_uso", "mean"),
    ).reset_index()
    res["cv_pct"] = 100 * res["vazao_dp"] / res["vazao_media"]

    poucas = res[res["n"] < MIN_REPS]
    if len(poucas):
        print(f"ATENCAO: {len(poucas)} configuracoes com menos de {MIN_REPS} repeticoes "
              "(o enunciado penaliza isso; nao use --quick no relatorio)")
    return res


def eixo_threads(ax, threads):
    ax.set_xscale("log", base=2)
    ax.set_xticks(threads)
    ax.set_xticklabels([str(t) for t in threads])
    ax.minorticks_off()


def grafico_vazao(res):
    sistemas = sorted(res["os"].unique())
    fig, eixos = plt.subplots(len(sistemas), len(SECOES),
                              figsize=(4.6 * len(SECOES), 3.6 * len(sistemas)),
                              squeeze=False)
    for i, so in enumerate(sistemas):
        ncpu = int(res[res["os"] == so]["ncpu"].iloc[0])
        for j, secao in enumerate(SECOES):
            ax = eixos[i][j]
            sub = res[(res["os"] == so) & (res["secao"] == secao)]
            threads = sorted(sub["threads"].unique())
            for prim in ["mutex", "spinlock"]:
                s = sub[sub["primitiva"] == prim].sort_values("threads")
                ax.errorbar(s["threads"], s["vazao_media"], yerr=s["vazao_dp"],
                            marker="o", capsize=3, color=COR[prim], label=prim)
            ax.axvline(ncpu, color="gray", linestyle=":", linewidth=1)
            ax.text(ncpu, 0.98, f" {ncpu} CPUs", transform=ax.get_xaxis_transform(),
                    fontsize=8, color="gray", va="top")
            eixo_threads(ax, threads)
            ax.set_yscale("log")
            ax.set_title(f"{so} — {ROTULO_SECAO[secao]}", fontsize=10)
            ax.set_xlabel("Número de threads")
            ax.set_ylabel("Vazão (aquisições/s)")
            ax.grid(True, which="both", alpha=0.3)
            ax.legend(fontsize=8)
    fig.suptitle("Vazão × número de threads (média ± desvio padrão)", fontsize=12)
    fig.tight_layout()
    caminho = os.path.join(SAIDA, "vazao_vs_threads.png")
    fig.savefig(caminho, dpi=150)
    plt.close(fig)
    print("gerado:", caminho)


def cruzamentos(threads, razao):
    """Pontos em que a razao spinlock/mutex cruza 1, interpolando em log2(threads)."""
    pontos = []
    for k in range(len(threads) - 1):
        a, b = razao[k], razao[k + 1]
        if (a - 1) * (b - 1) < 0:
            la, lb = math.log(a), math.log(b)
            f = la / (la - lb)
            x = 2 ** (math.log2(threads[k]) + f * (math.log2(threads[k + 1]) - math.log2(threads[k])))
            pontos.append((x, "spinlock deixa de compensar" if a > 1 else "spinlock passa a compensar"))
    return pontos


def grafico_cruzamento(res):
    sistemas = sorted(res["os"].unique())
    fig, eixos = plt.subplots(1, len(sistemas), figsize=(5.4 * len(sistemas), 4.2),
                              squeeze=False, sharey=True)
    linhas_txt = []
    for i, so in enumerate(sistemas):
        ax = eixos[0][i]
        ncpu = int(res[res["os"] == so]["ncpu"].iloc[0])
        linhas_txt.append(f"== {so} ({ncpu} CPUs) ==")
        todas = []
        for secao in SECOES:
            sub = res[(res["os"] == so) & (res["secao"] == secao)]
            piv = sub.pivot(index="threads", columns="primitiva", values="vazao_media").sort_index()
            if "mutex" not in piv or "spinlock" not in piv:
                continue
            threads = list(piv.index)
            razao = list(piv["spinlock"] / piv["mutex"])
            todas = threads
            ax.plot(threads, razao, marker="o", color=COR_SECAO[secao], label=ROTULO_SECAO[secao])
            pts = cruzamentos(threads, razao)
            for x, sentido in pts:
                ax.plot([x], [1], marker="x", markersize=10, color=COR_SECAO[secao])
                linhas_txt.append(f"  secao {secao}: cruzamento em ~{x:.1f} threads ({sentido})")
            if not pts:
                quem = "spinlock" if razao[-1] > 1 else "mutex"
                linhas_txt.append(f"  secao {secao}: sem cruzamento ({quem} vence em todos os pontos)")
        ax.axhline(1, color="black", linewidth=1)
        ax.axvline(ncpu, color="gray", linestyle=":", linewidth=1)
        if todas:
            eixo_threads(ax, todas)
        ax.set_yscale("log")
        ax.set_title(f"{so} ({ncpu} CPUs)")
        ax.set_xlabel("Número de threads")
        ax.grid(True, which="both", alpha=0.3)
        ax.legend(fontsize=8)
    eixos[0][0].set_ylabel("Vazão spinlock / vazão mutex\n(> 1: spinlock vence, < 1: mutex vence)")
    fig.suptitle("Ponto de cruzamento spinlock × mutex por plataforma (× = cruzamento)", fontsize=12)
    fig.tight_layout()
    caminho = os.path.join(SAIDA, "cruzamento.png")
    fig.savefig(caminho, dpi=150)
    plt.close(fig)
    print("gerado:", caminho)

    caminho_txt = os.path.join(SAIDA, "cruzamentos.txt")
    with open(caminho_txt, "w", encoding="utf-8") as f:
        f.write("\n".join(linhas_txt) + "\n")
    print("gerado:", caminho_txt)
    print("\n".join(linhas_txt))


def main():
    os.makedirs(SAIDA, exist_ok=True)
    df = carregar()
    res = resumir(df)
    caminho = os.path.join(SAIDA, "resumo.csv")
    res.to_csv(caminho, index=False, float_format="%.6g")
    print("gerado:", caminho)
    grafico_vazao(res)
    grafico_cruzamento(res)


if __name__ == "__main__":
    main()
