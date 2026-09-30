# Diseño PCAM — Simulación N-Body 2D con OpenMP

Este documento se escribió **antes** de implementar. Define las decisiones de
diseño que siguen las tres versiones del laboratorio y sirve como base para
`explicacion_pcam.pdf`.

## 0. Modelo del problema

Cada cuerpo tiene posición, velocidad y masa:

```c
typedef struct {
    double x, y;
    double vx, vy;
    double mass;
} Body;
```

En cada paso de simulación:

1. **Fase de fuerzas:** para cada cuerpo `i` se acumula la aceleración causada
   por todos los cuerpos `j != i`:

   ```
   dx = x_j - x_i,  dy = y_j - y_i
   r2 = dx² + dy² + EPS2          (EPS2 = softening, evita dividir entre 0)
   a_i += G * m_j * (dx, dy) / r2^(3/2)
   ```

2. **Fase de integración** (Euler semi-implícito):

   ```
   v_i += a_i * DT
   p_i += v_i * DT
   ```

Parámetros fijos, idénticos en todas las versiones:

| Parámetro | Valor | Motivo |
|-----------|-------|--------|
| `SEED`    | 42    | Mismas condiciones iniciales en todas las versiones |
| `G`       | 1.0   | Unidades normalizadas |
| `DT`      | 1e-3  | Paso pequeño, simulación estable en 10 pasos |
| `EPS2`    | 1e-4  | Softening para evitar singularidades |
| Posiciones | U[-1, 1) | Cuerpos distribuidos en un cuadrado |
| Velocidades | U[-0.05, 0.05) | Movimiento inicial pequeño |
| Masas     | U[0.5, 1.5) / N | Masa total ≈ 1, independiente de N |

**Generador aleatorio propio (LCG de 64 bits).** No se usa `rand()` porque su
implementación cambia entre macOS y Linux. Con un LCG propio, los tres
integrantes del grupo generan exactamente los mismos cuerpos en cualquier
máquina, y los checksums son comparables.

**Costo:** la versión directa hace `N·(N-1)` interacciones por paso, es decir
O(N²). Con N = 5000 son ~25 millones de interacciones por paso: suficiente
trabajo para medir speedup, overhead y balanceo.

## 1. P — Partition (partición)

**Descomposición de dominio sobre los cuerpos.** La unidad mínima de trabajo
paralelizable es **una interacción (i, j)**: calcular la contribución de `j`
a la aceleración de `i`. Hay N² de ellas por paso.

Tareas que se derivan:

- **Versión B (directa):** tarea = *calcular la aceleración total del cuerpo i*
  (una fila completa de la matriz de interacciones). Hay N tareas
  independientes: cada una solo **lee** posiciones/masas y **escribe** únicamente
  `ax[i], ay[i]`.
- **Versión C (simetría de pares):** tarea = *interacción del par (i, j) con
  i < j*, que por la tercera ley de Newton produce `+F` sobre `i` y `-F` sobre
  `j`. Hay N(N-1)/2 tareas: la mitad del trabajo, pero cada tarea escribe en
  **dos** acumuladores.
- **Fase de integración:** tarea = actualizar el cuerpo `i`. N tareas
  totalmente independientes.

**Dependencias entre pasos:** el paso `t+1` necesita **todas** las posiciones
del paso `t`. Por eso:

- Los pasos de tiempo son **secuenciales** (no se paraleliza el ciclo de pasos).
- Dentro de un paso, la fase de fuerzas debe terminar completa antes de
  integrar: si un cuerpo se moviera mientras otro todavía calcula su fuerza,
  se mezclarían posiciones de dos instantes distintos. Se separa en dos
  arreglos (`ax`, `ay`) y dos ciclos, con una barrera entre ellos.

## 2. C — Communication (comunicación)

Memoria compartida: la "comunicación" es qué datos leen y escriben los threads.

| Dato | Fase de fuerzas | Fase de integración | Riesgo |
|------|-----------------|---------------------|--------|
| `bodies[].x, y, mass` | Lectura compartida por todos | Escritura, solo el dueño de `i` | Ninguno si hay barrera entre fases |
| `bodies[].vx, vy` | No se usan | Lectura/escritura, solo el dueño de `i` | Ninguno |
| `ax[i], ay[i]` (versión B) | Escritura solo por el thread dueño de `i` | Lectura por el dueño de `i` | Ninguno |
| `ax[i], ay[i]` y `ax[j], ay[j]` (versión C) | **Escritura desde varios threads** | Lectura | **Race condition** |
| Variables temporales `dx, dy, r2, ...` | Privadas por thread | — | Ninguno si se declaran dentro del ciclo |

