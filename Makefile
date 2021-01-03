default: run

main.o: main.cpp
	g++ -std=c++17 -Wall -Werror -c lib/json.hpp main.cpp

main: main.o
	g++ main.o -o main -lvault -lcurl -lpqxx

run: main
	./main

.PHONY: clean
clean:
	rm -f *.o main

vault:
	docker run -p 8200:8200 vault

postgres:
	docker run -e POSTGRES_USER=postgres -e POSTGRES_PASSWORD=postgres -p 5432:5432 postgres