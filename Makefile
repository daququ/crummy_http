all: crummy_http

crummy_http: crummy_http.c
	gcc -g -Wall -ansi crummy_http.c -o crummy_http

clean:
	rm crummy_http
