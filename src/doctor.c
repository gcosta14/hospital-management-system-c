// Dinis Guedes Freire 2024203477
// Gonçalo Costa 2024201591

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/msg.h>
#include <time.h>
#include <errno.h>
#include <semaphore.h>
#include <signal.h>
#include <string.h>

#include "structs.h"
#include "doctor.h"

void doctor_logic(int id, Config *conf, int msq_id, Estatisticas *shm_data, int extra) {

    /* Só o Admission trata SIGINT */
    signal(SIGINT, SIG_IGN);

    time_t inicio_turno = time(NULL);
    MensagemPaciente msg;
    struct msqid_ds buf;

    if (extra) {
        log_write("[Doctor %d] Iniciado (EXTRA) para ajudar na carga!", id);
    } else {
        log_write("[Doctor %d] Iniciado.", id);
    }

    while (1) {
        // 1. Verificações de Saída
        
        // Se for médico NORMAL: sai se o turno acabar
        if (!extra && difftime(time(NULL), inicio_turno) >= conf->shift_length) {
            log_write("[Doctor %d] Turno terminou.", id);
            break;
        }

        // Se for médico EXTRA: sai se a fila estiver calma (< 80% do max)
        if (extra) {
            // Ler estado da fila
            if (msgctl(msq_id, IPC_STAT, &buf) != -1) {
                int threshold = (int)(conf->msq_wait_max * 0.8);
                if ((int)buf.msg_qnum < threshold) {
                    log_write("[Doctor %d] Carga normalizou (%ld msgs < %d). Extra a sair.", id, buf.msg_qnum, threshold);
                    break;
                }
            }
        }

        // 2. Recebe paciente (bloqueante, prioridade -10)
        // Usamos IPC_NOWAIT no extra para ele não ficar preso se a fila esvaziar de repente
        int flags = extra ? IPC_NOWAIT : 0;
        
        if (msgrcv(msq_id, &msg, sizeof(Paciente), -10, flags) >= 0) {

            /* Mensagem de controlo para terminar */
            if (msg.dados.prioridade == 0 && strcmp(msg.dados.nome, "__END__") == 0) {
                break;
            }

            /* Simular consulta */
            usleep(msg.dados.tempo_atendimento * 1000);

            // Atualizar Stats
            sem_wait(&shm_data->stats_sem);
            shm_data->total_pacientes_atendidos++;
            sem_post(&shm_data->stats_sem);

            log_write("[Doctor %d] Atendeu %s (Prio %d)", id, msg.dados.nome, msg.dados.prioridade);
        } else {
            // Se for extra e não houver msg, espera um pouco e verifica condição de saída
            if (errno == ENOMSG && extra) {
                usleep(100000); // 100ms
            }
        }
    }
    
    // Antes de morrer, garante que o processo morre mesmo
    exit(0);
}