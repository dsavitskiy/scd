
CPP 	:= g++
LD	:= g++
OBJS := scd_main.o
TGT	:= scd

%.o: %.cpp
	$(CPP) $(CPPFLAGS) -c -o $@ $<

$(TGT): $(OBJS)
	$(LD) $(LDFLAGS) -o scd $(OBJS)

all: $(TGT)
	@echo "Done"

clean:
	rm -rf $(OBJS) $(TGT)

