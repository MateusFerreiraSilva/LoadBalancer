CC = g++
CFLAGS = -c -g

All:	
	$(CC) $(CFLAGS) *.cpp 

LoadBalancer: All
	$(CC) -o LoadBalancer *.o

Rebuild: Clean LoadBalancer

Run: LoadBalancer
	./LoadBalancer

Clean:
	rm *.o	
	rm LoadBalancer