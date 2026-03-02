CC      = gcc
CFLAGS  = -Wall -Wextra -pthread
PROG    = urgencia
OBJS    = main.o doctor.o

all: $(PROG)

clean:
	rm -f $(OBJS) *~ $(PROG) DEI_Emergency.log

$(PROG): $(OBJS)
	$(CC) $(CFLAGS) $(OBJS) -o $@

.c.o:
	$(CC) $(CFLAGS) -c $< -o $@

#################################################

main.o:   main.c structs.h doctor.h
doctor.o: doctor.c doctor.h structs.h
