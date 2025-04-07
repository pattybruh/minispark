CC = gcc
CFLAGS = -Wall -Wextra -Og -g -pthread -I$(SOL_DIR) -I$(LIB_DIR)

APP_DIR = .
LIB_DIR = ../../lib
SOL_DIR = ../../solution
BIN_DIR = .

PROGRAMS = 1.tmp 2.tmp 3.tmp 4.tmp 6.tmp 5.tmp 11.tmp 12.tmp 13.tmp 15.tmp 18.tmp 19.tmp 20.tmp

PROGRAMS_TSAN = 7.tmp 8.tmp 9.tmp 10.tmp 14.tmp 16.tmp 17.tmp 

CHECKERS = 19checker.tmp 

OBJS = $(SOL_DIR)/minispark.o $(LIB_DIR)/lib.o

all: $(PROGRAMS) $(PROGRAMS_TSAN) $(CHECKERS)

# compile all the bins
$(PROGRAMS): %.tmp : $(APP_DIR)/%.o $(OBJS)
	$(CC) $(CFLAGS) -o $@ $^

$(PROGRAMS_TSAN): %.tmp : $(APP_DIR)/%.o $(OBJS)
	$(CC) $(CFLAGS) -fsanitize=thread -o $@ $^

$(PROGRAMS_ASAN): %.tmp : $(APP_DIR)/%.o $(OBJS)
	$(CC) $(CFLAGS) -fsanitize=address -o $@ $^

$(CHECKERS) : %.tmp : $(APP_DIR)/%.o $(OBJS)
	$(CC) $(CFLAGS) -o $@ $^

# compile all the objects
$(APP_DIR)/%.o: $(APP_DIR)/%.c
	$(CC) $(CFLAGS) -c -o $@ $^

$(LIB_DIR)/%.o: $(LIB_DIR)/%.c
	$(CC) $(CFLAGS) -c -o $@ $^

clean:
	rm -f *.tmp $(APP_DIR/*.o) $(LIB_DIR)/*.o
