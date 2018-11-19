all: oss user

oss: oss.c common.h
	gcc oss.c -o oss

user: user.c common.h 
	gcc user.c -o user

clean:
	rm -f user oss *.o oss.log
