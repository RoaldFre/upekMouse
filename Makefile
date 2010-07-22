CC 		= gcc
CFLAGS = -g -ggdb -Wextra -Wall -pedantic -Wwrite-strings -std=gnu99 -pedantic -march=native -masm=intel -pipe
OPTIM 		= -O2
PROGNAME 	= upek
OBJECTS 	= upek.o
INCFLAGS 	= 
LDFLAGS 	= -lbsapi 
LIBS 		= 

ASM = as
ASMFLAGS = --warn -march=core2 --gstabs+

COMPILE_TO_ASM_FLAGS = -fverbose-asm -S

CFLAGS_ADDITIONAL = 
LDFLAGS_ADDITIONAL =


all: $(PROGNAME)
safe: 	O0
O0: 
	@make all OPTIM=-O0 -B
O1: 
	@make all OPTIM=-O1 -B
O2: 
	@make all OPTIM=-O2 -B
O3: 
	@make all OPTIM=-O3 -B

$(PROGNAME): $(OBJECTS)
	@echo "	LD $(PROGNAME) "
	@$(CC) -o $(PROGNAME) $(OBJECTS) $(LDFLAGS) $(LDFLAGS_ADDITIONAL) $(LIBS)

profile:
	@make -B all CFLAGS_ADDITIONAL=-pg LDFLAGS_ADDITIONAL=-pg



.SUFFIXES:	.c .cc .C .cpp .o .s

.c.o :
	@echo "	CC $@"
	@$(CC) -o $@ -c $(CFLAGS) $(CFLAGS_ADDITIONAL) $(OPTIM) $< $(INCFLAGS)

.s.o:
	@echo "	AS $@"
	@$(ASM) -o $@  $(ASMFLAGS) $<

clean:
	@rm -f *.o $(PROGNAME)

.PHONY: all
.PHONY: clean
