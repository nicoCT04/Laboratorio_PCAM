/*
 * nbody_secuencial.c
 * Versión A: simulación gravitacional N-Body 2D secuencial (baseline).
 *
 * Integrantes: Nicolas Concuá (23197), Esteban Cárcamo (23016), Diego López (23747)
 *
 * Doble ciclo completo i x j: cada cuerpo acumula la aceleración causada
 * por todos los demás. Sirve como referencia de corrección y de tiempo
 * para las versiones paralelas.
 *
 * Compilar:  make
 * Uso:       ./bin/nbody_secuencial [N] [steps] [--out archivo]
 *
 * OpenMP solo se usa para medir tiempo (omp_get_wtime) y reportar hardware.
 */

#include <math.h>
#include <omp.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef STUDENT
#define STUDENT "Nicolas Concuá (23197), Esteban Cárcamo (23016), Diego López (23747)"
#endif

#define SEED 42
#define G 1.0
#define DT 1e-3
#define EPS2 1e-4 /* softening al cuadrado: evita dividir entre 0 */

#define DEFAULT_N 5000
#define DEFAULT_STEPS 10

typedef struct {
    double x, y;
    double vx, vy;
    double mass;
} Body;

/* ---------- Generador aleatorio reproducible ----------
 * LCG de 64 bits propio: rand() cambia entre macOS y Linux, esto no.
 * Garantiza las mismas condiciones iniciales en todas las versiones. */
static uint64_t rng_state;

static void rng_seed(uint64_t seed) { rng_state = seed; }

/* Uniforme en [0, 1) */
static double rng_uniform(void) {
    rng_state = rng_state * 6364136223846793005ULL + 1442695040888963407ULL;
    return (double)(rng_state >> 11) * (1.0 / 9007199254740992.0);
}

/* Uniforme en [lo, hi) */
static double rng_range(double lo, double hi) {
    return lo + (hi - lo) * rng_uniform();
}

/* ---------- Simulación ---------- */

static void init_bodies(Body *b, int n) {
    rng_seed(SEED);
    for (int i = 0; i < n; i++) {
        b[i].x = rng_range(-1.0, 1.0);
        b[i].y = rng_range(-1.0, 1.0);
        b[i].vx = rng_range(-0.05, 0.05);
        b[i].vy = rng_range(-0.05, 0.05);
        b[i].mass = rng_range(0.5, 1.5) / n; /* masa total ~ 1 */
    }
}

/* Fase de fuerzas: a_i = sum_{j != i} G * m_j * d / (|d|^2 + EPS2)^(3/2) */
static void compute_accelerations(const Body *b, double *ax, double *ay, int n) {
    for (int i = 0; i < n; i++) {
        double axi = 0.0, ayi = 0.0;
        for (int j = 0; j < n; j++) {
            if (j == i) continue;
            double dx = b[j].x - b[i].x;
            double dy = b[j].y - b[i].y;
            double r2 = dx * dx + dy * dy + EPS2;
            double inv_r = 1.0 / sqrt(r2);
            double s = G * b[j].mass * inv_r * inv_r * inv_r;
            axi += s * dx;
            ayi += s * dy;
        }
        ax[i] = axi;
        ay[i] = ayi;
    }
}

/* Fase de integración (Euler semi-implícito). Va después de calcular
 * TODAS las aceleraciones para no mezclar posiciones de dos pasos. */
static void update_bodies(Body *b, const double *ax, const double *ay, int n) {
    for (int i = 0; i < n; i++) {
        b[i].vx += ax[i] * DT;
        b[i].vy += ay[i] * DT;
        b[i].x += b[i].vx * DT;
        b[i].y += b[i].vy * DT;
    }
}

static double checksum(const Body *b, int n) {
    double sum = 0.0;
    for (int i = 0; i < n; i++)
        sum += fabs(b[i].x) + fabs(b[i].y) + fabs(b[i].vx) + fabs(b[i].vy);
    return sum;
}

/* Estado final (x y vx vy por línea) para comparar con compare_states */
static int write_state(const char *path, const Body *b, int n) {
    FILE *f = fopen(path, "w");
    if (!f) {
        perror(path);
        return -1;
    }
    fprintf(f, "%d\n", n);
    for (int i = 0; i < n; i++)
        fprintf(f, "%.17g %.17g %.17g %.17g\n", b[i].x, b[i].y, b[i].vx, b[i].vy);
    fclose(f);
    return 0;
}

static void usage(const char *prog) {
    fprintf(stderr, "Uso: %s [N] [steps] [--out archivo]\n", prog);
}

int main(int argc, char **argv) {
    int n = DEFAULT_N;
    int steps = DEFAULT_STEPS;
    const char *out_path = NULL;
    int positional = 0;

    for (int k = 1; k < argc; k++) {
        if (strcmp(argv[k], "--out") == 0 && k + 1 < argc) {
            out_path = argv[++k];
        } else if (strcmp(argv[k], "-h") == 0 || strcmp(argv[k], "--help") == 0) {
            usage(argv[0]);
            return 0;
        } else if (positional == 0) {
            n = atoi(argv[k]);
            positional++;
        } else if (positional == 1) {
            steps = atoi(argv[k]);
            positional++;
        } else {
            usage(argv[0]);
            return 1;
        }
    }
    if (n < 2 || steps < 1) {
        fprintf(stderr, "Error: N >= 2 y steps >= 1\n");
        return 1;
    }

    Body *bodies = malloc((size_t)n * sizeof(Body));
    double *ax = malloc((size_t)n * sizeof(double));
    double *ay = malloc((size_t)n * sizeof(double));
    if (!bodies || !ax || !ay) {
        fprintf(stderr, "Error: sin memoria\n");
        return 1;
    }

    init_bodies(bodies, n);

    /* Solo se mide la simulación, no la inicialización */
    double t0 = omp_get_wtime();
    for (int step = 0; step < steps; step++) {
        compute_accelerations(bodies, ax, ay, n);
        update_bodies(bodies, ax, ay, n);
    }
    double elapsed = omp_get_wtime() - t0;

    printf("Student: %s\n", STUDENT);
    printf("Executable: %s\n", argv[0]);
    printf("Mode: sequential\n");
    printf("N: %d | Steps: %d | Seed: %d\n", n, steps, SEED);
    printf("Threads requested: 1 | Threads used: 1\n");
    printf("Schedule: none | Chunk: -\n");
    printf("Elapsed: %.3f s\n", elapsed);
    printf("Checksum: %.6f\n", checksum(bodies, n));
    printf("Processors reported by OpenMP: %d\n", omp_get_num_procs());
    printf("Max threads reported by OpenMP: %d\n", omp_get_max_threads());

    if (out_path && write_state(out_path, bodies, n) == 0)
        printf("State written to: %s\n", out_path);

    free(bodies);
    free(ax);
    free(ay);
    return 0;
}