Puntos de sincronización:

1. **Barrera** al terminar la fase de fuerzas (implícita al final del
   `#pragma omp for`).
2. **Barrera** al terminar la integración, antes del siguiente paso.
3. **Versión C:** combinar los acumuladores de todos los threads.

**Race condition de la versión C.** Si el thread A procesa el par (3, 7) y el
thread B el par (5, 7) al mismo tiempo, ambos hacen `ax[7] -= ...`. La
operación es *leer-sumar-escribir*; si se intercalan, una de las dos
contribuciones se pierde. Estrategias posibles:

| Estrategia | Ventaja | Desventaja |
|------------|---------|------------|
| `#pragma omp critical` | Trivial | Serializa ~N²/2 actualizaciones: inviable |
| `#pragma omp atomic` | Correcta, fina | Contención y costo por cada suma (millones por paso) |
| **Acumuladores privados por thread + reducción** | Sin sincronización dentro del ciclo | Memoria O(T·N) y una fase extra de suma O(T·N) |

**Decisión:** acumuladores privados por thread. Cada thread escribe en su
propia copia de `ax`/`ay` sin ninguna sincronización y al final se suman las
copias (en paralelo por cuerpo). El costo extra O(T·N) es despreciable frente
a O(N²). `atomic` se deja como punto de comparación experimental.

## 3. A — Agglomeration (aglomeración)

Una interacción (i, j) cuesta ~20 FLOPs: usarla como tarea individual haría que
el overhead de repartir trabajo domine por completo. Se aglomera así:

- **Nivel 1:** todas las interacciones de un cuerpo `i` forman una tarea
  (una fila). Se ejecuta como ciclo interno secuencial, lo que aprovecha
  localidad de caché y permite acumular en registros (`axi`, `ayi`) en vez de
  escribir en memoria en cada iteración.
- **Nivel 2:** varias filas se agrupan en un **chunk** de OpenMP.

El tamaño de chunk controla la granularidad:

- Chunk pequeño (p. ej. 8): mejor balanceo, más overhead de planificación
  (sobre todo con `dynamic`).
- Chunk grande (p. ej. 64 o `default`): menos overhead, riesgo de desbalance
  al final.

**Experimento:** comparar al menos chunks `default`, 8 y 64 (ver
`results.csv`) y relacionarlos con el tiempo.

## 4. M — Mapping (asignación)

Se asignan **filas (cuerpos i)** a threads con `#pragma omp for schedule(...)`.
El programa recibe el schedule y el chunk por línea de comandos y los aplica
con `schedule(runtime)` + `omp_set_schedule()`, para poder medir sin
recompilar.

- **Versión B:** cada fila cuesta lo mismo (N-1 interacciones). Se espera que
  `static` sea suficiente y el más barato (reparto sin costo en tiempo de
  ejecución).
- **Versión C:** la fila `i` solo procesa los `j > i`, así que cuesta `N-1-i`
  interacciones: la carga es **triangular**. Con `static` sin chunk el primer
  thread recibe casi el doble de trabajo que el promedio. Se espera que
  `dynamic`/`guided` o `static` con chunk pequeño (reparto cíclico) balanceen
  mejor.
- **Integración:** costo uniforme por cuerpo → `static`.

Número de threads: 1, 2, 4 y 8 (configurable con `--threads`). Se reporta
`omp_get_num_procs()` y `omp_get_max_threads()` para documentar el hardware.

## 5. Corrección y reproducibilidad

- Misma semilla, N y pasos en todas las comparaciones.
- Cada ejecución imprime un **checksum** = Σ(|x| + |y| + |vx| + |vy|).
- Con `--out archivo` se guarda el estado final de todos los cuerpos;
  `compare_states` reporta la **diferencia máxima** contra la versión
  secuencial.
- Versión B: cada `ax[i]` se suma en el mismo orden que la secuencial →
  resultado idéntico bit a bit.
- Versión C: el orden de las sumas cambia (acumuladores por thread) →
  diferencias del orden de 1e-15, aceptables y explicables por redondeo de
  punto flotante.

## 6. Plan de trabajo del grupo

| Integrante | Responsabilidad |
|------------|-----------------|
| Nicolás | Diseño PCAM, estructura del proyecto, `Makefile`, versión secuencial (A), herramienta `compare_states` |
| Diego | `nbody_paralelo.c`: versión paralela directa (B) y optimizada por pares (C) |
| Esteban | Script de experimentos, `results.csv`, speedup/efficiency, evidencia y PDFs finales |
