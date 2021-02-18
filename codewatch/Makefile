gosrc := $(shell find ./ -iname '*.go')

./bin/codewatch: $(gosrc)
	mkdir -p bin/
	go build -o bin/ ./cmd/codewatch

.PHONY: clean
clean:
	rm -f bin/codewatch

.PHONY: test
test:
	go clean -testcache && go test -v ./codewatch/
