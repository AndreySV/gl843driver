BACKEND = gl843
OBJS = cs4400f.o low.o util.o main.o sanei.o scan.o

CPPFLAGS = -DDRIVER_VERSION=0 -shared -fPIC -fvisibility=hidden

#Make targets

%.o: %.c
	@echo CC $^
	@gcc $(CPPFLAGS) -c $^

libsane-$(BACKEND).so.1: $(OBJS)
	@echo LD $@
	@ld -o $@ $^ -lusb-1.0 -lm -export-dynamic -shared -soname libsane.so
	@echo STRIP $@
	@strip -x $@

test: test.o $(OBJS)
test:
	@echo LD $@
	@gcc $^ -lsane -lusb-1.0 -lm -o $@

.PHONY: clean
clean:
	rm -f libsane-$(BACKEND).so.* $(addprefix *.,o i s)
