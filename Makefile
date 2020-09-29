#NAME: Ryan Lam
#EMAIL: ryan.lam53@gmail.com
#ID: 705124474

lab1b: lab1b-client.c lab1b-server.c
	gcc -Wall -Wextra -o lab1b-client lab1b-client.c -lz
	gcc -Wall -Wextra -o lab1b-server lab1b-server.c -lz

lab1b-client: lab1b-client.c
	gcc -Wall -Wextra -o lab1b-client lab1b-client.c -lz

lab1b-server: lab1b-server.c
	gcc -Wall -Wextra -o lab1b-server lab1b-server.c -lz

clean:
	rm -f lab1b-client lab1b-server lab1b-705124474.tar.gz *.txt

dist:
	tar -czvf lab1b-705124474.tar.gz lab1b-client.c lab1b-server.c Makefile README