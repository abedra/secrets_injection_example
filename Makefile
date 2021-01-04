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

docker:
	docker build -t dynamic-secrets .

docker-network:
	docker network create dynamic-secrets

docker-run: docker
	docker run --net dynamic-secrets dynamic-secrets

vault:
	docker run --net dynamic-secrets -p 8200:8200 vault

postgres:
	docker rm -f dynamic-secrets-postgres
	docker run -e POSTGRES_USER=postgres -e POSTGRES_PASSWORD=postgres -p 5432:5432 --name dynamic-secrets-postgres --net dynamic-secrets postgres