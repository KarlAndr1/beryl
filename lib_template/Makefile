objs = beryl_lib.o

CFLAGS += -std=c99 -Wall -Wextra -Wpedantic -O2 -I../src

math.beryldl: $(objs)
	$(CC) -shared $(objs) $(CFLAGS) -olib.beryldl

$(objs):

clean:
	rm ./*.o
	rm ./*.beryldl
