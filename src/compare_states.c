/*
 * compare_states.c
 * Compara dos estados finales generados con --out y reporta la diferencia
 * máxima absoluta (x, y, vx, vy) respecto a la referencia secuencial.
 *
 * Uso: ./bin/compare_states referencia.txt otro.txt [tolerancia]
 * Código de salida: 0 si max_diff <= tolerancia (default 1e-9), 1 si no.
 */

#include <math.h>
#include <stdio.h>
#include <stdlib.h>

static double *read_state(const char *path, int *n_out) {
    FILE *f = fopen(path, "r");
    if (!f) {
        perror(path);
        return NULL;
    }
    int n;
    if (fscanf(f, "%d", &n) != 1 || n <= 0) {
        fprintf(stderr, "%s: encabezado inválido\n", path);
        fclose(f);
        return NULL;
    }
    double *v = malloc((size_t)n * 4 * sizeof(double));
    if (!v) {
        fclose(f);
        return NULL;
    }
    for (int k = 0; k < n * 4; k++) {
        if (fscanf(f, "%lf", &v[k]) != 1) {
            fprintf(stderr, "%s: faltan datos\n", path);
            free(v);
            fclose(f);
            return NULL;
        }
    }
    fclose(f);
    *n_out = n;
    return v;
}

int main(int argc, char **argv) {
    if (argc < 3) {
        fprintf(stderr, "Uso: %s referencia.txt otro.txt [tolerancia]\n", argv[0]);
        return 2;
    }
    double tol = argc > 3 ? atof(argv[3]) : 1e-9;

    int na, nb;
    double *a = read_state(argv[1], &na);
    double *b = read_state(argv[2], &nb);
    if (!a || !b) return 2;
    if (na != nb) {
        fprintf(stderr, "N distinto: %d vs %d\n", na, nb);
        return 2;
    }

    const char *names[4] = {"x", "y", "vx", "vy"};
    double max_diff = 0.0;
    int worst_body = 0, worst_field = 0;
    for (int i = 0; i < na; i++) {
        for (int c = 0; c < 4; c++) {
            double d = fabs(a[i * 4 + c] - b[i * 4 + c]);
            if (d > max_diff) {
                max_diff = d;
                worst_body = i;
                worst_field = c;
            }
        }
    }

    printf("Bodies compared: %d\n", na);
    printf("Max abs diff: %.3e (body %d, %s)\n", max_diff, worst_body, names[worst_field]);
    printf("Tolerance: %.1e -> %s\n", tol, max_diff <= tol ? "OK" : "FAIL");

    free(a);
    free(b);
    return max_diff <= tol ? 0 : 1;
}
