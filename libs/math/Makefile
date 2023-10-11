objs = beryl_lib.o

CFLAGS += -std=c99 -Wall -Wextra -Wpedantic -O2 -I../../src

math.beryldl: $(objs)
	$(CC) -shared $(objs) $(CFLAGS) -omath.beryldl -lm $(LINK_FLAGS)

$(objs):

clean:
	rm -f ./*.o
	rm -f ./*.beryldl
