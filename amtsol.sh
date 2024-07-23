#!/usr/bin/env bash
# Example script to utilze amtterm to connect  to remote AMT sites using SSL

V=""
if [[ $1 == "-v" ]]; then
	V="-v"
	shift
fi

# Example using Mutual TLS
CLIENT_CERT="~/Critical/SSL_CERTS/AMT_CLIENT_AND_CA/amtclient.cert"
CLIENT_KEY="~/Critical/SSL_CERTS/AMT_CLIENT_AND_CA/amtclient.key"
amtterm  $V -A  -c $CLIENT_CERT -k $CLIENT_KEY $1

