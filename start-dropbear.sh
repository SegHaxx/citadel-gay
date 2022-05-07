#!/bin/sh -e
[ -e /etc/dropbear/ ]||exit 1
if [ ! -e /etc/dropbear/dropbear_dss_host_key ] ; then
	/usr/bin/dropbearkey -t dss -f /etc/dropbear/dropbear_dss_host_key
fi
if [ ! -e /etc/dropbear/dropbear_rsa_host_key ] ; then
	/usr/bin/dropbearkey -t rsa -f /etc/dropbear/dropbear_rsa_host_key
fi
if [ ! -e /etc/dropbear/dropbear_ecdsa_host_key ] ; then
	/usr/bin/dropbearkey -t ecdsa -f /etc/dropbear/dropbear_ecdsa_host_key
fi

exec /usr/sbin/dropbear -FEwBjk
