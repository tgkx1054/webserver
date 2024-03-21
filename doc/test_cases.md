Test Report

## Highlight
(1) According to format of an HTTP request, each line should end with `\r\n`, and the request ends with a bank line (an extra `\r\n`).

**So in our test cases, we should cover this format testing. The requests called by curl are well formatted, so we should write a socket client or use linux nc command to test it.**

(2) Like nginx, multiple "/" in url does not affect the results. For example, `/dir//readme.txt` equals to `/dir/readme.txt`

## Test Condition
Webroot is /var/www  
Start server: ./webserver 127.0.0.1 8080 /var/www 5  
Create below files on webroot.

ubuntu@VM-12-8-ubuntu:/var/www/dir$ ubuntu@VM-12-8-ubuntu:/var/www/dir$ ls -l  
total 8  
-rw-r--r-- 1 root root  0 Mar 19 16:07 empty.txt  
-rwx------ 1 root root 11 Mar 19 16:06 forbid.txt  
-rw-r--r-- 1 root root 13 Mar 19 16:00 readme.txt  

## Test Cases By Socket Call
We can write a C++ client to test it, or use linux command `nc`. I used `nc` command to test it.

1.Bad format input, all return "400 Bad Request"  
(1) Incomplete reqeust line  
printf 'GET http://localhost:8080/dir/readme.txt HTTP/1.0' |nc localhost 8080  
printf 'GET http://localhost:8080/dir/readme.txt HTTP/1.0\r' |nc localhost 8080  
printf 'GET http://localhost:8080/dir/readme.txt HTTP/1.0\n' |nc localhost 8080  

(2) Incomplete header line  
printf 'GET http://localhost:8080/dir/readme.txt HTTP/1.0\r\nHost:localhost' |nc localhost 8080  
printf 'GET http://localhost:8080/dir/readme.txt HTTP/1.0\r\nHost:localhost\r' |nc localhost 8080  
printf 'GET http://localhost:8080/dir/readme.txt HTTP/1.0\r\nHost:localhost\n' |nc localhost 8080  

(3) No extra `\r\n` line at end  
printf 'GET http://localhost:8080/dir/readme.txt HTTP/1.0\r\n' |nc localhost 8080  
printf 'GET http://localhost:8080/dir/readme.txt HTTP/1.0\r\nHost:localhost\r\n' |nc localhost 8080  

(4) http protocol not correct  
printf 'GET http:/localhost:8080/dir/readme.txt HTTP/1.0\r\n\r\n' |nc localhost 8080  
printf 'GET https://localhost:8080/dir/readme.txt HTTP/1.0\r\n\r\n' |nc localhost 8080  
printf 'GET http:localhost:8080/dir/readme.txt HTTP/1.0\r\n\r\n' |nc localhost 8080  

2.Correct format, 200 OK  
(1) Normal Case  
printf 'GET http://localhost:8080/dir/readme.txt HTTP/1.0\r\n\r\n' |nc localhost 8080  

(2) Empty File  
printf 'GET http://localhost:8080/dir/empty.txt HTTP/1.0\r\n\r\n' |nc localhost 8080  

(3) With Additional Header  
printf 'GET http://localhost:8080/dir/readme.txt HTTP/1.0\r\nHost:localhost\r\n\r\n' |nc localhost 8080    

(4) Multiple / in url  
printf 'GET http://localhost:8080//dir/readme.txt HTTP/1.0\r\n\r\n' |nc localhost 8080    
printf 'GET http://localhost:8080/dir///readme.txt HTTP/1.0\r\n\r\n' |nc localhost 8080   

3.Correct format, 403 Forbid  
printf 'GET http://localhost:8080/dir/forbid.txt HTTP/1.0\r\n\r\n' |nc localhost 8080  

4.Correct format, 404 Not Found  
printf 'GET http://localhost:8080/dir/new.txt HTTP/1.0\r\n\r\n' |nc localhost 8080  

5.Correct format, 400 Bad Request  
(1) Not GET Method  
printf 'POST http://localhost:8080/dir/readme.txt HTTP/1.0\r\n\r\n' |nc localhost 8080  

(2) Not Http1.0  
printf 'GET http://localhost:8080/dir/readme.txt HTTP/1.1\r\n\r\n' |nc localhost 8080  

(3) Space after /  
printf 'GET http://localhost:8080/ dir/readme.txt HTTP/1.1\r\n\r\n' |nc localhost 8080  

(4) Request directory but not file  
printf 'GET http://localhost:8080/ HTTP/1.1\r\n\r\n' |nc localhost 8080  
printf 'GET http://localhost:8080/dir/ HTTP/1.1\r\n\r\n' |nc localhost 8080  

## Test Cases By Curl
1.200 OK  
(1) Normal Case  
curl -i --http1.0 http://127.0.0.1:8080/dir/readme.txt  

(2) Empty File  
curl -i --http1.0 http://127.0.0.1:8080/dir/empty.txt  

(3) With Additional Header  
curl -i --http1.0 http://127.0.0.1:8080/dir/readme.txt -H"Key:Value"  

(4) Multiple / in url  
curl -i --http1.0 http://127.0.0.1:8080//dir/readme.txt  
curl -i --http1.0 http://127.0.0.1:8080/dir///readme.txt  

2.403 Forbid  
curl -i --http1.0 http://127.0.0.1:8080/dir/forbid.txt  

3.404 Not Found  
curl -i --http1.0 http://127.0.0.1:8080/dir/new.txt  

4.400 Bad Request  
(1) Not GET Method  
curl -X POST -i --http1.0 http://127.0.0.1:8080/dir/readme.txt -H"Key:Value"  

(2) Http1.1  
curl -i --http1.1 http://127.0.0.1:8080/dir/readme.txt  

(3) Space after /  
curl -i --http1.0 http://127.0.0.1:8080/ dir/readme.txt  

(4) Request directory but not file  
curl -i --http1.0 http://127.0.0.1:8080/  
curl -i --http1.0 http://127.0.0.1:8080/dir/  
