# Laboratorio PCAM + OpenMP — Simulación N-Body 2D

Computación Paralela y Distribuida — UVG.
Simulación gravitacional N-Body en 2D escrita en **C (C11) con OpenMP**,
diseñada con la metodología PCAM.

El diseño completo (Partition, Communication, Agglomeration, Mapping) está en
[`docs/diseno_pcam.md`](docs/diseno_pcam.md).

## Estructura

```
Laboratorio_PCAM/
├── docs/
│   ├── diseno_pcam.md        # Diseño PCAM (base de explicacion_pcam.pdf)
│   ├── report/               # Fuentes HTML de los PDFs
│   ├── explicacion_pcam.pdf  # Informe PCAM (3-5 páginas)
│   └── evidencia.pdf         # Capturas y tablas de rendimiento
├── src/
│   ├── nbody_secuencial.c    # Versión A: secuencial, baseline
│   ├── nbody_paralelo.c      # Versiones B (directa) y C (pares) con OpenMP
│   └── compare_states.c      # Diferencia máxima entre estados finales
├── scripts/
│   ├── check_correctness.sh  # Compara cada versión contra la secuencial
│   ├── run_experiments.sh    # Corre todas las configuraciones -> results.csv
│   ├── analyze.py            # Gráficas de speedup y schedules
│   ├── capture_evidence.sh   # Capturas reales de terminal (Hyprland + tmux + grim)
│   └── build_reports.py      # Genera los PDFs desde docs/report/
├── evidence/                 # Capturas de compilación y ejecución
├── results/                  # Salidas crudas, hardware y gráficas
├── results.csv               # Tiempos promedio, speedup y efficiency
└── Makefile
```

## Requisitos

- **macOS:** Apple clang + `brew install libomp`
- **Linux:** `gcc` con soporte `-fopenmp`

## Compilación

```bash
make
```

El `Makefile` detecta el sistema operativo y compila todos los `.c` de `src/`
en `bin/`. Equivalente manual en Linux:

```bash
gcc -O2 -std=c11 -fopenmp src/nbody_secuencial.c -o bin/nbody_secuencial -lm
```

## Ejecución

```bash
./bin/nbody_secuencial 5000 10
./bin/nbody_secuencial 5000 10 --out results/states/seq.txt
```

| Argumento | Descripción | Default |
|-----------|-------------|---------|
| `N`       | Número de cuerpos | 5000 |
| `steps`   | Pasos de simulación | 10 |
| `--out f` | Guarda el estado final para comparar | — |

Salida de ejemplo:

```
Student: Nicolas Concuá (23197), Esteban Cárcamo (23016), Diego López (23747)
Executable: ./bin/nbody_secuencial
Mode: sequential
N: 5000 | Steps: 10 | Seed: 42
Threads requested: 1 | Threads used: 1
Schedule: none | Chunk: -
Elapsed: 0.271 s
Checksum: 5265.774135
Processors reported by OpenMP: 12
Max threads reported by OpenMP: 12
```

> **Escala:** en un equipo con Apple Silicon, N = 5000 tarda ~0.3 s en
> secuencial, muy poco para medir speedup con precisión. Se recomienda
> **N = 20000, 10 pasos** (~4 s secuencial). El valor elegido debe usarse
> en *todas* las comparaciones y documentarse en el informe.

## Corrección

Todas las versiones usan `seed = 42` y el mismo generador (LCG propio), así
que parten exactamente de los mismos cuerpos en cualquier máquina.

```bash
./bin/compare_states results/states/seq.txt results/states/otro.txt [tolerancia]
./scripts/check_correctness.sh 5000 10
```

`compare_states` reporta la diferencia máxima absoluta en `x, y, vx, vy`.

## Experimentos

```bash
./scripts/run_experiments.sh 20000 10 3   # N, pasos, corridas por configuración
python3 scripts/analyze.py                # gráficas en results/*.png
./scripts/capture_evidence.sh 20000 10 8  # capturas reales en evidence/ (Hyprland)
python3 scripts/build_reports.py          # docs/explicacion_pcam.pdf y docs/evidencia.pdf
```

- `run_experiments.sh` corre cada configuración 3 veces para `parallel` y
  `optimized` (las 9 obligatorias, `dynamic default` y `guided 8`/`64` para
  tener 3 chunks por schedule, 1 thread y, como puntos extra, 16 y 32 threads
  con `EXTRA_THREADS`), más `atomic` con 8 threads. Genera `results.csv`, la
  salida completa de cada corrida en `results/raw/` y el hardware en
  `results/environment.txt`.
- `speedup = T_secuencial / T_promedio` y `efficiency = speedup / threads`.
- Los informes se generan desde `docs/report/*.html` con Chromium headless;
  las tablas se llenan directamente desde `results.csv`.

**Tamaño usado: N = 20000, 10 pasos, seed 42** (la secuencial tarda ~9.4 s
en un i9-13980HX; con N = 5000 tardaba 0.6 s y el speedup salía ruidoso).

| Versión (8 threads) | Mejor schedule | Tiempo | Speedup |
|---------------------|----------------|--------|---------|
| A secuencial        | —              | 9.362 s | 1.00 |
| B directa           | dynamic default | 1.667 s | 5.62 |
| C pares (privados)  | dynamic default | 0.958 s | 9.77 |
| C pares (atomic)    | static default  | 9.261 s | 1.01 |

## Entregables

| Archivo | Contenido |
|---------|-----------|
| `results.csv` | Tiempos de las 3 corridas, promedio, speedup, efficiency y checksum |
| `evidence/*.png` | Compilación, secuencial, paralela, optimizada, hardware y corrección |
| `docs/explicacion_pcam.pdf` | Diseño PCAM, estrategia OpenMP, race conditions, resultados y preguntas |
| `docs/evidencia.pdf` | Capturas, tablas de tiempos y comparaciones de rendimiento |

## Integrantes

| Integrante | Parte |
|------------|-------|
| Nicolas Concuá (23197) | Diseño PCAM, estructura, versión secuencial, validación |
| Diego López (23747) | Versiones paralelas B y C |
| Esteban Cárcamo (23016) | Experimentos, resultados, evidencia e informe |
