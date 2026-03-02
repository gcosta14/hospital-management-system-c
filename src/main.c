// Projeto SO - Urgencias
// Aluno 1: Dinis Guedes Freire - 2024203477
// Aluno 2: Gonçalo Costa - 2024201591

#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <sys/msg.h>
#include <sys/wait.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <signal.h>
#include <pthread.h>
#include <sys/mman.h>
#include <stdarg.h>
#include <time.h>
#include <semaphore.h>
#include <errno.h>
#include <sys/select.h>

#include "structs.h"
#include "doctor.h"

#define LOG_FILENAME "DEI_Emergency.log"
#define LOG_SIZE (2 * 1024 * 1024)
#define MAX_EXTRA_DOCTORS 5 

/* ================= GLOBAIS ================= */

int shm_id, msq_id;
Estatisticas *shm_data;
Config conf;

pid_t *doctor_pids = NULL; 
pthread_t *triage_threads = NULL;

TriageQueue tq;
char *log_ptr = NULL;

int triage_active = 0;
int shutting_down = 0;
int active_extra_doctors = 0; 

/* ================= SINAIS ================= */

void handle_sigint(int sig) {
    (void)sig;
    shutting_down = 1;
}

void handle_sigusr1(int sig) {
    (void)sig;
    sem_wait(&shm_data->stats_sem);
    printf("\n### ESTATISTICAS (SIGUSR1) ###\n");
    printf("Pacientes Triados: %d\n", shm_data->total_pacientes_triados);
    printf("Pacientes Atendidos: %d\n", shm_data->total_pacientes_atendidos);
    printf("Medicos Extra Ativos: %d\n", active_extra_doctors);
    printf("##############################\n");
    sem_post(&shm_data->stats_sem);
    
    // Pequeno truque para escrever no log sem args variáveis
    log_write("[INFO] SIGUSR1 recebido. Estatisticas mostradas no ecra."); 
}

/* ================= LOG ================= */

void log_write(const char *fmt, ...) {
    va_list args;
    char buffer[512], msg_content[400], time_buffer[64];

    time_t t = time(NULL);
    strftime(time_buffer, sizeof(time_buffer), "%H:%M:%S", localtime(&t));

    sem_wait(&shm_data->log_sem);

    va_start(args, fmt);
    vsnprintf(msg_content, sizeof(msg_content), fmt, args);
    va_end(args);

    snprintf(buffer, sizeof(buffer), "[%s] %s\n", time_buffer, msg_content);
    printf("%s", buffer);
    fflush(stdout);

    int len = (int)strlen(buffer);
    if (shm_data->log_index + len < LOG_SIZE) {
        memcpy(log_ptr + shm_data->log_index, buffer, len);
        shm_data->log_index += len;
    }

    sem_post(&shm_data->log_sem);
}

