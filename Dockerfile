FROM gcc:latest

WORKDIR /app

COPY . /app

RUN gcc -o webserver server.c -pthread  # Modify based on your C files and dependencies

EXPOSE 8080

CMD ["./webserver"]
