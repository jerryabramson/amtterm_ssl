#!/usr/bin/env bash
# Example script to utilze amtterm to connect  to remote AMT sites using SSL

V=""
if [[ $1 == "-v" ]]; then
	V="-v"
	shift
fi

# Example using Mutual TLS
CLIENT_CERT="$HOME/Critical/SSL_CERTS/AMT_CLIENT_AND_CA/amtclient.cert"
CLIENT_KEY="$HOME/Critical/SSL_CERTS/AMT_CLIENT_AND_CA/amtclient.key"
#amtterm  $V -A  -c "$CLIENT_CERT" -k "$CLIENT_KEY" $@

# Example using custom root ca
CERT_CA="/Users/jerry/Critical/SSL_CERTS/AMT_ALL_3_2024/AMT4/full_chain.pem"
amtterm  $V -C "$CERT_CA"  -c "$CLIENT_CERT" -k "$CLIENT_KEY" $@