void init_log() {
    int fd = open(LOG_FILENAME, O_RDWR | O_CREAT | O_TRUNC, 0666);
    if (fd < 0) { perror("log open"); exit(1); }
    if (ftruncate(fd, LOG_SIZE) == -1) { perror("log ftruncate"); exit(1); }

    log_ptr = mmap(NULL, LOG_SIZE, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (log_ptr == MAP_FAILED) { perror("log mmap"); exit(1); }
    close(fd);

    sem_init(&shm_data->log_sem, 1, 1);
}

/* ================= CONFIG ================= */

void read_config() {
    FILE *fp = fopen("config.txt", "r");
    if (!fp) { perror("config"); exit(1); }

    conf.triage_queue_max = 10;
    conf.triage_threads   = 2;
    conf.doctors          = 2;
    conf.shift_length     = 10;
    conf.msq_wait_max     = 5; 

    char line[128];
    while (fgets(line, sizeof(line), fp)) {
        if (strstr(line, "TRIAGE_QUEUE_MAX")) sscanf(line, "TRIAGE_QUEUE_MAX=%d", &conf.triage_queue_max);
        else if (strstr(line, "TRIAGE"))      sscanf(line, "TRIAGE=%d", &conf.triage_threads);
        else if (strstr(line, "DOCTORS"))     sscanf(line, "DOCTORS=%d", &conf.doctors);
        else if (strstr(line, "SHIFT_LENGTH"))sscanf(line, "SHIFT_LENGTH=%d", &conf.shift_length);
        else if (strstr(line, "MSQ_WAIT_MAX"))sscanf(line, "MSQ_WAIT_MAX=%d", &conf.msq_wait_max);
    }
    fclose(fp);
}

/* ================= TRIAGE QUEUE ================= */

void tq_init(TriageQueue *q, int cap) {
    q->buf = calloc((size_t)cap, sizeof(Paciente));
    q->cap = cap;
    q->head = q->tail = q->count = 0;
    pthread_mutex_init(&q->mtx, NULL);
    pthread_cond_init(&q->not_empty, NULL);
}

int tq_push(TriageQueue *q, const Paciente *p) {
    pthread_mutex_lock(&q->mtx);
    if (q->count == q->cap) {
        pthread_mutex_unlock(&q->mtx);
        return -1;
    }
    q->buf[q->tail] = *p;
    q->tail = (q->tail + 1) % q->cap;
    q->count++;
    pthread_cond_signal(&q->not_empty);
    pthread_mutex_unlock(&q->mtx);
    return 0;
}

int tq_pop(TriageQueue *q, Paciente *p) {
    pthread_mutex_lock(&q->mtx);
    while (q->count == 0 && triage_active) {
        pthread_cond_wait(&q->not_empty, &q->mtx);
    }
    if (q->count == 0 && !triage_active) {
        pthread_mutex_unlock(&q->mtx);
        return -1;
    }
    *p = q->buf[q->head];
    q->head = (q->head + 1) % q->cap;
    q->count--;
    pthread_mutex_unlock(&q->mtx);
    return 0;
}

/* ================= WORKERS ================= */

void* triage_logic(void* arg) {
    int id = *((int*)arg);
    free(arg);
    log_write("[Triagem %d] iniciada", id);

    while (1) {
        Paciente p;
        if (tq_pop(&tq, &p) == -1) break;

        usleep(p.tempo_triagem * 1000);
        MensagemPaciente msg;
        msg.mtype = p.prioridade;
        msg.dados = p;

        if (msgsnd(msq_id, &msg, sizeof(Paciente), 0) == -1) {
            log_write("[ERRO] msgsnd falhou (%s)", strerror(errno));
        } else {
            sem_wait(&shm_data->stats_sem);
            shm_data->total_pacientes_triados++;
            sem_post(&shm_data->stats_sem);
            log_write("[Triagem %d] Triou %s -> Prio %d", id, p.nome, p.prioridade);
        }
    }
    log_write("[Triagem %d] terminou", id);
    return NULL;
}

void start_triage() {
    triage_active = 1;
    triage_threads = malloc(sizeof(pthread_t) * (size_t)conf.triage_threads);
    for (int i = 0; i < conf.triage_threads; i++) {
        int *id = malloc(sizeof(int));
        *id = i + 1;
        pthread_create(&triage_threads[i], NULL, triage_logic, id);
    }
}

void start_doctors() {
    doctor_pids = malloc(sizeof(pid_t) * (size_t)conf.doctors);
    for (int i = 0; i < conf.doctors; i++) {
        pid_t pid = fork();
        if (pid == 0) {
            doctor_logic(i + 1, &conf, msq_id, shm_data, 0); // 0 = Normal
            exit(0);
        }
        doctor_pids[i] = pid;
        log_write("[Admission] Doctor %d (PID %d) iniciou turno.", i+1, pid);
    }
}

/* ================= SHUTDOWN ================= */

void graceful_shutdown() {
    log_write("[INFO] A terminar (SIGINT)...");

    triage_active = 0;
    pthread_mutex_lock(&tq.mtx);
    pthread_cond_broadcast(&tq.not_empty);
    pthread_mutex_unlock(&tq.mtx);

    if (triage_threads) {
        for (int i = 0; i < conf.triage_threads; i++) {
            pthread_join(triage_threads[i], NULL);
        }
        free(triage_threads);
    }

    // Mata todos
    kill(0, SIGTERM);
    while(wait(NULL) > 0);

    if (doctor_pids) free(doctor_pids);

    if (log_ptr) munmap(log_ptr, LOG_SIZE);
    sem_destroy(&shm_data->log_sem);
    sem_destroy(&shm_data->stats_sem);

    msgctl(msq_id, IPC_RMID, NULL);
    shmctl(shm_id, IPC_RMID, NULL);
    unlink("input_pipe");

    printf("\n[INFO] Shutdown completo.\n");
}

/* ================= MAIN LOOP ================= */

int main() {
    signal(SIGINT, handle_sigint);
    signal(SIGUSR1, handle_sigusr1);

    read_config();

    /* IPC */
    shm_id = shmget(IPC_PRIVATE, sizeof(Estatisticas), IPC_CREAT | 0666);
    shm_data = shmat(shm_id, NULL, 0);
    memset(shm_data, 0, sizeof(Estatisticas));
    sem_init(&shm_data->stats_sem, 1, 1);

    msq_id = msgget(IPC_PRIVATE, IPC_CREAT | 0666);
    unlink("input_pipe");
    mkfifo("input_pipe", 0666);
    init_log();
    tq_init(&tq, conf.triage_queue_max);

    /* Start */
    start_triage();
    start_doctors();

    log_write("[Admission] Iniciado. Envia SIGUSR1 para stats.");

    int fd = open("input_pipe", O_RDONLY | O_NONBLOCK);
    char acc[1024];
    int acc_len = 0;
    memset(acc, 0, sizeof(acc));

    while (!shutting_down) {
        // 1. SELECT
        fd_set rfds;
        FD_ZERO(&rfds);
        FD_SET(fd, &rfds);

        struct timeval tv;
        tv.tv_sec = 0;
        tv.tv_usec = 200000; // 200ms

        int r = select(fd + 1, &rfds, NULL, NULL, &tv);

        // --- 2. GESTÃO DE DOUTORES ---
        
        int status;
        pid_t dead_pid;
        while ((dead_pid = waitpid(-1, &status, WNOHANG)) > 0) {
            int replaced = 0;
            for (int i = 0; i < conf.doctors; i++) {
                if (doctor_pids[i] == dead_pid) {
                    log_write("[Admission] Medico Normal (PID %d) saiu. A substituir...", dead_pid);
                    pid_t new_pid = fork();
                    if (new_pid == 0) {
                        doctor_logic(i + 1, &conf, msq_id, shm_data, 0);
                        exit(0);
                    }
                    doctor_pids[i] = new_pid;
                    replaced = 1;
                    break;
                }
            }
            if (!replaced) {
                log_write("[Admission] Medico Extra (PID %d) terminou.", dead_pid);
                if (active_extra_doctors > 0) active_extra_doctors--;
            }
        }

        struct msqid_ds queue_info;
        if (msgctl(msq_id, IPC_STAT, &queue_info) != -1) {
            if ((int)queue_info.msg_qnum > conf.msq_wait_max) {
                if (active_extra_doctors < MAX_EXTRA_DOCTORS) {
                    log_write("[ALERTA] Fila cheia. A criar Medico Extra!");
                    pid_t pid = fork();
                    if (pid == 0) {
                        doctor_logic(900 + active_extra_doctors, &conf, msq_id, shm_data, 1);
                        exit(0);
                    }
                    active_extra_doctors++;
                }
            }
        }

        // --- 3. LEITURA DO PIPE ---
        if (r > 0 && FD_ISSET(fd, &rfds)) {
            char buf[256];
            ssize_t n = read(fd, buf, sizeof(buf));
            if (n > 0) {
                if (acc_len + n < (int)sizeof(acc) - 1) {
                    memcpy(acc + acc_len, buf, (size_t)n);
                    acc_len += (int)n;
                    acc[acc_len] = '\0';

                    char *start = acc;
                    char *nl;
                    while ((nl = strchr(start, '\n')) != NULL) {
                        *nl = '\0';
                        if (strlen(start) > 0) {
                            
                            // A. Verificar se é COMANDO
                            if (strncmp(start, "TRIAGE=", 7) == 0) {
                                int novas = atoi(start + 7);
                                log_write("[CMD] Pedido alterar threads para %d (Ignorado)", novas);
                            } 
                            // B. Verificar se é GRUPO ou PACIENTE ÚNICO
                            else {
                                char token1[50];
                                int tt, ta, pr;
                                
                                if (sscanf(start, "%49s %d %d %d", token1, &tt, &ta, &pr) == 4) {
                                    
                                    // Verifica se token1 é numérico (ex: "8")
                                    int is_group = 1;
                                    for(int k=0; token1[k] != '\0'; k++) {
                                        if (token1[k] < '0' || token1[k] > '9') {
                                            is_group = 0; break; 
                                        }
                                    }

                                    if (is_group) {
                                        // === É UM GRUPO ===
                                        int qtd = atoi(token1);
                                        time_t now = time(NULL);
                                        struct tm *t = localtime(&now);
                                        
                                        log_write("[Admission] A processar grupo de %d pacientes...", qtd);

                                        for(int i=0; i<qtd; i++) {
                                            Paciente p;
                                            // Gera ID: YYYYMMDD-NNN
                                            snprintf(p.nome, sizeof(p.nome), "%04d%02d%02d-%03d", 
                                                     t->tm_year+1900, t->tm_mon+1, t->tm_mday, i+1);
                                            p.tempo_triagem = tt;
                                            p.tempo_atendimento = ta;
                                            p.prioridade = pr;

                                            if (tq_push(&tq, &p) != 0) {
                                                log_write("[ERRO] Fila cheia. Paciente %s descartado.", p.nome);
                                            }
                                        }
                                    } 
                                    else {
                                        // === É UM PACIENTE ÚNICO ===
                                        Paciente p;
                                        strncpy(p.nome, token1, sizeof(p.nome)-1);
                                        p.nome[sizeof(p.nome)-1] = '\0';
                                        p.tempo_triagem = tt;
                                        p.tempo_atendimento = ta;
                                        p.prioridade = pr;

                                        if (tq_push(&tq, &p) != 0) {
                                            log_write("[ERRO] Fila cheia. Paciente %s descartado.", p.nome);
                                        }
                                    }
                                } 
                                else {
                                    log_write("[Triagem] Formato invalido: %s", start);
                                }
                            }
                        }
                        start = nl + 1;
                    }
                    int rest = (int)strlen(start);
                    memmove(acc, start, (size_t)rest);
                    acc_len = rest;
                    acc[acc_len] = '\0';
                }
            }
        }
    }

    close(fd);
    graceful_shutdown();
    return 0;
}
