# WebCit-NG

This is WebCit-NG, a complete refactoring of the WebCit server that will
focus on "REST first" and build around that.  The server code is well layered
with as little spaghetti as possible.

Please don't mess with this yet.  I'm only pushing it upstream so it gets backed up.

Yes, I know the Makefile is built in a way that forces it to recompile everything
when you touch even one file.  For the time being this is acceptable.

## Design goals
* Hold as little state as possible
* Require NO cleanup.  Killing the process lets the OS reclaim all resources.
* As much as possible, resources should be freed by just coming back down the stack.
  Avoid global variables and thread-local variables as much as possible.
* Readability of the code is more important than shaving off a few CPU cycles.
* Throw sensitive data such as passwords back and forth in clear text.
  If you want privacy, encrypt the whole session.  Anything else is false security.

REST format URLs will generally take the form of:

  /ctdl/objectClass/[container/]object[/operation]

## We are using
* libcitadel for information about the Citadel server, some string handling, and the JSON encoder
* Expat for DAV handling
* OpenSSL for TLS

## We are NOT using
* Your favorite javascript library
* Your favorite CSS framework
