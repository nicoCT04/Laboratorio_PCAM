/*
 * nbody_paralelo.c
 * versiones paralelas de la simulación n-body 2d con openmp.
 *
 *   --mode parallel   (B) cada hilo calcula filas completas i, igual que la
 *                     secuencial. da el mismo resultado bit a bit.
 *   --mode optimized  (C) cada par (i, j) con i < j se calcula una sola vez
 *                     (tercera ley de newton). la carrera sobre ax[j] se evita
 *                     con acumuladores privados por hilo + reducción.
 *   --mode atomic     lo mismo que C pero sumando con omp atomic, solo para
 *                     comparar tiempos contra los acumuladores privados.
 *
 * compilar:  make
 * uso:       ./bin/nbody_paralelo [N] [steps] --mode parallel|optimized|atomic
 *                                 --threads T --schedule static|dynamic|guided
 *                                 --chunk C --out archivo
 *
 * init, lcg, constantes, checksum y write_state son copia exacta de
 * nbody_secuencial.c para que los resultados se puedan comparar.
 */

#include <errno.h>
#include <limits.h>
#include <math.h>
#include <omp.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef STUDENT
#define STUDENT "Diego"
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

/* ---------- configuración y argumentos ---------- */

enum { MODE_PARALLEL, MODE_OPTIMIZED, MODE_ATOMIC, N_MODES };

static const char *const MODE_ARGS[N_MODES] = {"parallel", "optimized", "atomic"};
static const char *const MODE_LABELS[N_MODES] = {"parallel", "parallel-optimized",
                                                 "parallel-atomic"};

#define N_SCHEDS 3
static const char *const SCHED_NAMES[N_SCHEDS] = {"static", "dynamic", "guided"};
static const omp_sched_t SCHED_KINDS[N_SCHEDS] = {omp_sched_static, omp_sched_dynamic,
                                                  omp_sched_guided};

typedef struct {
    int n, steps;
    int mode;    /* índice en MODE_ARGS */
    int threads;
    int sched;   /* índice en SCHED_NAMES */
    int chunk;   /* 0 = default de openmp */
    const char *out_path;
} Config;

/* strtol con chequeo de basura y rangos, atoi se traga cualquier cosa */
static int parse_int(const char *s, int min, int *out) {
    char *end;
    errno = 0;
    long v = strtol(s, &end, 10);
    if (errno != 0 || end == s || *end != '\0' || v < min || v > INT_MAX) return -1;
    *out = (int)v;
    return 0;
}

static int find_name(const char *s, const char *const *names, int count) {
    for (int k = 0; k < count; k++)
        if (strcmp(s, names[k]) == 0) return k;
    return -1;
}

/* 0 = ok, 1 = pidió ayuda, -1 = argumento malo */
static int parse_args(int argc, char **argv, Config *cfg) {
    int positional = 0;
    for (int k = 1; k < argc; k++) {
        const char *arg = argv[k];
        const char *val = k + 1 < argc ? argv[k + 1] : NULL;
        int ok = 0;

        if (strcmp(arg, "-h") == 0 || strcmp(arg, "--help") == 0) return 1;

        if (strncmp(arg, "--", 2) == 0) {
            if (!val) {
                fprintf(stderr, "Error: falta el valor de %s\n", arg);
                return -1;
            }
            if (strcmp(arg, "--out") == 0) {
                cfg->out_path = val;
                ok = 1;
            } else if (strcmp(arg, "--mode") == 0) {
                ok = (cfg->mode = find_name(val, MODE_ARGS, N_MODES)) >= 0;
            } else if (strcmp(arg, "--schedule") == 0) {
                ok = (cfg->sched = find_name(val, SCHED_NAMES, N_SCHEDS)) >= 0;
            } else if (strcmp(arg, "--threads") == 0) {
                ok = parse_int(val, 1, &cfg->threads) == 0;
            } else if (strcmp(arg, "--chunk") == 0) {
                ok = parse_int(val, 0, &cfg->chunk) == 0;
            }
            k++;
        } else if (positional == 0) {
            ok = parse_int(arg, 2, &cfg->n) == 0;
            positional++;
        } else if (positional == 1) {
            ok = parse_int(arg, 1, &cfg->steps) == 0;
            positional++;
        }

        if (!ok) {
            fprintf(stderr, "Error: argumento inválido cerca de '%s'\n", arg);
            return -1;
        }
    }
    return 0;
}

static void usage(const char *prog) {
    fprintf(stderr,
            "Uso: %s [N] [steps] --mode parallel|optimized|atomic\n"
            "       --threads T --schedule static|dynamic|guided --chunk C --out archivo\n"
            "  N >= 2, steps >= 1, T >= 1, C >= 0 (0 = default)\n",
            prog);
}

