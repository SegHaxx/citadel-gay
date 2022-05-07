#!/usr/bin/env -S buildah unshare bash -ev

# up to date baseline layer with apk cache
run="buildah run -v $HOME/.cache/apk:/var/cache/apk:Z"
base=$(buildah from docker.io/library/alpine)
$run $base ln -s /var/cache/apk /etc/apk/cache
$run $base apk -U upgrade
base=$(buildah commit --rm $base)

# build environment
builder=$(buildah from $base)
$run $builder apk add make gcc musl-dev openssl-dev zlib-dev #gnu-libiconv-dev

buildah copy $builder . /build

# do build
buildah config --workingdir /build/libcitadel $builder
$run $builder ./configure
$run $builder make
$run $builder make install

buildah config --workingdir /build/textclient $builder
$run $builder ./configure --ctdldir=/citadel-client
$run $builder make
$run $builder make install

# stage client container
client=$(buildah from $base)
buildah copy $client $(buildah mount $builder)/usr/local/lib/* /usr/local/lib/
buildah copy $client $(buildah mount $builder)/usr/local/bin/citadel /usr/local/bin/
buildah copy $client $(buildah mount $builder)/citadel.rc /citadel-client/
ln -s /citadel-client/citadel.rc $(buildah mount $client)/etc/
buildah config --workingdir /citadel-client $client
buildah config --user guest:users $client
buildah config --cmd citadel $client

buildah config --author "Seg <seg@haxxed.com>" $client
client=$(buildah commit --rm $client citadel-client)

# client container with ssh server
ssh=$(buildah from $client)
buildah copy $ssh start-dropbear.sh /usr/local/bin/start-dropbear
pushd $(buildah mount $ssh)
mkdir citadel-client/dropbear
ln -s /citadel-client/dropbear etc/
echo /usr/local/bin/citadel>>etc/shells
rm etc/motd
popd
buildah config --user root $ssh
$run $ssh apk add dropbear dropbear-openrc
$run $ssh adduser -Dg BBS bbs -s /usr/local/bin/citadel
$run $ssh passwd bbs -d
buildah config --port 22 $ssh
buildah config --cmd start-dropbear $ssh
buildah commit --rm $ssh citadel-client-ssh

buildah rm -a
