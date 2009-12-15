OBJS = cs4400f.o low.o util.o main.o scan.o
test: %: %.o $(OBJS)
test: LDLIBS = -lsane -lusb-1.0 -lm
main.o: CPPFLAGS = -DDRIVER_VERSION=0

.PHONY: clean
clean:
	rm -f main test $(addprefix *.,o i s)
