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
$run $builder ./bootstrap
$run $builder ./configure --ctdldir=/citadel-client
$run $builder make
$run $builder make install

staging=$(buildah from $base)
buildah copy $staging $(buildah mount $builder)/usr/local/lib/* /usr/local/lib/
buildah copy $staging $(buildah mount $builder)/usr/local/bin/citadel /usr/local/bin/
buildah copy $staging $(buildah mount $builder)/citadel.rc /citadel-client/
buildah copy $staging citadel/messages /citadel-client/messages

buildah config --workingdir / $staging
#$run $staging sh

buildah config --author "seg@haxxed.com" --created-by "Seg" $staging
buildah commit --rm $staging citadel-client
buildah rm -a
