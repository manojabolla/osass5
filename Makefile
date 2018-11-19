all: oss user

oss: oss.c
	gcc oss.c -o oss -lrt -lpthread
	
user: user.c
	gcc user.c -o user -lrt -lpthread
	
clean:
	rm -f oss user 
