# Hospital Emergency Management System (C)

## Description
This project is a hospital emergency management system developed in C as part of an Operating Systems course.

The system simulates the flow of patients in an emergency unit, including triage, doctor allocation and treatment management. It coordinates multiple processes and threads using several operating system mechanisms.

## System Overview

The system includes:

- Multiple doctor processes created using `fork()`
- Triage threads implemented with `pthreads`
- Inter-process communication using System V message queues
- Shared memory for global statistics management
- POSIX semaphores for synchronization
- Signal handling for graceful shutdown and statistics reporting
- Memory-mapped logging using `mmap()`

## Operating Systems Concepts Applied

- Process creation (`fork`)
- Multithreading (`pthread`)
- System V Message Queues (`msgsnd`, `msgrcv`)
- Shared Memory
- Semaphores (synchronization and mutual exclusion)
- Signal handling (`SIGINT`, `SIGUSR1`)
- Memory mapping (`mmap`)
- Configuration file parsing
- Modular C programming with header files

## Technologies Used

- C
- GCC
- POSIX Threads
- System V IPC
- Makefile build automation

## Project Structure

hospital-management-system-c/
│
├── src/
│   ├── main.c
│   ├── doctor.c
│   ├── doctor.h
│   ├── structs.h
│
├── config.txt
├── Makefile
├── README.md
└── .gitignore

## How to Run

Compile using the provided Makefile:

make

Run the program:

./urgencia

## Features

- Priority-based emergency handling
- Dynamic doctor allocation
- Real-time statistics collection
- Configurable system parameters through `config.txt`
- Graceful shutdown with signal handling
- Logging of system activity

## What I Learned

- Designing concurrent systems in C
- Managing synchronization between processes and threads
- Implementing IPC mechanisms
- Structuring medium-sized C projects
- Applying Operating Systems theoretical concepts in a practical scenario

## Author

Gonçalo Costa  
Computer Engineering Student – University of Coimbra
