#!/bin/bash

openssl genrsa -3 3072 > ../resources/mrsigner.priv
openssl rsa -in ../resources/mrsigner.priv -pubout > ../resources/mrsigner.pub
