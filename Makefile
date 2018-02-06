.phony all: 
all: ACS.c
        gcc ACS.c -lpthread -o ACS
        
.phony clean:
clean:
        -rm -rf *.o *.exe