# Laboratorio PCAM + OpenMP — N-Body 2D
# Uso: make            -> compila todo lo que exista en src/
#      make clean
# Cambiar nombres en la salida: make STUDENT="Nombre Apellido"

STUDENT ?= Nicolas Concua, Diego, Esteban

UNAME := $(shell uname -s)

ifeq ($(UNAME),Darwin)
  # macOS: Apple clang + libomp de Homebrew (brew install libomp)
  CC       = clang
  LIBOMP  ?= $(shell brew --prefix libomp 2>/dev/null)
  OMPFLAGS = -Xpreprocessor -fopenmp -I$(LIBOMP)/include
  OMPLIBS  = -L$(LIBOMP)/lib -lomp
else
  CC      ?= gcc
  OMPFLAGS = -fopenmp
  OMPLIBS  = -fopenmp
endif

CFLAGS  = -O2 -std=c11 -Wall -Wextra -DSTUDENT="\"$(STUDENT)\""
LDLIBS  = -lm

SRC_DIR = src
BIN_DIR = bin

# Solo compila los programas cuyo .c ya existe
SOURCES = $(wildcard $(SRC_DIR)/*.c)
TARGETS = $(patsubst $(SRC_DIR)/%.c,$(BIN_DIR)/%,$(SOURCES))

.PHONY: all clean

all: $(TARGETS)

$(BIN_DIR)/%: $(SRC_DIR)/%.c | $(BIN_DIR)
	$(CC) $(CFLAGS) $(OMPFLAGS) $< -o $@ $(OMPLIBS) $(LDLIBS)

$(BIN_DIR):
	mkdir -p $(BIN_DIR)

clean:
	rm -rf $(BIN_DIR)
