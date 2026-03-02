// Dinis Guedes Freire 2024203477
// Gonçalo Costa 2024201591

#ifndef STRUCTS_H
#define STRUCTS_H

#include <pthread.h>
#include <semaphore.h>

typedef struct {
    int triage_queue_max;
    int triage_threads;
    int doctors;
    int shift_length;
    int msq_wait_max;
} Config;

typedef struct {
    char nome[50];
    int tempo_triagem;
    int tempo_atendimento;
    int prioridade; // 1..10 (0 reservado para mensagens de controlo)
} Paciente;

typedef struct {
    long mtype;
    Paciente dados;
} MensagemPaciente;

typedef struct {
    int total_pacientes_triados;
    int total_pacientes_atendidos;
    int log_index;
    sem_t log_sem;
    sem_t stats_sem;
} Estatisticas;

typedef struct {
    Paciente *buf;
    int cap, head, tail, count;
    pthread_mutex_t mtx;
    pthread_cond_t not_empty;
    pthread_cond_t empty; // sinaliza quando a fila fica vazia (shutdown limpo)
} TriageQueue;

void log_write(const char *fmt, ...);

#endif
