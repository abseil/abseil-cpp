
.PHONY: all
all: bin/hello

bin/hello: hello.c version
	mkdir -p bin
	gcc -DVERSION=\""$(shell cat version)"\" -o bin/hello hello.c

.PHONY: clean 
clean:
	rm -f bin/hello

.PHONY: upload
upload:
	az artifacts universal publish \
	  --scope project \
	  --organization https://dev.azure.com/minervapoc \
	  --project minerva-poc \
	  --feed minerva-bin \
	  --name hello \
	  --version $(shell cat version) \
	  --description "hello" \
	  --path bin
