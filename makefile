.PHONY : all

.PHONY : test

.PHONY : clean

all : supermarket.c config.txt
	gcc -std=c99 -D_POSIX_C_SOURCE=200112L supermarket.c -o supermarket.out -lpthread
	
test : 
	(./supermarket.out config.txt & echo $$! > spid) &
	sleep 25s; \
	kill -1 $$(cat spid); \
	tail --pid=$$(cat spid) -f /dev/null; \
	chmod +x ./analisi.sh; \
	./analisi.sh
	
clean :
	rm -f ./supermarket.out ./file_di_log.txt ./log_cassieri.txt ./log_clienti.txt ./spid
