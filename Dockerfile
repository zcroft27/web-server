FROM gcc:latest

WORKDIR /app

COPY . /app

RUN gcc -o webserver server.c -pthread

EXPOSE 8080

CMD ["./webserver"]