/* ---------- simulación ---------- */

/* euler semi-implícito, misma fórmula que update_bodies de la secuencial */
static inline void integrate_body(Body *bi, double axi, double ayi) {
    bi->vx += axi * DT;
    bi->vy += ayi * DT;
    bi->x += bi->vx * DT;
    bi->y += bi->vy * DT;
}

/* g / r^3 del par, lo comparten las dos versiones por pares */
static inline double pair_factor(double dx, double dy) {
    double r2 = dx * dx + dy * dy + EPS2;
    double inv_r = 1.0 / sqrt(r2);
    return G * inv_r * inv_r * inv_r;
}

/*
 * versión B. las funciones step_* se llaman dentro de la región paralela,
 * así que los omp for de aquí se reparten entre los hilos de ese equipo.
 */
static void step_direct(Body *b, double *ax, double *ay, int n) {
    /* fuerzas: cada hilo agarra filas i enteras según el schedule de runtime.
     * b solo se lee y ax[i], ay[i] solo los escribe el dueño de la fila, así
     * que no hay carreras. el ciclo j es idéntico al secuencial: mismo orden
     * de sumas, mismo resultado bit a bit */
#pragma omp for schedule(runtime)
    for (int i = 0; i < n; i++) {
        double axi = 0.0, ayi = 0.0; /* privadas, se declaran dentro */
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
    /* barrera implícita del for: nadie mueve cuerpos hasta tener todas las
     * fuerzas, si no se mezclarían posiciones de dos pasos */

    /* integración: costo igual por cuerpo, static es lo más barato */
#pragma omp for schedule(static)
    for (int i = 0; i < n; i++)
        integrate_body(&b[i], ax[i], ay[i]);
    /* otra barrera implícita: el siguiente paso ve todas las posiciones nuevas */
}

/*
 * versión C. acc_x/acc_y son nt filas de n: la fila t es solo del hilo t,
 * ahí suma +F sobre i y -F sobre j sin sincronizar nada. llegan en cero.
 */
static void step_pairs_private(Body *b, double *acc_x, double *acc_y, int n) {
    int nt = omp_get_num_threads();
    size_t row = (size_t)omp_get_thread_num() * (size_t)n;
    double *my_ax = acc_x + row;
    double *my_ay = acc_y + row;

    /* la fila i hace n-1-i pares (carga triangular), aquí el schedule pesa */
#pragma omp for schedule(runtime)
    for (int i = 0; i < n - 1; i++) {
        double axi = 0.0, ayi = 0.0;
        const double xi = b[i].x, yi = b[i].y, mi = b[i].mass;
        for (int j = i + 1; j < n; j++) {
            double dx = b[j].x - xi;
            double dy = b[j].y - yi;
            double s = pair_factor(dx, dy);
            axi += b[j].mass * s * dx;
            ayi += b[j].mass * s * dy;
            my_ax[j] -= mi * s * dx; /* fila propia, nadie más la toca */
            my_ay[j] -= mi * s * dy;
        }
        my_ax[i] += axi;
        my_ay[i] += ayi;
    }
    /* barrera implícita: todas las copias ya están completas */

    /* reducción + integración en un solo ciclo: el cuerpo i es de un solo
     * hilo, que suma la columna i de las nt copias, la deja en cero para el
     * siguiente paso y mueve el cuerpo. costo O(nt*n), nada frente a O(n^2) */
#pragma omp for schedule(static)
    for (int i = 0; i < n; i++) {
        double axi = 0.0, ayi = 0.0;
        for (int t = 0; t < nt; t++) {
            size_t k = (size_t)t * (size_t)n + (size_t)i;
            axi += acc_x[k];
            ayi += acc_y[k];
            acc_x[k] = 0.0;
            acc_y[k] = 0.0;
        }
        integrate_body(&b[i], axi, ayi);
    }
    /* barrera implícita antes del siguiente paso */
}

/*
 * misma idea que C pero con un solo ax/ay compartido y omp atomic en cada
 * suma. correcto, pero son millones de atomics por paso: es el punto de
 * comparación contra los acumuladores privados. ax/ay llegan en cero.
 */
static void step_pairs_atomic(Body *b, double *ax, double *ay, int n) {
#pragma omp for schedule(runtime)
    for (int i = 0; i < n - 1; i++) {
        double axi = 0.0, ayi = 0.0;
        const double xi = b[i].x, yi = b[i].y, mi = b[i].mass;
        for (int j = i + 1; j < n; j++) {
            double dx = b[j].x - xi;
            double dy = b[j].y - yi;
            double s = pair_factor(dx, dy);
            axi += b[j].mass * s * dx;
            ayi += b[j].mass * s * dy;
            /* otro hilo puede estar sumando al mismo j: leer-sumar-escribir atómico */
#pragma omp atomic
            ax[j] -= mi * s * dx;
#pragma omp atomic
            ay[j] -= mi * s * dy;
        }
        /* ax[i] también lo tocan las filas anteriores, va atómico */
#pragma omp atomic
        ax[i] += axi;
#pragma omp atomic
        ay[i] += ayi;
    }
    /* barrera implícita: todas las sumas atómicas terminaron */

#pragma omp for schedule(static)
    for (int i = 0; i < n; i++) {
        integrate_body(&b[i], ax[i], ay[i]);
        ax[i] = 0.0; /* queda limpio para el siguiente paso */
        ay[i] = 0.0;
    }
}

/*
 * una sola región paralela para todos los pasos, así los hilos se crean una
 * vez y no en cada paso. el ciclo de pasos NO se reparte: cada hilo lo
 * recorre completo y se coordinan con las barreras de los omp for (el paso
 * t+1 necesita todas las posiciones del paso t). devuelve los hilos usados.
 */
static int simulate(const Config *cfg, Body *b, double *acc_x, double *acc_y) {
    int used = 0;
#pragma omp parallel default(none) shared(cfg, b, acc_x, acc_y, used)
    {
        /* el número real del equipo, no el pedido */
#pragma omp single nowait
        used = omp_get_num_threads();

        for (int step = 0; step < cfg->steps; step++) {
            switch (cfg->mode) {
            case MODE_PARALLEL:
                step_direct(b, acc_x, acc_y, cfg->n);
                break;
            case MODE_OPTIMIZED:
                step_pairs_private(b, acc_x, acc_y, cfg->n);
                break;
            default:
                step_pairs_atomic(b, acc_x, acc_y, cfg->n);
                break;
            }
        }
    }
    return used;
}

int main(int argc, char **argv) {
    /* se lee antes de omp_set_num_threads para reportar el hardware tal cual */
    int max_threads = omp_get_max_threads();

    Config cfg = {DEFAULT_N, DEFAULT_STEPS, MODE_PARALLEL, max_threads, 0, 0, NULL};
    int rc = parse_args(argc, argv, &cfg);
    if (rc != 0) {
        usage(argv[0]);
        return rc > 0 ? 0 : 1;
    }

    omp_set_num_threads(cfg.threads);
    /* esto es lo que lee schedule(runtime); chunk 0 = default de la implementación */
    omp_set_schedule(SCHED_KINDS[cfg.sched], cfg.chunk);

    /* optimized necesita una fila de acumuladores por hilo, los otros solo una.
     * calloc porque los step_* esperan los acumuladores en cero */
    size_t rows = cfg.mode == MODE_OPTIMIZED ? (size_t)cfg.threads : 1;
    Body *bodies = malloc((size_t)cfg.n * sizeof(Body));
    double *acc_x = calloc(rows * (size_t)cfg.n, sizeof(double));
    double *acc_y = calloc(rows * (size_t)cfg.n, sizeof(double));
    if (!bodies || !acc_x || !acc_y) {
        fprintf(stderr, "Error: sin memoria\n");
        free(bodies);
        free(acc_x);
        free(acc_y);
        return 1;
    }

    init_bodies(bodies, cfg.n);

    /* Solo se mide la simulación, no la inicialización */
    double t0 = omp_get_wtime();
    int used = simulate(&cfg, bodies, acc_x, acc_y);
    double elapsed = omp_get_wtime() - t0;

    printf("Student: %s\n", STUDENT);
    printf("Executable: %s\n", argv[0]);
    printf("Mode: %s\n", MODE_LABELS[cfg.mode]);
    printf("N: %d | Steps: %d | Seed: %d\n", cfg.n, cfg.steps, SEED);
    printf("Threads requested: %d | Threads used: %d\n", cfg.threads, used);
    if (cfg.chunk > 0)
        printf("Schedule: %s | Chunk: %d\n", SCHED_NAMES[cfg.sched], cfg.chunk);
    else
        printf("Schedule: %s | Chunk: default\n", SCHED_NAMES[cfg.sched]);
    printf("Elapsed: %.3f s\n", elapsed);
    printf("Checksum: %.6f\n", checksum(bodies, cfg.n));
    printf("Processors reported by OpenMP: %d\n", omp_get_num_procs());
    printf("Max threads reported by OpenMP: %d\n", max_threads);

    int status = 0;
    if (cfg.out_path) {
        if (write_state(cfg.out_path, bodies, cfg.n) == 0)
            printf("State written to: %s\n", cfg.out_path);
        else
            status = 1;
    }

    free(bodies);
    free(acc_x);
    free(acc_y);
    return status;
}
