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
│   └── diseno_pcam.md        # Diseño PCAM (base de explicacion_pcam.pdf)
├── src/
│   ├── nbody_secuencial.c    # Versión A: secuencial, baseline
│   ├── nbody_paralelo.c      # Versiones B (directa) y C (pares) con OpenMP
│   └── compare_states.c      # Diferencia máxima entre estados finales
├── scripts/
│   └── check_correctness.sh  # Compara cada versión contra la secuencial
├── evidence/                 # Capturas de compilación y ejecución
├── results/                  # results.csv y estados finales
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
Student: Nicolas Concua, Diego, Esteban
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

## Integrantes

| Integrante | Parte |
|------------|-------|
| Nicolás | Diseño PCAM, estructura, versión secuencial, validación |
| Diego | Versiones paralelas B y C |
| Esteban | Experimentos, resultados, evidencia e informe |
