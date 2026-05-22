CC	      = g++

CFLAGS        = -g -Wall -Wextra

LIBFLAG       = -lm

HDRS	      = global.h \
		queue.h \
		rand.h \
		simulator.h \
		calendar.h \
		packet.h \
		event.h \
		easyio.h \
		stat.h \
		buffer.h

MAKEFILE      = Makefile

# Oggetti del simulatore (file legacy modificati per il progetto C3).
OBJS	      = main.o \
		queue.o \
		rand.o \
		simulator.o \
		calendar.o \
		packet.o \
		event.o \
		easyio.o \
		stat.o \
		buffer.o

PROGRAM	      = queue

SRCS	      = main.c \
		queue.c \
		rand.c \
		simulator.c \
		calendar.c \
		packet.c \
		event.c \
		easyio.c \
		stat.c \
		buffer.c

# Runner C++ ausiliario (orchestratore degli esperimenti della relazione,
# non parte del simulatore in senso stretto).
RUNNER        = runner
RUNNER_OBJS   = runner.o

all:		$(PROGRAM) $(RUNNER)

$(PROGRAM):	$(OBJS)
		@echo -n "Loading $(PROGRAM) ... "
		@$(CC) $(CFLAGS) $(OBJS) -o $(PROGRAM) $(LIBFLAG)
		@echo "done"

$(RUNNER):	$(RUNNER_OBJS)
		@echo -n "Loading $(RUNNER) ... "
		@$(CC) $(CFLAGS) $(RUNNER_OBJS) -o $(RUNNER)
		@echo "done"

# Pattern per sorgenti .cpp (es. runner.cpp).
%.o: %.cpp
		$(CC) $(CFLAGS) -c $< -o $@

# Override del pattern .c -> .o: silenzia i warning preesistenti
# legati alla conversione implicita C -> C++ (clang) e ai string
# literal in easyio.c, senza modificare i sorgenti legacy.
%.o: %.c
		$(CC) $(CFLAGS) -Wno-deprecated -Wno-writable-strings \
			-Wno-unused-but-set-variable -c $< -o $@

clean:;		@rm -f $(OBJS) $(RUNNER_OBJS) $(PROGRAM) $(RUNNER)
